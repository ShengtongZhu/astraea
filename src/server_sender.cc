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
#include "child_process.hh"
#include "common.hh"
#include "deepcc_socket.hh"
#include "ipc_socket.hh"
#include "logging.hh"
#include "socket.hh"
#include "system_runner.hh"  // Add this line

#define BUFSIZ 8192
#define ALG "astraea"

using namespace std;
using clock_type = std::chrono::high_resolution_clock;

enum MessageType { INIT = 0, START = 1, END = 2, ALIVE = 3, OBSERVE = 4 };

std::atomic<bool> send_traffic(true);
std::atomic<size_t> send_cnt = 0;
static size_t last_observed_send_cnt = 0;
std::unique_ptr<std::ofstream> perf_log;
std::unique_ptr<IPCSocket> ipc;
std::unique_ptr<ChildProcess> astraea_pyhelper;
static int global_flow_id = 0;

void ipc_send_message(IPCSocket& ipc, const string& message) {
  try {
    ipc.write(message, true);
  } catch (const exception& e) {
    LOG(ERROR) << "IPC send error: " << e.what();
  }
}

void signal_handler(int sig) {
  if (sig == SIGINT or sig == SIGKILL or sig == SIGTERM) {
    send_traffic = false;
    if (ipc != nullptr) {
      ipc_send_message(*ipc, "{\"msg_type\": " + to_string(END) + "}");
    }
    if (perf_log) {
      perf_log->close();
    }
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    exit(1);
  }
}

void do_congestion_control(DeepCCSocket& sock, std::unique_ptr<IPCSocket>& ipc) {
  // get TCP info
  TCPInfo tcpinfo = sock.get_tcp_info();
  string msg = "{\"msg_type\": " + to_string(OBSERVE) +
               ", \"cwnd\": " + to_string(tcpinfo.tcpi_snd_cwnd) +
               ", \"min_rtt\": " + to_string(tcpinfo.tcpi_min_rtt) +
               ", \"srtt\": " + to_string(tcpinfo.tcpi_rtt) +
               ", \"throughput\": " + to_string(sock.get_throughput()) +
               ", \"packets_out\": " + to_string(tcpinfo.tcpi_packets_out) +
               ", \"retrans_out\": " + to_string(tcpinfo.tcpi_retrans_out) +
               ", \"lost_out\": " + to_string(tcpinfo.tcpi_lost) +
               ", \"flow_id\": " + to_string(global_flow_id) + "}";

  ipc_send_message(*ipc, msg);

  // receive action from Python helper
  try {
    string response = ipc->read();
    if (not response.empty()) {
      // parse JSON response and apply congestion window
      size_t cwnd_pos = response.find("\"cwnd\":");
      if (cwnd_pos != string::npos) {
        size_t value_start = response.find(":", cwnd_pos) + 1;
        size_t value_end = response.find_first_of(",}", value_start);
        if (value_start != string::npos && value_end != string::npos) {
          string cwnd_str = response.substr(value_start, value_end - value_start);
          int new_cwnd = stoi(cwnd_str);
          sock.set_cwnd(new_cwnd);
        }
      }
    }
  } catch (const exception& e) {
    LOG(ERROR) << "Error receiving from IPC: " << e.what();
  }

  // log performance data
  if (perf_log) {
    *perf_log << tcpinfo.tcpi_min_rtt << "\t"
              << tcpinfo.tcpi_rtt << "\t"
              << send_cnt << "\t"
              << tcpinfo.tcpi_rtt << "\t"
              << sock.get_throughput() << "\t"
              << send_cnt << "\t"
              << sock.get_pacing_rate() << "\t"
              << tcpinfo.tcpi_bytes_retrans << "\t"
              << tcpinfo.tcpi_packets_out << "\t"
              << tcpinfo.tcpi_retrans_out << "\t"
              << tcpinfo.tcpi_snd_cwnd << "\t"
              << tcpinfo.tcpi_snd_cwnd << "\t"
              << tcpinfo.tcpi_snd_cwnd << endl;
  }
}

