#!/usr/bin/env python3
import time
import subprocess
from new_run_server import NewServerManager
from new_run_client import run_client

def run_experiment(sizes_mb, server_port=8888, cc_algo="cubic"):
    """Run experiments for different data sizes"""
    server = NewServerManager()
    results = []
    
    for size_mb in sizes_mb:
        print(f"\n=== Testing {size_mb} MB ===")
        
        try:
            # Start server
            print("Starting server...")
            server.start_server(
                port=server_port,
                cc_algo=cc_algo,
                perf_log=f"server_perf_{size_mb}mb.log"
            )
            
            # Wait a bit for server to be ready
            time.sleep(2)
            
            # Run client
            print(f"Running client for {size_mb} MB...")
            size_bytes = int(size_mb * 1024 * 1024)
            success, duration = run_client(
                server_ip="127.0.0.1",
                port=server_port,
                size_bytes=size_bytes,
                cc_algo=cc_algo,
                perf_log=f"client_perf_{size_mb}mb.log"
            )
            
            if success:
                throughput_mbps = (size_mb * 8) / duration
                results.append((size_mb, duration, throughput_mbps))
                print(f"Success: {duration:.2f}s, {throughput_mbps:.2f} Mbps")
            else:
                print(f"Failed for {size_mb} MB")
                results.append((size_mb, None, None))
            
        except Exception as e:
            print(f"Error during {size_mb} MB test: {e}")
            results.append((size_mb, None, None))
        
        finally:
            # Stop server
            server.stop_server()
            time.sleep(1)  # Brief pause between tests
    
    # Print summary
    print("\n=== EXPERIMENT RESULTS ===")
    print("Size (MB)\tDuration (s)\tThroughput (Mbps)")
    print("-" * 45)
    for size_mb, duration, throughput in results:
        if duration is not None:
            print(f"{size_mb}\t\t{duration:.2f}\t\t{throughput:.2f}")
        else:
            print(f"{size_mb}\t\tFAILED\t\tFAILED")

if __name__ == "__main__":
    # Test different sizes
    test_sizes = [1, 5, 10, 50, 100]  # MB
    
    print("Starting network performance experiment...")
    print(f"Testing sizes: {test_sizes} MB")
    
    run_experiment(test_sizes, server_port=8888, cc_algo="cubic")