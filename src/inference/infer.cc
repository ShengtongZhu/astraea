#include <getopt.h>
#include <signal.h>

#include <iostream>
#include <unordered_map>
#include <unordered_set>

#include <boost/asio.hpp>

#include "define.hh"
#include "server.hh"
#include "tf_inference.hh"
#include "udp_server.hh"
#include "unix_socket_server.hh"

void signal_handler(int sig) {
  std::cout << "Signal " << sig << " received" << std::endl;
  TFInference::Get()->stop();
  exit(0);
}

void usage_error(char** argv) {
  std::cerr << "Usage: " << argv[0] << " [-g|--graph] <graph-file> "
            << "[-c|--checkpoint] <checkpoint-path> [-b|--batch] BATCH_MODE\n";
  exit(1);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    usage_error(argv);
  }

  const option opts[] = {{"graph", required_argument, nullptr, 'g'},
                         {"checkpoint", required_argument, nullptr, 'c'},
                         {"batch", optional_argument, nullptr, 'b'},
                         {"channel", optional_argument, nullptr, 'h'},
                         {0, 0, nullptr, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "b:g:c:h:", opts, nullptr)) != -1) {
    switch (opt) {
    case 'b':
      batchMode = atoi(optarg);
      break;
    case 'g':
      graphPath = optarg;
      break;
    case 'c':
      checkpointPath = optarg;
      break;
    case 'h':
      channel = optarg;
      break;
    case '?':
      usage_error(argv);
      return 1;
    default:
      usage_error(argv);
      return 1;
    }
  }

  std::cout << "Graph path: " << graphPath << std::endl;
  std::cout << "Checkpoint path: " << checkpointPath << std::endl;
  if (batchMode) {
    std::cout << "Batch mode enabled" << std::endl;
  }
  std::cout << "Communication Channel: " << channel << std::endl;
  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);

  TFInference::Get();
  std::vector<float> input(50, 0);
  for (int i = 0; i < 100; ++i) {
    auto state = input;
    TFInference::Get()->inference_imdt(0, std::move(state), [](float, const std::string&) {});
  }
  // launch UDP server
  try {
    boost::asio::io_service io_service;
    if (channel == "udp") {
      UdpServer server(io_service);
      server.start();
      io_service.run();
    } else if (channel == "unix") {
      // launch unix socket server
      std::string socket_path = "/tmp/astraea.sock";
      ::unlink(socket_path.c_str());
      UnixSocketServer server(io_service, socket_path);
      server.start();
      io_service.run();
    } else {
      throw std::runtime_error("Unknown communication channel: " + channel);
    }
  } catch (std::exception& e) {
    std::cerr << e.what() << std::endl;
  }
  return 0;
}
