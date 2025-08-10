#include <getopt.h>
#include <signal.h>
#include <stdio.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "address.hh"
#include "pid.hh"
#include "child_process.hh"
#include "common.hh"
#include "deepcc_socket.hh"
#include "filesystem.hh"
#include "ipc_socket.hh"
#include "json.hpp"
#include "logging.hh"
#include "serialization.hh"
#include "socket.hh"
#include "system_runner.hh"
#include "tcp_info.hh"

#define BUFSIZ 1024
#define ALG "astraea"

using namespace std;
using clock_type = std::chrono::high_resolution_clock;
using json = nlohmann::json;
using IPC_ptr = std::unique_ptr<IPCSocket>;
typedef DeepCCSocket::TCPInfoRequestType RequestType;

enum MessageType { INIT = 0, START = 1, END = 2, ALIVE = 3, OBSERVE = 4 };

std::atomic<bool> send_traffic(true);
std::atomic<size_t> send_cnt = 0;
static size_t last_observed_send_cnt = 0;
std::unique_ptr<std::ofstream> perf_log;
std::unique_ptr<IPCSocket> ipc;
std::unique_ptr<ChildProcess> astraea_pyhelper;
static int global_flow_id = 0;

template <typename E>
constexpr typename std::underlying_type<E>::type to_underlying(E e) noexcept {
  return static_cast<typename std::underlying_type<E>::type>(e);
}

void ipc_send_message(std::unique_ptr<IPCSocket>& ipc_sock, const MessageType& type,
                      const json& state, const int observer_id = -1,
                      const int step = -1) {
  json message;
  message["state"] = state;
  message["flow_id"] = global_flow_id;
  if (type == MessageType::OBSERVE) {
    message["type"] = to_underlying(MessageType::OBSERVE);
    message["observer"] = observer_id;
    message["step"] = step;
  } else {
    message["type"] = to_underlying(type);
  }

  uint16_t len = message.dump().length();
  if (ipc_sock) {
    ipc_sock->write(put_field(len) + message.dump());
  }
}

void do_congestion_control(DeepCCSocket& sock, std::unique_ptr<IPCSocket>& ipc) {
  auto state = sock.get_tcp_deepcc_info_json(RequestType::REQUEST_ACTION);
  LOG(TRACE) << "Server " << global_flow_id << " send state: " << state.dump();
  ipc_send_message(ipc, MessageType::ALIVE, state);

  auto ts_now = clock_type::now();

  auto header = ipc->read_exactly(2);
  auto data_len = get_uint16(header.data());
  auto data = ipc->read_exactly(data_len);
  int cwnd = json::parse(data).at("cwnd");
  sock.set_tcp_cwnd(cwnd);

  auto elapsed = clock_type::now() - ts_now;
  LOG(DEBUG) << "Server GET cwnd: " << cwnd << ", elapsed time is "
             << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()
             << "us";

  if (perf_log) {
    unsigned int srtt = state["srtt_us"];
    srtt = srtt >> 3; // Convert to microseconds
    *perf_log << state["min_rtt"] << "\t" << state["avg_urtt"] << "\t"
              << state["cnt"] << "\t" << srtt << "\t" << state["avg_thr"]
              << "\t" << state["thr_cnt"] << "\t" << state["pacing_rate"]
              << "\t" << state["loss_bytes"] << "\t" << state["packets_out"]
              << "\t" << state["retrans_out"] << "\t"
              << state["max_packets_out"] << "\t" << state["cwnd"] << "\t"
              << cwnd << endl;
  }
}

