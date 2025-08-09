#!/usr/bin/env python3
import subprocess
import sys
import time

def run_client(server_ip="127.0.0.1", port=8888, size_bytes=1024*1024, cc_algo="cubic", perf_log=None):
    """Run the new client receiver with specified parameters"""
    cmd = [
        "./new_client_receiver",
        "--ip", server_ip,
        "--port", str(port),
        "--size", str(size_bytes),
        "--cc", cc_algo
    ]
    
    if perf_log:
        cmd.extend(["--perf-log", perf_log])
    
    print(f"Running client with command: {' '.join(cmd)}")
    
    start_time = time.time()
    result = subprocess.run(cmd, cwd="src", capture_output=True, text=True)
    end_time = time.time()
    
    duration = end_time - start_time
    
    if result.returncode == 0:
        print(f"Client completed successfully in {duration:.2f} seconds")
        print(f"Stdout: {result.stdout}")
    else:
        print(f"Client failed with return code {result.returncode}")
        print(f"Stderr: {result.stderr}")
    
    return result.returncode == 0, duration

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 new_run_client.py <size_in_mb>")
        sys.exit(1)
    
    try:
        size_mb = float(sys.argv[1])
        size_bytes = int(size_mb * 1024 * 1024)
        
        success, duration = run_client(
            server_ip="127.0.0.1",
            port=8888,
            size_bytes=size_bytes,
            cc_algo="cubic",
            perf_log=f"client_perf_{size_mb}mb.log"
        )
        
        if success:
            throughput_mbps = (size_mb * 8) / duration  # Convert to Mbps
            print(f"Throughput: {throughput_mbps:.2f} Mbps")
        
    except ValueError:
        print("Error: Size must be a number")
        sys.exit(1)