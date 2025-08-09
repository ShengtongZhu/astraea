#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <cstring>

#include "deepcc_socket.hh"
#include "filesystem.hh"

std::atomic<bool> recv_traffic{true};
std::atomic<uint64_t> recv_cnt{0};
std::atomic<uint64_t> expected_bytes{0};

std::ofstream perf_log;

void data_thread(DeepCCSocket& sock) {
    const size_t chunk_size = 1024;
    uint64_t bytes_received = 0;
    
    while (recv_traffic && bytes_received < expected_bytes) {
        size_t to_receive = std::min(chunk_size, static_cast<size_t>(expected_bytes - bytes_received));
        std::string data = sock.read(to_receive);
        if (data.empty()) {
            break;
        }
        bytes_received += data.length();
        recv_cnt += data.length();
    }
    
    std::cout << "Received " << bytes_received << " bytes from server" << std::endl;
    recv_traffic = false;
}

void perf_log_thread() {
    auto start_time = std::chrono::steady_clock::now();
    uint64_t last_recv_cnt = 0;
    
    while (recv_traffic) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count();
        uint64_t current_recv_cnt = recv_cnt.load();
        
        if (perf_log.is_open()) {
            perf_log << elapsed << "," << current_recv_cnt << std::endl;
        }
        
        last_recv_cnt = current_recv_cnt;
    }
}

void usage_error(const std::string& program_name) {
    std::cerr << "Usage: " << program_name << " [options]" << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --ip IP               Server IP address (default: 127.0.0.1)" << std::endl;
    std::cerr << "  --port PORT           Server port (default: 8888)" << std::endl;
    std::cerr << "  --size BYTES          Number of bytes to request" << std::endl;
    std::cerr << "  --cc CC_ALGO          Congestion control algorithm" << std::endl;
    std::cerr << "  --perf-log FILE       Performance log file" << std::endl;
    exit(1);
}

int main(int argc, char* argv[]) {
    std::string server_ip = "127.0.0.1";
    std::string port = "8888";
    std::string cc_algo = "cubic";
    std::string perf_log_file;
    uint64_t requested_size = 0;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--ip" && i + 1 < argc) {
            server_ip = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = argv[++i];
        } else if (arg == "--size" && i + 1 < argc) {
            requested_size = std::stoull(argv[++i]);
        } else if (arg == "--cc" && i + 1 < argc) {
            cc_algo = argv[++i];
        } else if (arg == "--perf-log" && i + 1 < argc) {
            perf_log_file = argv[++i];
        } else {
            usage_error(argv[0]);
        }
    }
    
    if (requested_size == 0) {
        std::cerr << "Error: --size parameter is required" << std::endl;
        usage_error(argv[0]);
    }
    
    expected_bytes = requested_size;
    
    // Open performance log if specified
    if (!perf_log_file.empty()) {
        perf_log.open(perf_log_file);
        if (!perf_log.is_open()) {
            std::cerr << "Failed to open performance log file: " << perf_log_file << std::endl;
            return 1;
        }
    }
    
    try {
        // Connect to server
        DeepCCSocket client_sock;
        client_sock.connect({server_ip, port});
        
        std::cout << "Connected to server " << server_ip << ":" << port << std::endl;
        
        // Set congestion control
        client_sock.set_congestion_control(cc_algo);
        
        // Send size request to server
        std::string size_data(sizeof(requested_size), '\0');
        std::memcpy(&size_data[0], &requested_size, sizeof(requested_size));
        
        auto result = client_sock.write(size_data);
        // Fix: Calculate sent bytes correctly
        size_t sent_bytes = result - size_data.begin();
        if (sent_bytes != sizeof(requested_size)) {
            std::cerr << "Failed to send size request to server" << std::endl;
            return 1;
        }
        
        std::cout << "Requested " << requested_size << " bytes from server" << std::endl;
        
        // Start performance logging thread
        std::thread perf_thread;
        if (perf_log.is_open()) {
            perf_thread = std::thread(perf_log_thread);
        }
        
        // Start data thread
        std::thread data_th(data_thread, std::ref(client_sock));
        
        // Wait for data thread to complete
        data_th.join();
        
        // Clean up
        if (perf_thread.joinable()) {
            perf_thread.join();
        }
        
        if (perf_log.is_open()) {
            perf_log.close();
        }
        
        std::cout << "Client finished" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}