#include <getopt.h>
#include <signal.h>
#include <stdio.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <cstring>

#include "address.hh"
#include "common.hh"
#include "logging.hh"
#include "socket.hh"

#define BUFFER 1024

using namespace std;
using clock_type = std::chrono::high_resolution_clock;

std::atomic<bool> recv_traffic(true);
std::atomic<size_t> recv_cnt = 0;
static size_t last_observed_recv_cnt = 0;
std::unique_ptr<std::ofstream> perf_log;

void signal_handler(int sig) {
  if (sig == SIGINT or sig == SIGKILL or sig == SIGTERM) {
    recv_traffic = false;
    if (perf_log) {
      perf_log->close();
    }
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    exit(1);
  }
}

void data_thread(TCPSocket& sock, const uint64_t expected_bytes) {
  size_t bytes_received = 0;
  while (recv_traffic.load() and bytes_received < expected_bytes) {
    try {
      size_t to_read = std::min<size_t>(BUFFER, expected_bytes - bytes_received);
      size_t got = sock.read(to_read).length();
      if (got > 0) {
        recv_cnt += got;
        bytes_received += got;
      } else {
        LOG(INFO) << "Server closed connection";
        break;
      }
    } catch (const exception& e) {
      LOG(ERROR) << "Error receiving data: " << e.what();
      break;
    }
  }
  recv_traffic = false;
  LOG(INFO) << "Data thread exits after receiving " << bytes_received << " bytes";
}

void perf_log_thread(const std::chrono::milliseconds interval) {
  auto when_started = clock_type::now();
  auto target_time = when_started + interval;
  size_t tmp = 0;
  while (recv_traffic.load()) {
    tmp = recv_cnt;
    unsigned long long current_thr =
        (tmp - last_observed_recv_cnt) * 8 / interval.count() * 1000 / 1000000;
    last_observed_recv_cnt = tmp;
    if (perf_log) {
      *perf_log << current_thr << endl;
    }
    std::this_thread::sleep_until(target_time);
    target_time += interval;
  }
}

void usage_error(const string& program_name) {
  cerr << "Usage: " << program_name << " [OPTION]... [COMMAND]" << endl;
  cerr << endl;
  cerr << "Options = --ip=IP_ADDR --port=PORT --cong=ALGORITHM (default: CUBIC) "
          "--size=BYTES --perf-log=PATH(default is None) --perf-interval=MS"
       << endl
       << "If perf_log is specified, the default log interval is 500ms" << endl;
  cerr << endl;

  throw runtime_error("invalid arguments");
}

int main(int argc, char** argv) {
  signal(SIGTERM, signal_handler);
  signal(SIGKILL, signal_handler);
  signal(SIGINT, signal_handler);
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    throw runtime_error("signal: failed to ignore SIGPIPE");
  }

  if (argc < 1) {
    usage_error(argv[0]);
  }
  const option command_line_options[] = {
      {"ip", required_argument, nullptr, 'a'},
      {"port", required_argument, nullptr, 'p'},
      {"cong", optional_argument, nullptr, 'c'},
      {"size", required_argument, nullptr, 's'},
      {"perf-log", optional_argument, nullptr, 'l'},
      {"perf-interval", optional_argument, nullptr, 'i'},
      {0, 0, nullptr, 0}};

  string ip, service, cong_ctl, perf_log_path, interval;
  uint64_t requested_size = 0;
  while (true) {
    const int opt = getopt_long(argc, argv, "", command_line_options, nullptr);
    if (opt == -1) {
      break;
    }
    switch (opt) {
    case 'a':
      ip = optarg;
      break;
    case 'c':
      cong_ctl = optarg;
      break;
    case 'i':
      interval = optarg;
      break;
    case 'l':
      perf_log_path = optarg;
      break;
    case 'p':
      service = optarg;
      break;
    case 's':
      requested_size = std::stoull(optarg);
      break;
    case '?':
      usage_error(argv[0]);
      break;
    default:
      throw runtime_error("getopt_long: unexpected return value " + to_string(opt));
    }
  }

  if (optind > argc) {
    usage_error(argv[0]);
  }

  if (requested_size == 0) {
    cerr << "Error: --size must be provided and > 0" << endl;
    usage_error(argv[0]);
  }

  if (cong_ctl.empty()) {
    cong_ctl = "cubic";
  }

  std::chrono::milliseconds log_interval(500ms);
  if (not perf_log_path.empty()) {
    perf_log.reset(new std::ofstream(perf_log_path));
    if (not perf_log->good()) {
      throw runtime_error(perf_log_path + ": error opening for writing");
    }
    if (not interval.empty()) {
      log_interval = std::chrono::milliseconds(stoi(interval));
    }
  }

  int port = stoi(service);
  Address address(ip, port);
  TCPSocket client;
  client.set_reuseaddr();
  client.connect(address);

  struct timeval timeout = {10, 0};
  setsockopt(client.fd_num(), SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
  setsockopt(client.fd_num(), SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
  client.set_congestion_control(cong_ctl);
  client.set_nodelay();
  LOG(DEBUG) << "Client congestion control algorithm: " << cong_ctl;

  LOG(INFO) << "Expecting " << requested_size << " bytes from server";

  thread log_thread;
  if (perf_log) {
    cerr << "Client start with perf logger" << endl;
    log_thread = std::move(std::thread(perf_log_thread, log_interval));
    *perf_log << "# Interval = " << log_interval.count() << "ms" << endl;
  }

  thread dt(data_thread, std::ref(client), requested_size);
  LOG(INFO) << "Client is receiving data from server...";

  dt.join();
  if (log_thread.joinable()) {
    log_thread.join();
  }
}