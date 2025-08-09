#!/usr/bin/env python3
import subprocess
import sys
import time
import signal
import os
import threading

class ClientRunner:
    def __init__(self):
        self.client_process = None
        self.base_dir = os.path.dirname(os.path.abspath(__file__))
        self.data_sent = 0
        
    def run_client(self, server_ip, port=12345, cong="cubic", duration=10, perf_log="client_perf.log"):
        """Run the client with specified parameters"""
        cmd = [
            f"{self.base_dir}/src/build/bin/client_receiver",
            f"--ip={server_ip}",
            f"--port={port}",
            f"--cong={cong}",
            f"--perf-log={perf_log}"
        ]
        
        print(f"Starting client with command: {' '.join(cmd)}")
        try:
            self.client_process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            print(f"Client started with PID: {self.client_process.pid}")
            
            # Let client run for specified duration
            print(f"Client will run for {duration} seconds...")
            time.sleep(duration)
            
            # Stop client
            self.stop_client()
            return True
            
        except Exception as e:
            print(f"Failed to run client: {e}")
            return False
    
    def stop_client(self):
        """Stop the client process"""
        if self.client_process and self.client_process.poll() is None:
            print("Stopping client...")
            self.client_process.terminate()
            try:
                stdout, stderr = self.client_process.communicate(timeout=5)
                if stdout:
                    print("Client output:", stdout)
                if stderr:
                    print("Client errors:", stderr)
            except subprocess.TimeoutExpired:
                print("Client didn't stop gracefully, killing...")
                self.client_process.kill()
                self.client_process.wait()
            print("Client stopped")

def calculate_duration_for_size(target_size_mb, estimated_throughput_mbps=100):
    """Calculate duration needed to receive target size in MB"""
    # Add some buffer time
    duration = max(10, int(target_size_mb / estimated_throughput_mbps * 8) + 5)
    return duration

def main():
    if len(sys.argv) < 4:
        print("Usage: python run_client.py <server_ip> <port> <target_size_mb> [throughput_estimate_mbps]")
        print("Example: python run_client.py 103.49.160.232 12345 100 50")
        sys.exit(1)
    
    server_ip = sys.argv[1]
    port = int(sys.argv[2])
    target_size_mb = float(sys.argv[3])
    throughput_estimate = float(sys.argv[4]) if len(sys.argv) > 4 else 100
    
    # Calculate duration based on target size
    duration = calculate_duration_for_size(target_size_mb, throughput_estimate)
    
    client = ClientRunner()
    
    def signal_handler(signum, frame):
        print("\nReceived signal, stopping client...")
        client.stop_client()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    try:
        print(f"Requesting {target_size_mb} MB of data (estimated duration: {duration}s)")
        if client.run_client(server_ip, port, duration=duration):
            print(f"Client finished receiving data")
        else:
            print("Failed to run client")
            sys.exit(1)
    finally:
        client.stop_client()

if __name__ == "__main__":
    main()