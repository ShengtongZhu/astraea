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
    // Read the requested size from client
    std::string size_request = sock.read_exactly(sizeof(uint64_t));
    if (size_request.length() != sizeof(uint64_t)) {
        std::cerr << "Failed to read size request from client" << std::endl;
        return;
    }
    
    uint64_t requested_size;
    std::memcpy(&requested_size, size_request.data(), sizeof(uint64_t));
    
    std::cout << "Client requested " << requested_size << " bytes" << std::endl;
    
    // Send exactly the requested amount of data
    uint64_t total_sent = 0;
    const std::string data(BUFFER, 'x');
    
    while (total_sent < requested_size && send_traffic) {
        uint64_t remaining = requested_size - total_sent;
        size_t to_send = std::min(static_cast<size_t>(remaining), data.length());
        
        std::string chunk = data.substr(0, to_send);
        auto result = sock.write(chunk);
        
        // Fix: Use result directly as it points to the end of written data
        size_t sent = result - chunk.begin();
        total_sent += sent;
        
        if (sent == 0) {
            break; // Connection closed
        }
    }
    
    std::cout << "Sent " << total_sent << " bytes total" << std::endl;
    send_traffic = false; // Signal other threads to stop
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