#!/usr/bin/env python3
import subprocess
import time
import signal
import sys
import os
from datetime import datetime

target_cc = "astraea"

class CycleServerManager:
    def __init__(self):
        self.server_process = None
        self.tcpdump_process = None
        self.experiment_start_time = None
    
    def start_tcpdump(self, interface="eth0", output_file="network_trace.pcap"):
        """Start tcpdump to capture network traffic"""
        cmd = [
            "sudo", "tcpdump", 
            "-i", interface,
            "-w", output_file,
            "-s", "100",  # Capture full packets
            "port", "8888"  # Only capture traffic on our server port
        ]
        
        print(f"Starting tcpdump: {' '.join(cmd)}")
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
        result = subprocess.run(["sudo", "dmesg", "-C"], capture_output=True, text=True)
        if result.returncode != 0:
            print(f"Warning: Failed to clear dmesg buffer: {result.stderr}")
    
    def save_dmesg(self, output_file="dmesg_log.txt"):
        """Save current dmesg output to file"""
        print(f"Saving dmesg to {output_file}...")
        result = subprocess.run(["dmesg"], capture_output=True, text=True)
        if result.returncode == 0:
            with open(output_file, 'w') as f:
                f.write(result.stdout)
            print(f"dmesg log saved to {output_file}")
        else:
            print(f"Error: Failed to capture dmesg: {result.stderr}")
    
    def start_server(self, port=8888, cc_algo=target_cc, perf_log=None):
        """Start the new server sender process"""
        cmd = ["./src/build/bin/new_server_sender", "--port", str(port), "--cc", cc_algo]
        
        if perf_log:
            cmd.extend(["--perf-log", perf_log])
        
        print(f"Starting server with command: {' '.join(cmd)}")
        self.server_process = subprocess.Popen(cmd)
        
        # Give server time to start
        time.sleep(1)
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
    
    def is_server_running(self):
        """Check if server is still running"""
        return self.server_process is not None and self.server_process.poll() is None
    
    def run_cycle_experiment(self, interface="eth0"):
        """Run the complete experiment with tcpdump and dmesg logging"""
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        tcpdump_file = f"{target_cc}_{timestamp}.pcap"
        dmesg_file = f"{target_cc}_{timestamp}.txt"
        server_perf_log = f"{target_cc}_{timestamp}.log"
        
        self.experiment_start_time = time.time()
        
        try:
            # Step 1: Clear dmesg buffer
            self.clear_dmesg()
            
            # Step 2: Start tcpdump
            self.start_tcpdump(interface=interface, output_file=tcpdump_file)
            
            # Step 3: Start server
            self.start_server(port=8888, cc_algo=target_cc, perf_log=server_perf_log)
            
            print(f"\nExperiment started at {datetime.now()}")
            print(f"Network trace will be saved to: {tcpdump_file}")
            print(f"dmesg log will be saved to: {dmesg_file}")
            print(f"Server performance log: {server_perf_log}")
            print("\nServer is ready for client connections...")
            print("Press Ctrl+C to stop the experiment")
            
            # Keep server running and handle multiple client connections
            while True:
                if not self.is_server_running():
                    print("Server process died, restarting...")
                    self.start_server(port=8888, cc_algo="cubic", perf_log=server_perf_log)
                
                time.sleep(1)
                
        except KeyboardInterrupt:
            print("\nReceived interrupt signal, stopping experiment...")
        
        finally:
            # Cleanup: stop server, stop tcpdump, save dmesg
            self.stop_server()
            self.stop_tcpdump()
            self.save_dmesg(dmesg_file)
            
            experiment_duration = time.time() - self.experiment_start_time
            print(f"\nExperiment completed after {experiment_duration:.2f} seconds")
            print(f"Files generated:")
            print(f"  - Network trace: {tcpdump_file}")
            print(f"  - dmesg log: {dmesg_file}")
            print(f"  - Server performance: {server_perf_log}")

def signal_handler(sig, frame):
    print("\nReceived interrupt signal")
    sys.exit(0)

if __name__ == "__main__":
    signal.signal(signal.SIGINT, signal_handler)
    
    # Get network interface from command line or use default
    interface = sys.argv[1] if len(sys.argv) > 1 else "eth0"
    
    print(f"Starting cycle server experiment on interface: {interface}")
    print("Note: This script requires sudo privileges for tcpdump and dmesg operations")
    
    server_manager = CycleServerManager()
    server_manager.run_cycle_experiment(interface=interface)