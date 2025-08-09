#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "deepcc_socket.hh"
#include "ipc_socket.hh"
#include "json.hpp"
#include "serialization.hh"
#include "system_runner.hh"
#include "tcp_info.hh"
#include "filesystem.hh"

using json = nlohmann::json;
using IPC_ptr = std::unique_ptr<IPCSocket>;

std::atomic<bool> send_traffic{true};
std::atomic<uint64_t> send_cnt{0};
std::atomic<uint64_t> target_bytes{0};
std::atomic<bool> size_received{false};

std::ofstream perf_log;

enum class RequestType {
    INFERENCE_REQUEST = 0,
    MONITOR_REQUEST = 1
};

void ipc_send_message(IPC_ptr& ipc, const json& message) {
    std::string serialized = message.dump();
    ipc->write(serialized, true);
}

void data_thread(DeepCCSocket& sock) {
    // First, read the size request from client
    uint64_t requested_bytes;
    std::string size_data = sock.read_exactly(sizeof(requested_bytes));
    if (size_data.length() != sizeof(requested_bytes)) {
        std::cerr << "Failed to read size request from client" << std::endl;
        send_traffic = false;
        return;
    }
    
    // Extract the requested bytes from the string
    std::memcpy(&requested_bytes, size_data.data(), sizeof(requested_bytes));
    target_bytes = requested_bytes;
    size_received = true;
    std::cout << "Received request for " << requested_bytes << " bytes" << std::endl;
    
    // Send exactly the requested amount of data
    const size_t chunk_size = 1024;
    std::string data(chunk_size, 'A');
    uint64_t bytes_sent = 0;
    
    while (send_traffic && bytes_sent < target_bytes) {
        size_t to_send = std::min(chunk_size, static_cast<size_t>(target_bytes - bytes_sent));
        if (to_send < chunk_size) {
            data.resize(to_send);
        }
        auto result = sock.write(data);
        size_t sent = std::distance(data.begin(), result);
        if (sent == 0) {
            break;
        }
        bytes_sent += sent;
        send_cnt += sent;
    }
    
    std::cout << "Sent " << bytes_sent << " bytes to client" << std::endl;
    send_traffic = false;
}

void perf_log_thread() {
    auto start_time = std::chrono::steady_clock::now();
    uint64_t last_send_cnt = 0;
    
    while (send_traffic) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count();
        uint64_t current_send_cnt = send_cnt.load();
        
        if (perf_log.is_open()) {
            perf_log << elapsed << "," << current_send_cnt << std::endl;
        }
        
        last_send_cnt = current_send_cnt;
    }
}

void usage_error(const std::string& program_name) {
    std::cerr << "Usage: " << program_name << " [options]" << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --port PORT           Port to listen on (default: 8888)" << std::endl;
    std::cerr << "  --cc CC_ALGO          Congestion control algorithm" << std::endl;
    std::cerr << "  --perf-log FILE       Performance log file" << std::endl;
    exit(1);
}

int main(int argc, char* argv[]) {
    std::string port = "8888";
    std::string cc_algo = "cubic";
    std::string perf_log_file;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = argv[++i];
        } else if (arg == "--cc" && i + 1 < argc) {
            cc_algo = argv[++i];
        } else if (arg == "--perf-log" && i + 1 < argc) {
            perf_log_file = argv[++i];
        } else {
            usage_error(argv[0]);
        }
    }
    
    // Open performance log if specified
    if (!perf_log_file.empty()) {
        perf_log.open(perf_log_file);
        if (!perf_log.is_open()) {
            std::cerr << "Failed to open performance log file: " << perf_log_file << std::endl;
            return 1;
        }
    }
    
    try {
        // Create and bind socket
        DeepCCSocket server_sock;
        server_sock.bind({"0.0.0.0", port});
        server_sock.listen();
        
        std::cout << "Server listening on port " << port << std::endl;
        
        // Accept client connection
        auto client_sock = server_sock.accept();
        std::cout << "Client connected" << std::endl;
        
        // Set congestion control
        client_sock.set_congestion_control(cc_algo);
        
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
        
        std::cout << "Server finished" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}