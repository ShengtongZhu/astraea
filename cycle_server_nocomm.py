#!/usr/bin/env python3
import subprocess
import time
import signal
import sys
import os
from datetime import datetime
import socket
import json

class CycleServerNocommManager:
    def __init__(self):
        self.server_process = None
        self.tcpdump_process = None
        self.experiment_start_time = None
        self.request_count = 0
        self.data_port = 8888   # Data transfer port
        self.coord_socket = None
        self.coord_server_socket = None
        self.coord_port = 8889  # Coordination port

    def start_tcpdump(self, interface="eno1", output_file="network_trace.pcap"):
        """Start tcpdump to capture network traffic"""
        abs_output_file = os.path.abspath(output_file)
        cmd = [
            "sudo", "tcpdump",
            "-i", interface,
            "-w", abs_output_file,
            "-U",  # flush packets to file as they arrive
            "-s", "100",  # Capture first 100 bytes per packet
            "port", str(self.data_port)  # Only capture traffic on data port
        ]

        print(f"Starting tcpdump: {abs_output_file}")
        # Start in a new process group so we can signal the whole group
        self.tcpdump_process = subprocess.Popen(
            cmd,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            preexec_fn=os.setsid
        )
        time.sleep(1)  # Give tcpdump time to start
        return self.tcpdump_process

    def stop_tcpdump(self):
        """Forcefully stop ALL tcpdump processes with escalating signals and verification."""
        def run_cmd(base_cmd):
            if os.geteuid() == 0:
                return subprocess.run(base_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            else:
                return subprocess.run(["sudo"] + base_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        def pgrep_tcpdump():
            res = subprocess.run(["pgrep", "-x", "tcpdump"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if res.returncode != 0 or not res.stdout.strip():
                return []
            return [int(x) for x in res.stdout.strip().splitlines() if x.strip().isdigit()]

        print("Stopping tcpdump (force)...")

        # Try escalating signals with retries
        for sig_flag in ["-INT", "-TERM", "-KILL"]:
            pids = pgrep_tcpdump()
            if not pids:
                break
            print(f"Sending {sig_flag} to tcpdump PIDs: {pids}")
            # Kill by name (all tcpdump)
            run_cmd(["pkill", sig_flag, "-x", "tcpdump"])

            # Also try signaling our own process group (if still running)
            try:
                if self.tcpdump_process and self.tcpdump_process.poll() is None:
                    pgid = os.getpgid(self.tcpdump_process.pid)
                    sig_map = {"-INT": signal.SIGINT, "-TERM": signal.SIGTERM, "-KILL": signal.SIGKILL}
                    os.killpg(pgid, sig_map[sig_flag])
            except Exception:
                pass

            # Wait up to 2 seconds for processes to die
            for _ in range(10):
                time.sleep(0.2)
                if not pgrep_tcpdump():
                    break

        survivors = pgrep_tcpdump()
        if survivors:
            print(f"Warning: tcpdump survivors after KILL: {survivors}")
        else:
            print("All tcpdump processes stopped.")

        # Clean local handle
        if self.tcpdump_process:
            try:
                self.tcpdump_process.wait(timeout=0.5)
            except Exception:
                pass
            self.tcpdump_process = None

    def clear_dmesg(self):
        """Clear the kernel log buffer"""
        print("Clearing dmesg buffer...")
        result = subprocess.run(["sudo", "dmesg", "-C"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        if result.returncode != 0:
            print(f"Warning: Failed to clear dmesg buffer: {result.stderr}")

    def save_dmesg(self, output_file="dmesg_log.txt"):
        """Save current dmesg output to file"""
        print(f"Saving dmesg to {output_file}...")
        result = subprocess.run(["sudo", "dmesg"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        if result.returncode == 0:
            with open(output_file, 'w') as f:
                f.write(result.stdout)
            print(f"dmesg log saved to {output_file}")
        else:
            print(f"Error: Failed to capture dmesg: {result.stderr}")

    def start_server(self, cc_algo, request_size, perf_log=None):
        """Start the nocomm server sender process with specified CC and size"""

        # Get the original user who ran sudo
        original_user = os.environ.get('SUDO_USER', 'nobody')

        # Get current working directory (should be the astraea directory)
        astraea_dir = os.getcwd()

        # Build command to run as original user with preserved environment
        cmd = [
            "sudo", "-E", "-u", original_user,  # -E preserves environment including virtual env
            "-H",  # Use original user's home directory
            "--",
            "./src/build/bin/new_server_sender_nocomm",
            f"--port={self.data_port}",
            f"--cong={cc_algo}",
            f"--size={request_size}",
            f"--pyhelper=./python/infer.py",
            f"--model=./models/py-model1/"
        ]

        if perf_log:
            cmd.append(f"--perf-log={perf_log}")

        print(f"Starting server as user '{original_user}' with CC: {cc_algo} and size: {request_size} bytes")
        print(f"Working directory: {astraea_dir}")

        # Run in the astraea directory with preserved environment
        self.server_process = subprocess.Popen(cmd, cwd=astraea_dir)

        # Give server time to start and bind to port
        time.sleep(2)
        return self.server_process

    def stop_server(self):
        """Stop the server process"""
        if self.server_process:
            print("Stopping server...")
            self.server_process.terminate()
            try:
                self.server_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                print("Force killing server...")
                self.server_process.kill()
                self.server_process.wait()
            self.server_process = None

    def wait_for_server_exit(self):
        """Wait for server to exit (indicating request completed)"""
        if self.server_process:
            print("Waiting for server to complete request...")
            self.server_process.wait()
            self.server_process = None

    def start_coordination_server(self):
        """Start coordination server to receive client requests"""
        self.coord_server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.coord_server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.coord_server_socket.bind(('', self.coord_port))
        self.coord_server_socket.listen(1)
        print(f"Coordination server listening on port {self.coord_port}")

    def wait_for_client_request(self):
        """Wait for client request with CC and size info"""
        try:
            print("Waiting for client request...")
            self.coord_socket, addr = self.coord_server_socket.accept()
            print(f"Client connected from {addr}")

            data = self.coord_socket.recv(1024).decode('utf-8')
            request = json.loads(data)

            cc_algo = request['cc_algo']
            request_size = request['request_size']

            print(f"Received request: CC={cc_algo}, Size={request_size} bytes")

            # Send acknowledgment
            response = {'status': 'ready'}
            self.coord_socket.send(json.dumps(response).encode('utf-8'))

            return cc_algo, request_size
        except Exception as e:
            print(f"Error receiving client request: {e}")
            return None, None

    def notify_client_completion(self):
        """Notify client that request is completed"""
        try:
            if self.coord_socket:
                response = {'status': 'completed'}
                self.coord_socket.send(json.dumps(response).encode('utf-8'))
                self.coord_socket.close()
                self.coord_socket = None
        except Exception as e:
            print(f"Error notifying client: {e}")

    def stop_coordination_server(self):
        """Stop coordination server"""
        if self.coord_server_socket:
            self.coord_server_socket.close()
            self.coord_server_socket = None

    def wait_for_server_exit(self):
        """Wait for server to exit (indicating request completed)"""
        if self.server_process:
            print("Waiting for server to complete request...")
            self.server_process.wait()
            self.server_process = None

    def handle_single_request(self, cc_algo, request_size, interface="eno1"):
        """Handle a single request with tcpdump capture and dmesg logs"""
        self.request_count += 1
        request_start_time = datetime.now().strftime("%m-%d-%H-%M-%S")

        tcpdump_file = f"{cc_algo}_{request_size}_{request_start_time}.pcap"
        dmesg_file = f"{cc_algo}_{request_size}_{request_start_time}_dmesg.txt"
        server_perf_log = f"{cc_algo}_{request_size}_{self.request_count}_{request_start_time}_server.log"

        try:
            print(f"\n=== Request {self.request_count} - {request_start_time} ===")
            print(f"CC: {cc_algo}, Size: {request_size} bytes")

            # Step 1: Clear dmesg buffer
            self.clear_dmesg()

            # Step 2: Start tcpdump
            self.start_tcpdump(interface=interface, output_file=tcpdump_file)

            # Step 3: Start server with specified CC and size
            self.start_server(cc_algo=cc_algo, request_size=request_size, perf_log=server_perf_log)
            print("Server ready for data connection...")

            # Step 4: Wait for server to complete the request
            self.wait_for_server_exit()
            print(f"Request {self.request_count} completed")

            # Immediately stop tcpdump once the request is completed
            time.sleep(5)
            self.stop_tcpdump()
            self.save_dmesg(dmesg_file)

        except Exception as e:
            print(f"Error handling request {self.request_count}: {e}")
            return False

        finally:
            # Step 5: Cleanup after request
            self.stop_server()
            self.stop_tcpdump()

        return True

    def run_cycle_experiment(self, interface="eno1"):
        """Handle incoming coordinated requests and run them (nocomm data path)"""
        print("Starting server cycle (nocomm with coordination)")
        print(f"Coordination port: {self.coord_port}")
        print(f"Data connection (listening) on port: {self.data_port}")
        print("Press Ctrl+C to stop\n")

        self.start_coordination_server()
        cycle_count = 0

        try:
            while True:
                cycle_count += 1
                print(f"\n=== Cycle {cycle_count} ===")

                # Wait for the client to specify CC and request size
                cc_algo, request_size = self.wait_for_client_request()
                if cc_algo is None or request_size is None:
                    print("Invalid request, continuing...")
                    continue

                success = self.handle_single_request(cc_algo=cc_algo, request_size=request_size, interface=interface)
                if not success:
                    print("Request failed, continuing to next...")

                # Step: Notify client that server completed the request
                self.notify_client_completion()

                print("Waiting 10 seconds before next request...")
                time.sleep(10)

        except KeyboardInterrupt:
            print("\nReceived interrupt, exiting...")
            sys.exit(0)
        finally:
            self.stop_coordination_server()

def signal_handler(sig, frame):
    print("\nReceived interrupt, exiting...")
    sys.exit(0)

if __name__ == "__main__":
    signal.signal(signal.SIGINT, signal_handler)

    # Optional: pass interface as first arg, default "eno1"
    interface = sys.argv[1] if len(sys.argv) > 1 else "eno1"

    server_manager = CycleServerNocommManager()
    server_manager.run_cycle_experiment(interface=interface)