#!/usr/bin/env python3
import subprocess
import time
import signal
import sys
import os
import socket
import json
from datetime import datetime

class CycleServerManager:
    def __init__(self):
        self.server_process = None
        self.tcpdump_process = None
        self.coord_socket = None
        self.coord_server_socket = None
        self.experiment_start_time = None
        self.request_count = 0
        self.coord_port = 8889  # Coordination port
        self.data_port = 8888   # Data transfer port
    
    def start_coordination_server(self):
        """Start coordination server to receive client requests"""
        self.coord_server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.coord_server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.coord_server_socket.bind(('', self.coord_port))
        self.coord_server_socket.listen(1)
        print(f"Coordination server listening on port {self.coord_port}")
    
    def stop_coordination_server(self):
        """Stop coordination server"""
        if self.coord_server_socket:
            self.coord_server_socket.close()
            self.coord_server_socket = None
    
    def wait_for_client_request(self):
        """Wait for client request with CC and size info"""
        try:
            print("Waiting for client request...")
            self.coord_socket, addr = self.coord_server_socket.accept()
            print(f"Client connected from {addr}")
            
            # Receive request message
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
    
    def start_tcpdump(self, interface="eth0", output_file="network_trace.pcap"):
        """Start tcpdump to capture network traffic"""
        cmd = [
            "sudo", "tcpdump", 
            "-i", interface,
            "-w", output_file,
            "-s", "100",  # Capture full packets
            "port", str(self.data_port)  # Only capture traffic on data port
        ]
        
        print(f"Starting tcpdump: {output_file}")
        self.tcpdump_process = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1)  # Give tcpdump time to start
        return self.tcpdump_process
    
    def stop_tcpdump(self):
        """Stop tcpdump process"""
        if self.tcpdump_process:
            print("Stopping tcpdump...")
            self.tcpdump_process.terminate()
            try:
                self.tcpdump_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                print("Force killing tcpdump...")
                self.tcpdump_process.kill()
                self.tcpdump_process.wait()
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
    
    def start_server(self, cc_algo, perf_log=None):
        """Start the new server sender process with specified CC"""
        
        cmd = [
            "./src/build/bin/new_server_sender",
            f"--port={self.data_port}",
            f"--cong={cc_algo}",
            f"--pyhelper=./python/infer.py",
            f"--model=./models/py-model1/"
        ]
        
        if perf_log:
            cmd.append(f"--perf-log={perf_log}")
        
        print(f"Starting server as user '{original_user}' with CC: {cc_algo}")
        self.server_process = subprocess.Popen(cmd)
        
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
    
    def handle_single_request(self, interface="eth0"):
        """Handle a single client request with full cycle"""
        # Wait for client request
        cc_algo, request_size = self.wait_for_client_request()
        if cc_algo is None:
            return False
        
        self.request_count += 1
        request_start_time = datetime.now().strftime("%Y%m%d_%H%M%S_%f")[:-3]
        
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
            
            # Step 3: Start server with specified CC
            self.start_server(cc_algo=cc_algo, perf_log=server_perf_log)
            
            print("Server ready for data connection...")
            
            # Step 4: Wait for server to complete the request
            self.wait_for_server_exit()
            
            print(f"Request {self.request_count} completed")
            
            # Step 5: Notify client of completion
            self.notify_client_completion()
            
        except Exception as e:
            print(f"Error handling request {self.request_count}: {e}")
            return False
        
        finally:
            # Step 6: Cleanup after request
            self.stop_server()
            self.stop_tcpdump()
            self.save_dmesg(dmesg_file)
            
            print(f"Request {self.request_count} files saved:")
            print(f"  - Network trace: {tcpdump_file}")
            print(f"  - dmesg log: {dmesg_file}")
            print(f"  - Server performance: {server_perf_log}")
        
        return True
    
    def run_cycle_experiment(self, interface="eth0"):
        """Run continuous experiment handling multiple requests"""
        self.experiment_start_time = time.time()
        
        print(f"Starting cycle server experiment on interface: {interface}")
        print("Note: This script requires sudo privileges for tcpdump and dmesg operations")
        print(f"Coordination server on port {self.coord_port}, data server on port {self.data_port}")
        print("Press Ctrl+C to stop the experiment\n")
        
        try:
            self.start_coordination_server()
            
            while True:
                success = self.handle_single_request(interface=interface)
                if not success:
                    print("Failed to handle request, continuing...")
                print("Ready for next client request...\n")
                
        except KeyboardInterrupt:
            print("\nReceived interrupt signal, stopping experiment...")
        
        finally:
            # Final cleanup
            self.stop_server()
            self.stop_tcpdump()
            self.stop_coordination_server()
            
            experiment_duration = time.time() - self.experiment_start_time
            print(f"\nExperiment completed after {experiment_duration:.2f} seconds")
            print(f"Total requests handled: {self.request_count}")

def signal_handler(sig, frame):
    print("\nReceived interrupt signal")
    sys.exit(0)

if __name__ == "__main__":
    signal.signal(signal.SIGINT, signal_handler)
    
    # Get network interface from command line or use default
    interface = sys.argv[1] if len(sys.argv) > 1 else "eth0"
    
    server_manager = CycleServerManager()
    server_manager.run_cycle_experiment(interface=interface)