void do_monitor(DeepCCSocket& sock) {
  while (send_traffic.load()) {
    TCPInfo tcpinfo = sock.get_tcp_info();
    if (perf_log) {
      *perf_log << tcpinfo.tcpi_min_rtt << "\t"
                << tcpinfo.tcpi_rtt << "\t"
                << send_cnt << "\t"
                << tcpinfo.tcpi_rtt << "\t"
                << sock.get_throughput() << "\t"
                << send_cnt << "\t"
                << sock.get_pacing_rate() << "\t"
                << tcpinfo.tcpi_bytes_retrans << "\t"
                << tcpinfo.tcpi_packets_out << "\t"
                << tcpinfo.tcpi_retrans_out << "\t"
                << tcpinfo.tcpi_snd_cwnd << "\t"
                << tcpinfo.tcpi_snd_cwnd << "\t"
                << tcpinfo.tcpi_snd_cwnd << endl;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void control_thread(DeepCCSocket& sock, std::unique_ptr<IPCSocket>& ipc,
                   const std::chrono::milliseconds interval) {
  // send INIT message
  ipc_send_message(*ipc, "{\"msg_type\": " + to_string(INIT) + "}");
  // send START message
  ipc_send_message(*ipc, "{\"msg_type\": " + to_string(START) + "}");

  // start regular congestion control pattern
  auto when_started = clock_type::now();
  auto target_time = when_started + interval;
  while (send_traffic.load()) {
    do_congestion_control(sock, ipc);
    std::this_thread::sleep_until(target_time);
    target_time += interval;
  }
}

void data_thread(TCPSocket& sock) {
  string data(BUFSIZ, 'a');
  while (send_traffic.load()) {
    try {
      size_t bytes_sent = sock.write(data, true);
      send_cnt += bytes_sent;
    } catch (const exception& e) {
      LOG(ERROR) << "Error sending data: " << e.what();
      break;
    }
  }
  LOG(INFO) << "Data thread exits";
}

void perf_log_thread(const std::chrono::milliseconds interval) {
  auto when_started = clock_type::now();
  auto target_time = when_started + interval;
  size_t tmp = 0;
  while (send_traffic.load()) {
    // log the current throughput in Mbps
    tmp = send_cnt;
    unsigned long long current_thr =
        (tmp - last_observed_send_cnt) * 8 / interval.count() * 1000 / 1000000;
    last_observed_send_cnt = tmp;
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
  cerr << "Options = --port=PORT --cong=ALGORITHM --interval=INTERVAL (Milliseconds) "
          "--pyhelper=PYTHON_PATH --model=MODEL_PATH --id=None --perf-log=PATH "
          "--perf-interval=MS"
       << endl;
  cerr << endl;
  cerr << "Default congestion control algorithm is CUBIC; " << endl
       << "Default control interval is 20ms; " << endl
       << "Default flow id is None; " << endl
       << "pyhelper specifies the path of Python-inference script; " << endl
       << "model-path specifies the pre-trained model, and will be passed to "
          "python inference module; " << endl
       << "If perf_log is specified, the default log interval is 500ms" << endl;

  throw runtime_error("invalid arguments");
}

int main(int argc, char** argv) {
  /* register signal handler */
  signal(SIGTERM, signal_handler);
  signal(SIGKILL, signal_handler);
  signal(SIGINT, signal_handler);
  /* ignore SIGPIPE generated by Socket write */
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
      {"cong", optional_argument, nullptr, 'c'},
      {"interval", optional_argument, nullptr, 't'},
      {"id", optional_argument, nullptr, 'f'},
      {"perf-log", optional_argument, nullptr, 'l'},
      {"perf-interval", optional_argument, nullptr, 'i'},
      {0, 0, nullptr, 0}};

  /* use RL inference or not */
  bool use_RL = false;
  string service, pyhelper, model, cong_ctl, interval, id, perf_log_path, perf_interval;
  while (true) {
    const int opt = getopt_long(argc, argv, "", command_line_options, nullptr);
    if (opt == -1) { /* end of options */
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
    case '?':
      usage_error(argv[0]);
      break;
    default:
      throw runtime_error("getopt_long: unexpected return value " +
                          to_string(opt));
    }
  }

  if (optind > argc) {
    usage_error(argv[0]);
  }

  /* assign flow_id */
  if (not id.empty()) {
    global_flow_id = stoi(id);
    LOG(INFO) << "Flow id: " << global_flow_id;
  }

  std::chrono::milliseconds control_interval(20ms);
  if (cong_ctl == "astraea" and not(pyhelper.empty() or model.empty())) {
    // first check pyhelper and model
    if (not fs::exists(pyhelper)) {
      throw runtime_error("Pyhelper does not exist");
    }
    if (not fs::exists(model)) {
      throw runtime_error("Trained model does not exist");
    }
    /* IPC and control interval */
    string ipc_dir = "astraea_ipc";
    // return true if created or dir exists
    fs::create_directory(ipc_dir);
    string ipc_file = fs::path(ipc_dir) / ("astraea" + to_string(pid()));
    IPCSocket ipcsock;
    ipcsock.set_reuseaddr();
    ipcsock.bind(ipc_file);
    ipcsock.listen();

    fs::path ipc_path = fs::current_path() / ipc_file;

    LOG(INFO) << "Server: IPC listen at " << ipc_path;

    // start child process of Python helper for inference
    vector<string> prog_args{pyhelper, "--ipc-path", ipc_path, "--model-path",
                             model};
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
    /* has checked all things, we can use RL */
    use_RL = true;
  } else {
    LOG(INFO) << "Trained model must be specified, or " << ALG
                 << " will be pure TCP with " << cong_ctl;
  }

  /* default CC is cubic */
  if (cong_ctl.empty()) {
    cong_ctl = "cubic";
  }

  // init perf log file
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
  // init server addr
  Address address("0.0.0.0", port);
  DeepCCSocket server;
  /* set reuse_addr */
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
  LOG(DEBUG) << "Server " << global_flow_id << " set congestion control to "
             << cong_ctl;
  
  /* !! should be set after socket connected */
  if (cong_ctl == "astraea") {
    int enable_deepcc = 2;
    client.enable_deepcc(enable_deepcc);
    LOG(DEBUG) << "Server " << global_flow_id << " "
               << "enables deepCC plugin: " << enable_deepcc;
  }

  /* setup performance log header for Astraea */
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

  // start threads
  thread ct;
  thread log_thread;
  
  if (use_RL and ipc != nullptr) {
    ct = std::move(thread(control_thread, std::ref(client), std::ref(ipc),
                          control_interval));
    LOG(DEBUG) << "Server " << global_flow_id << " Started control thread ... ";
  } else if (cong_ctl != "astraea" and perf_log != nullptr) {
    // launch monitor thread for non-Astraea algorithms
    LOG(INFO) << "Launch monitor thread for " << cong_ctl << " ...";
    ct = thread(do_monitor, std::ref(client));
  }
  
  if (perf_log and not use_RL) {
    cerr << "Server start with perf logger" << endl;
    log_thread = std::move(std::thread(perf_log_thread, log_interval));
  }

  // start data sending thread
  thread dt(data_thread, std::ref(client));
  LOG(INFO) << "Server " << global_flow_id << " is sending data to client...";

  /* wait for finish */
  dt.join();
  if (ct.joinable()) {
    ct.join();
  }
  if (log_thread.joinable()) {
    log_thread.join();
  }
}