#!/usr/bin/env python3
import subprocess
import sys
import time
import csv
from datetime import datetime

def run_client(server_ip="127.0.0.1", port=8888, size_bytes=1024*1024, cc_algo="cubic", perf_log=None):
    """Run the new client receiver with specified parameters"""
    cmd = [
        "./src/build/bin/new_client_receiver",
        "--ip", server_ip,
        "--port", str(port),
        "--size", str(size_bytes),
        "--cc", cc_algo
    ]
    
    if perf_log:
        cmd.extend(["--perf-log", perf_log])
    
    print(f"Running client with command: {' '.join(cmd)}")
    
    start_time = time.time()
    result = subprocess.run(cmd, capture_output=True, text=True)
    end_time = time.time()
    
    duration = end_time - start_time
    
    if result.returncode == 0:
        print(f"Client completed successfully in {duration:.2f} seconds")
        print(f"Stdout: {result.stdout}")
    else:
        print(f"Client failed with return code {result.returncode}")
        print(f"Stderr: {result.stderr}")
    
    return result.returncode == 0, duration, start_time

def log_request_data(log_file, start_time, request_size_bytes, duration, success, throughput_kbps=None):
    """Log request data to CSV file"""
    start_time_str = datetime.fromtimestamp(start_time).strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
    
    # Check if file exists to write header
    file_exists = False
    try:
        with open(log_file, 'r'):
            file_exists = True
    except FileNotFoundError:
        pass
    
    with open(log_file, 'a', newline='') as csvfile:
        fieldnames = ['timestamp', 'start_time', 'request_size_bytes', 'request_size_kb', 
                     'duration_seconds', 'success', 'throughput_kbps']
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        
        # Write header if file is new
        if not file_exists:
            writer.writeheader()
        
        writer.writerow({
            'timestamp': datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3],
            'start_time': start_time_str,
            'request_size_bytes': request_size_bytes,
            'request_size_kb': request_size_bytes // 1024,
            'duration_seconds': f'{duration:.6f}',
            'success': success,
            'throughput_kbps': f'{throughput_kbps:.2f}' if throughput_kbps else 'N/A'
        })

if __name__ == "__main__":
    # Define the cycle of request sizes in KB
    size_cycle_kb = [32, 16, 8, 4, 2, 1, 512]
    
    server_ip = "103.49.160.232"
    port = 8888
    cc_algo = "cubic"
    
    # Create log file with timestamp
    log_filename = f"client_requests_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    
    print(f"Starting infinite client cycle with sizes: {size_cycle_kb} KB")
    print(f"Connecting to server at {server_ip}:{port}")
    print(f"Logging requests to: {log_filename}")
    print("Press Ctrl+C to stop")
    
    cycle_count = 0
    
    try:
        while True:
            cycle_count += 1
            print(f"\n=== Cycle {cycle_count} ===")
            
            for size_kb in size_cycle_kb:
                size_bytes = size_kb * 1024
                
                print(f"\nRequesting {size_kb} KB ({size_bytes} bytes)...")
                
                success, duration, start_time = run_client(
                    server_ip=server_ip,
                    port=port,
                    size_bytes=size_bytes,
                    cc_algo=cc_algo,
                    perf_log=f"client_perf_{size_kb}kb_cycle{cycle_count}.log"
                )
                
                throughput_kbps = None
                if success:
                    throughput_kbps = (size_kb * 8) / duration  # Convert to Kbps
                    print(f"Throughput: {throughput_kbps:.2f} Kbps")
                else:
                    print(f"Failed to transfer {size_kb} KB")
                
                # Log the request data
                log_request_data(log_filename, start_time, size_bytes, duration, success, throughput_kbps)
                
                # Small delay between requests
                time.sleep(0.5)
            
            print(f"Completed cycle {cycle_count}")
            time.sleep(1)  # Delay between cycles
            
    except KeyboardInterrupt:
        print(f"\nStopped after {cycle_count} cycles")
        print(f"Request log saved to: {log_filename}")
        sys.exit(0)