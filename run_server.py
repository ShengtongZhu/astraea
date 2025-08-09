#!/usr/bin/env python3
import subprocess
import sys
import time
import signal
import os

class ServerRunner:
    def __init__(self):
        self.server_process = None
        self.base_dir = os.path.dirname(os.path.abspath(__file__))
        
    def start_server(self, port=12345, cong="astraea", perf_log="server_perf.log"):
        """Start the server with specified parameters"""
        cmd = [
            f"{self.base_dir}/src/build/bin/server_sender",
            f"--port={port}",
            f"--cong={cong}",
            f"--pyhelper={self.base_dir}/python/infer.py",
            f"--model={self.base_dir}/models/py-model1",
            f"--perf-log={perf_log}"
        ]
        
        print(f"Starting server with command: {' '.join(cmd)}")
        try:
            self.server_process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            print(f"Server started with PID: {self.server_process.pid}")
            return True
        except Exception as e:
            print(f"Failed to start server: {e}")
            return False
    
    def stop_server(self):
        """Stop the server process"""
        if self.server_process and self.server_process.poll() is None:
            print("Stopping server...")
            self.server_process.terminate()
            try:
                self.server_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                print("Server didn't stop gracefully, killing...")
                self.server_process.kill()
                self.server_process.wait()
            print("Server stopped")
    
    def wait_for_client(self, timeout=60):
        """Wait for client to connect and finish"""
        print(f"Waiting for client connection (timeout: {timeout}s)...")
        try:
            stdout, stderr = self.server_process.communicate(timeout=timeout)
            if stdout:
                print("Server output:", stdout)
            if stderr:
                print("Server errors:", stderr)
            return self.server_process.returncode
        except subprocess.TimeoutExpired:
            print("Timeout waiting for client")
            self.stop_server()
            return -1

def main():
    if len(sys.argv) < 2:
        print("Usage: python run_server.py <port> [timeout_seconds]")
        print("Example: python run_server.py 12345 60")
        sys.exit(1)
    
    port = int(sys.argv[1])
    timeout = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    
    server = ServerRunner()
    
    def signal_handler(signum, frame):
        print("\nReceived signal, stopping server...")
        server.stop_server()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    try:
        if server.start_server(port=port):
            # Wait for client to connect and finish
            return_code = server.wait_for_client(timeout=timeout)
            print(f"Server finished with return code: {return_code}")
        else:
            print("Failed to start server")
            sys.exit(1)
    finally:
        server.stop_server()

if __name__ == "__main__":
    main()