void do_monitor(DeepCCSocket& sock) {
  while (send_traffic.load()) {
    auto state = sock.get_tcp_deepcc_info_json(RequestType::REQUEST_ACTION);
    if (perf_log) {
      unsigned int srtt = state["srtt_us"];
      srtt = srtt >> 3; // Convert to microseconds
      *perf_log << state["min_rtt"] << "\t" << state["avg_urtt"] << "\t"
                << state["cnt"] << "\t" << srtt << "\t" << state["avg_thr"]
                << "\t" << state["thr_cnt"] << "\t" << state["pacing_rate"]
                << "\t" << state["loss_bytes"] << "\t" << state["packets_out"]
                << "\t" << state["retrans_out"] << "\t"
                << state["max_packets_out"] << "\t" << state["cwnd"] << "\t"
                << 0 << endl;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
}

void control_thread(DeepCCSocket& sock, std::unique_ptr<IPCSocket>& ipc,
                   const std::chrono::milliseconds interval) {
  LOG(DEBUG) << "control_thread running";
  auto when_started = clock_type::now();
  auto target_time = when_started + interval;
  while (send_traffic.load()) {
    LOG(DEBUG) << "do do_congestion_control running";
    do_congestion_control(sock, ipc);
    std::this_thread::sleep_until(target_time);
    target_time += interval;
  }
}

void data_thread(TCPSocket& sock, const uint64_t requested_size) {
  string data(BUFSIZ, 'a');
  uint64_t sent_total = 0;
  while (send_traffic.load() && sent_total < requested_size) {
    size_t to_send = std::min<uint64_t>(data.size(), requested_size - sent_total);
    if (to_send == data.size()) {
      auto it = sock.write(data, true);
      sent_total += (it - data.begin());
    } else {
      string chunk = data.substr(0, to_send);
      auto it = sock.write(chunk, true);
      sent_total += (it - chunk.begin());
    }
  }
  LOG(INFO) << "Data thread exits after sending " << sent_total << " bytes";
  send_traffic = false;
}

void signal_handler(int sig) {
  if (sig == SIGINT or sig == SIGKILL or sig == SIGTERM) {
    send_traffic = false;
    if (perf_log) {
      perf_log->close();
    }
    if (astraea_pyhelper) {
      astraea_pyhelper->signal(SIGKILL);
    }
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    exit(1);
  }
}

void usage_error(const string& program_name) {
  cerr << "Usage: " << program_name << " [OPTION]... [COMMAND]" << endl;
  cerr << endl;
  cerr << "Options = --port=PORT --cong=ALGORITHM --interval=INTERVAL (Milliseconds) "
          "--pyhelper=PYTHON_PATH --model=MODEL_PATH --id=None --perf-log=PATH "
          "--perf-interval=MS --size=BYTES"
       << endl;
  cerr << endl;
  cerr << "Default congestion control algorithm is CUBIC; " << endl
       << "Default control interval is 20ms; " << endl
       << "Default flow id is None; " << endl
       << "pyhelper specifies the path of Python-inference script; " << endl
       << "model-path specifies the pre-trained model, and will be passed to "
          "python inference module; " << endl
       << "If perf_log is specified, the default log interval is 500ms" << endl
       << "size specifies total bytes to send to the client (required)" << endl;

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
      {"port", required_argument, nullptr, 'p'},
      {"pyhelper", required_argument, nullptr, 'h'},
      {"model", required_argument, nullptr, 'm'},
      {"cong", required_argument, nullptr, 'c'},
      {"interval", optional_argument, nullptr, 't'},
      {"id", optional_argument, nullptr, 'f'},
      {"perf-log", optional_argument, nullptr, 'l'},
      {"perf-interval", optional_argument, nullptr, 'i'},
      {"size", required_argument, nullptr, 's'},
      {0, 0, nullptr, 0}};

  bool use_RL = false;
  string service, pyhelper, model, cong_ctl, interval, id, perf_log_path, perf_interval;
  uint64_t requested_size = 0;
  while (true) {
    const int opt = getopt_long(argc, argv, "", command_line_options, nullptr);
    if (opt == -1) {
      break;
    }
    switch (opt) {
    case 'c':
      cong_ctl = optarg;
      break;
    case 'f':
      id = optarg;
      break;
    case 'h':
      pyhelper = optarg;
      break;
    case 'i':
      perf_interval = optarg;
      break;
    case 'l':
      perf_log_path = optarg;
      break;
    case 'm':
      model = optarg;
      break;
    case 'p':
      service = optarg;
      break;
    case 't':
      interval = optarg;
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

  if (not id.empty()) {
    global_flow_id = stoi(id);
    LOG(INFO) << "Flow id: " << global_flow_id;
  }

  std::chrono::milliseconds control_interval(20ms);
  if (cong_ctl == "astraea" and not(pyhelper.empty() or model.empty())) {
    if (not fs::exists(pyhelper)) {
      throw runtime_error("Pyhelper does not exist");
    }
    if (not fs::exists(model)) {
      throw runtime_error("Trained model does not exist");
    }
    string ipc_dir = "astraea_ipc";
    fs::create_directory(ipc_dir);
    string ipc_file = fs::path(ipc_dir) / ("astraea" + to_string(pid()));
    IPCSocket ipcsock;
    ipcsock.set_reuseaddr();
    ipcsock.bind(ipc_file);
    ipcsock.listen();

    fs::path ipc_path = fs::current_path() / ipc_file;

    LOG(INFO) << "Server: IPC listen at " << ipc_path;

    vector<string> prog_args{pyhelper, "--ipc-path", ipc_path, "--model-path", model};
    astraea_pyhelper = std::make_unique<ChildProcess>(
        pyhelper,
        [&pyhelper, &prog_args]() { return ezexec(pyhelper, prog_args); });

    if (not interval.empty()) {
      control_interval = std::move(std::chrono::milliseconds(stoi(interval)));
    }
    LOG(INFO) << "Server: started subprocess of Python helper";
    ipc = make_unique<IPCSocket>(ipcsock.accept());
    LOG(INFO) << "Server " << global_flow_id
              << " IPC with env has been established, control interval is "
              << control_interval.count() << "ms";
    use_RL = true;
  } else {
    LOG(INFO) << "Trained model must be specified, or " << ALG
              << " will be pure TCP with " << cong_ctl;
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
    if (not perf_interval.empty()) {
      log_interval = std::chrono::milliseconds(stoi(perf_interval));
    }
  }

  int port = stoi(service);
  Address address("0.0.0.0", port);
  DeepCCSocket server;
  server.set_reuseaddr();
  server.bind(address);
  server.listen();
  LOG(INFO) << "Server listen at " << port;

  DeepCCSocket client = server.accept();
  struct timeval timeout = {10, 0};
  setsockopt(client.fd_num(), SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
  setsockopt(client.fd_num(), SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
  client.set_congestion_control(cong_ctl);
  client.set_nodelay();
  LOG(DEBUG) << "Server " << global_flow_id << " set congestion control to " << cong_ctl;

  if (cong_ctl == "astraea") {
    int enable_deepcc = 2;
    client.enable_deepcc(enable_deepcc);
    LOG(DEBUG) << "Server " << global_flow_id << " enables deepCC plugin: " << enable_deepcc;
  }

  LOG(INFO) << "Will send " << requested_size << " bytes to client (no size handshake)";

  if (use_RL and perf_log) {
    *perf_log << "min_rtt\t"
              << "avg_urtt\t"
              << "cnt\t"
              << "srtt_us\t"
              << "avg_thr\t"
              << "thr_cnt\t"
              << "pacing_rate\t"
              << "loss_bytes\t"
              << "packets_out\t"
              << "retrans_out\t"
              << "max_packets_out\t"
              << "CWND in Kernel\t"
              << "CWND to Assign" << endl;
  } else if (perf_log) {
    *perf_log << "# Interval = " << log_interval.count() << "ms" << endl;
  }

  thread ct;
  thread log_thread;

  if (use_RL and ipc != nullptr) {
    ct = std::move(thread(control_thread, std::ref(client), std::ref(ipc), control_interval));
    LOG(DEBUG) << "Server " << global_flow_id << " Started control thread ... ";
  } else if (cong_ctl != "astraea" and perf_log != nullptr) {
    LOG(INFO) << "Launch monitor thread for " << cong_ctl << " ...";
    ct = thread(do_monitor, std::ref(client));
  }

  thread dt(data_thread, std::ref(client), requested_size);
  LOG(INFO) << "Server " << global_flow_id << " is sending data to client...";

  dt.join();
  if (ct.joinable()) {
    ct.join();
  }
  if (log_thread.joinable()) {
    log_thread.join();
  }
}