#!/usr/bin/env python3
import subprocess
import time
import signal
import sys

class NewServerManager:
    def __init__(self):
        self.process = None
    
    def start_server(self, port=8888, cc_algo="cubic", perf_log=None):
        """Start the new server sender process"""
        cmd = ["./src/build/bin/new_server_sender", "--port", str(port), "--cong", cc_algo, "--pyhelper", "./python/infer.py", "--model", "./models/py-model1/"]
        
        if perf_log:
            cmd.extend(["--perf-log", perf_log])
        
        print(f"Starting server with command: {' '.join(cmd)}")
        self.process = subprocess.Popen(cmd)
        
        # Give server time to start
        time.sleep(1)
        return self.process
    
    def stop_server(self):
        """Stop the server process"""
        if self.process:
            print("Stopping server...")
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                print("Force killing server...")
                self.process.kill()
                self.process.wait()
            self.process = None
    
    def is_running(self):
        """Check if server is still running"""
        return self.process is not None and self.process.poll() is None

def signal_handler(sig, frame):
    print("\nReceived interrupt signal")
    sys.exit(0)

if __name__ == "__main__":
    signal.signal(signal.SIGINT, signal_handler)
    
    server = NewServerManager()
    
    try:
        # Start server
        server.start_server(port=8888, cc_algo="astraea", perf_log="server_perf.log")
        
        print("Server started. Press Ctrl+C to stop.")
        
        # Keep server running
        while server.is_running():
            time.sleep(1)
            
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        server.stop_server()