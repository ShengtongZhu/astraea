#!/usr/bin/env python3
import subprocess
import sys
import time
import csv
from datetime import datetime
import socket
import json

def run_client(server_ip="127.0.0.1", port=8888, size_bytes=1024*1024, cc_algo="cubic", perf_log=None):
    """Run the nocomm client receiver with specified parameters"""
    cmd = [
        "./src/build/bin/new_client_receiver_nocomm",
        f"--ip={server_ip}",
        f"--port={port}",
        f"--size={size_bytes}",
        f"--cong={cc_algo}"
    ]

    if perf_log:
        cmd.append(f"--perf-log={perf_log}")

    print(f"Running client with command: {' '.join(cmd)}")

    start_time = time.time()
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    end_time = time.time()

    duration = end_time - start_time

    if result.returncode == 0:
        print(f"Client completed successfully in {duration:.2f} seconds")
        if result.stdout.strip():
            print(f"Stdout: {result.stdout}")
    else:
        print(f"Client failed with return code {result.returncode}")
        print(f"Stderr: {result.stderr}")

    return result.returncode == 0, duration, start_time

def log_request_data(log_file, start_time, server_cc_algo, client_cc_algo, request_size_bytes, duration, success, throughput_kbps=None):
    """Log request data to CSV file"""
    start_time_str = datetime.fromtimestamp(start_time).strftime('%m-%d-%H-%M-%S')

    # Check if file exists to write header
    file_exists = False
    try:
        with open(log_file, 'r'):
            file_exists = True
    except FileNotFoundError:
        pass

    with open(log_file, 'a', newline='') as csvfile:
        fieldnames = [
            'timestamp', 'start_time', 'server_cc_algo', 'client_cc_algo',
            'request_size_bytes', 'request_size_kb',
            'duration_seconds', 'success', 'throughput_kbps'
        ]
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)

        # Write header if file is new
        if not file_exists:
            writer.writeheader()

        writer.writerow({
            'timestamp': datetime.now().strftime('%m-%d-%H-%M-%S'),
            'start_time': start_time_str,
            'server_cc_algo': server_cc_algo,
            'client_cc_algo': client_cc_algo,
            'request_size_bytes': request_size_bytes,
            'request_size_kb': request_size_bytes // 1024,
            'duration_seconds': f"{duration:.2f}",
            'success': success,
            'throughput_kbps': f"{throughput_kbps:.2f}" if throughput_kbps is not None else ''
        })

# top-level function: send_request_to_server / wait_for_completion
def send_request_to_server(server_ip, coord_port, cc_algo, request_size):
    """Send request with CC and size to coordination server"""
    try:
        coord_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        coord_sock.connect((server_ip, coord_port))

        request = {
            'cc_algo': cc_algo,
            'request_size': request_size
        }
        coord_sock.send(json.dumps(request).encode('utf-8'))

        response_data = coord_sock.recv(1024).decode('utf-8')
        response = json.loads(response_data)

        if response.get('status') != 'ready':
            raise Exception(f"Server not ready: {response}")

        print("Server is ready for data connection")
        return coord_sock

    except Exception as e:
        print(f"Error coordinating with server: {e}")
        try:
            coord_sock.close()
        except Exception:
            pass
        return None

def wait_for_completion(coord_sock):
    """Wait for server completion notification"""
    try:
        response_data = coord_sock.recv(1024).decode('utf-8')
        response = json.loads(response_data)
        coord_sock.close()
        return response.get('status') == 'completed'
    except Exception as e:
        print(f"Error waiting for completion: {e}")
        try:
            coord_sock.close()
        except Exception:
            pass
        return False

if __name__ == "__main__":
    # Config
    cc_arr = ["astraea"]  # Server-side CC sequence to mirror
    size_cycle_kb = [32 * 1024, 16 * 1024, 8 * 1024, 4 * 1024, 2 * 1024, 1 * 1024, 512]  # KB
    server_ip = "103.49.160.232"  # Change if needed
    coord_port = 8889        # Coordination port
    data_port = 8888         # Data transfer port
    client_cc = "cubic"      # Client-side CC algorithm

    if len(sys.argv) > 1:
        server_ip = sys.argv[1]
    if len(sys.argv) > 2:
        try:
            data_port = int(sys.argv[2])
        except ValueError:
            print("Invalid port provided, using default 8888")

    log_filename = f"client_requests_{datetime.now().strftime('%m-%d-%H-%M-%S')}.csv"

    print("Starting client cycle (nocomm)")
    print(f"CC algorithms (server-side expected): {cc_arr}")
    print(f"Request sizes: {size_cycle_kb} KB")
    print(f"Server coordination: {server_ip}:{coord_port}")
    print(f"Data connection: {server_ip}:{data_port}")
    print(f"Logging requests to: {log_filename}")
    print("Press Ctrl+C to stop\n")

    cycle_count = 0
    request_count = 0

    try:
        while True:
            cycle_count += 1
            print(f"\n=== Cycle {cycle_count} ===")

            for server_cc in cc_arr:
                print(f"\n--- Expecting Server CC Algorithm: {server_cc} ---")

                for size_kb in size_cycle_kb:
                    request_count += 1
                    size_bytes = size_kb * 1024

                    print(f"\n--- Request {request_count}: server_cc={server_cc} + size={size_kb} KB ---")

                    # Coordinate with server: send cc+size, wait for 'ready'
                    coord_sock = send_request_to_server(server_ip, coord_port, server_cc, size_bytes)
                    if not coord_sock:
                        print(f"Failed to coordinate request {request_count}, skipping...")
                        continue

                    # Give the server time to start listening for this request
                    time.sleep(5)
                    request_start_time = datetime.now().strftime("%m-%d-%H-%M-%S")

                    success, duration, start_time = run_client(
                        server_ip=server_ip,
                        port=data_port,
                        size_bytes=size_bytes,
                        cc_algo=client_cc,
                        perf_log=f"{server_cc}_{size_bytes}_{request_count}_{request_start_time}_client.log"
                    )

                    # Wait for server completion notification
                    completion_success = wait_for_completion(coord_sock)
                    if not completion_success:
                        print("Warning: Server completion notification failed")

                    throughput_kbps = None
                    if success:
                        throughput_kbps = (size_kb * 8) / duration if duration > 0 else 0.0
                        print(f"Throughput: {throughput_kbps:.2f} Kbps")
                    else:
                        print(f"Failed to transfer {size_kb} KB (server_cc={server_cc})")

                    log_request_data(
                        log_filename,
                        start_time,
                        server_cc_algo=server_cc,
                        client_cc_algo=client_cc,
                        request_size_bytes=size_bytes,
                        duration=duration,
                        success=success,
                        throughput_kbps=throughput_kbps
                    )

                    print("Waiting 10 seconds before next request...")
                    time.sleep(10)

            print(f"Completed cycle {cycle_count} with all CC algorithms")
            time.sleep(2)  # Delay between cycles

    except KeyboardInterrupt:
        print(f"\nStopped after {cycle_count} cycles, {request_count} total requests")
        print(f"Request log saved to: {log_filename}")
        sys.exit(0)