#include "udp_server.hh"

UdpServer::UdpServer(boost::asio::io_service& io_service)
    : Server(),
      socket_(io_service, boost::asio::ip::udp::endpoint(
                              boost::asio::ip::udp::v4(), PORT)) {}

void UdpServer::start() {
  // std::cout << "Server started" << std::endl;
  socket_.async_receive_from(
      boost::asio::buffer(recv_buffer_), remote_endpoint_,
      boost::bind(&UdpServer::handle_receive, this,
                  boost::asio::placeholders::error,
                  boost::asio::placeholders::bytes_transferred()));
}

void UdpServer::handle_flow_init(int& flow_id,
                                 ResponseCallback&& send_response) {
  std::string response;
  if (flow_contexts.find(flow_id) != flow_contexts.end()) {
    // generate a random one if already exists
    flow_id = rand();
    // std::cout << "Flow " << flow_id
    //           << " already exists, generate a new one: " << flow_id
    //           << std::endl;
  }
  flow_contexts[flow_id] = new FlowContext(flow_id);
  json reply;
  reply["flow_id"] = flow_id;
  response = reply.dump();
  send_response(-1, response);
}

void UdpServer::handle_congestion_control(int flow_id, json& data,
                                          ResponseCallback&& send_response) {
  if (unlikely(flow_contexts.find(flow_id) == flow_contexts.end())) {
    std::cerr << "Flow " << flow_id << " does not exist" << std::endl;
    return;
  }
  auto context = flow_contexts[flow_id];
  auto state = context->format_state(data["state"]);
  if (!batchMode) {
    TFInference::Get()->inference_imdt(flow_id, std::move(state),
                                       std::move(send_response));
  } else {
    TFInference::Get()->submit_inference_request(flow_id, std::move(state),
                                                 std::move(send_response));
  }
}

void UdpServer::handle_receive(const boost::system::error_code& error,
                               std::size_t bytes_transferred) {
  if (!error) {
    std::string info;
    std::string response;
    info.resize(2);
    // read the first two bytes which indicates the message length
    std::memcpy(info.data(), recv_buffer_.data(), 2);
    auto length = get_uint16(info.data());
    // check if the message is complete
    if (length != bytes_transferred - 2) {
      std::cout << "Incomplete message received" << std::endl;
      return;
    }
    info.resize(length);
    // read the message
    std::memcpy(info.data(), recv_buffer_.data() + 2, length);
    json data = json::parse(info);
#ifdef DEBUG
    std::cout << "Received message: " << std::endl;
    std::cout << data.dump(4) << std::endl;
#endif
    MessageType type = data.at("type");
    int flow_id = data.at("flow_id");
    ResponseCallback send_response =
        std::bind(&UdpServer::send_response, this, remote_endpoint_, data,
                  std::placeholders::_1, std::placeholders::_2);
    switch (type) {
    case MessageType::START: {
      std::cout << "Register flow " << flow_id << std::endl;
      handle_flow_init(flow_id, std::move(send_response));
      break;
    }
    case MessageType::ALIVE: {
      handle_congestion_control(flow_id, data, std::move(send_response));
      break;
    }
    case MessageType::END: {
      handle_flow_removal(flow_id);
      break;
    }
    default:
      break;
    }
  }
  start();
}

void UdpServer::send_response(boost::asio::ip::udp::endpoint remote_endpoint,
                              const json data, float action,
                              const std::string& info) {
  std::string response;
  if (info != "") {
    response = put_field(info.length()) + info;
  } else {
    int cwnd = data["state"]["cwnd"];
    auto new_cwnd = map_action(action, cwnd);
    json reply;
    reply["cwnd"] = new_cwnd;
    reply["flow_id"] = data["flow_id"];
    response = put_field(reply.dump().length()) + reply.dump();
  }
#ifdef DEBUG
  std::cout << "Original cwnd: " << cwnd << ", action: " << action
            << ", new cwnd: " << new_cwnd << std::endl;
  std::cout << "Flow " << data["flow_id"] << "Action: " << reply.dump(4)
            << std::endl;
  std::cout << "Sending response: " << std::endl;
  std::cout << response << std::endl;
#endif
  auto len = socket_.send_to(boost::asio::buffer(response), remote_endpoint);
  if (unlikely(len != response.length())) {
    std::cerr << "UDP Send Error: " << len << " bytes sent, "
              << response.length() << " bytes expected" << std::endl;
  }
}

void UdpServer::handle_send(const boost::system::error_code& error,
                            std::size_t bytes_transferred) {
  if (error) {
    std::cerr << "UDP Send Error: " << error.message() << std::endl;
  }
}