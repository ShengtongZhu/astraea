#!/usr/bin/env python3
import subprocess
import sys
import time
import threading
import os

def run_server(port, timeout=120):
    """Run server in a separate process"""
    cmd = ["python3", "run_server.py", str(port), str(timeout)]
    return subprocess.Popen(cmd)

def run_client(server_ip, port, size_mb, throughput_estimate=100):
    """Run client for specific size"""
    cmd = ["python3", "run_client.py", server_ip, str(port), str(size_mb), str(throughput_estimate)]
    process = subprocess.Popen(cmd)
    process.wait()
    return process.returncode

def main():
    if len(sys.argv) < 3:
        print("Usage: python run_experiment.py <server_ip> <port> <size1_mb> [size2_mb] [size3_mb] ...")
        print("Example: python run_experiment.py 103.49.160.232 12345 50 100 200")
        sys.exit(1)
    
    server_ip = sys.argv[1]
    port = int(sys.argv[2])
    sizes = [float(size) for size in sys.argv[3:]]
    
    print(f"Running experiment with sizes: {sizes} MB")
    
    for i, size in enumerate(sizes, 1):
        print(f"\n=== Request {i}: {size} MB ===")
        
        # Start server
        print("Starting server...")
        server_process = run_server(port)
        time.sleep(2)  # Give server time to start
        
        try:
            # Run client
            print(f"Running client for {size} MB...")
            client_result = run_client(server_ip, port, size)
            
            if client_result == 0:
                print(f"Request {i} completed successfully")
            else:
                print(f"Request {i} failed with code {client_result}")
                
        finally:
            # Ensure server is stopped
            print("Stopping server...")
            server_process.terminate()
            try:
                server_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server_process.kill()
                server_process.wait()
            
            time.sleep(1)  # Brief pause between requests
    
    print("\nAll requests completed!")

if __name__ == "__main__":
    main()