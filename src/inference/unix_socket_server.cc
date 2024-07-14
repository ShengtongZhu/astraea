#include "unix_socket_server.hh"
#include "serialization.hh"

UnixSocketServer::UnixSocketServer(boost::asio::io_service& io_service,
                                   const std::string& socket_path)
    : io_service_(io_service),
      acceptor_(io_service,
                boost::asio::local::stream_protocol::endpoint(socket_path)) {
  start();
}

void UnixSocketServer::start() {
  std::shared_ptr<Session> new_session = std::make_shared<Session>(io_service_);
  new_session->set_udp_server(this);
  acceptor_.async_accept(
      new_session->socket(),
      boost::bind(&UnixSocketServer::handle_accept, this, new_session,
                  boost::asio::placeholders::error));
}

void UnixSocketServer::handle_accept(std::shared_ptr<Session> new_session,
                                     const boost::system::error_code& error) {
  if (!error) {
    new_session->start();
    start();
  } else {
    std::cerr << "Accept error: " << error.message() << std::endl;
  }
}

Session::Session(boost::asio::io_service& io_service) : socket_(io_service) {}

boost::asio::local::stream_protocol::socket& Session::socket() {
  return socket_;
}

void Session::start() {
  boost::asio::async_read(
      socket_,
      boost::asio::buffer(message_length_buffer_.data(),
                          sizeof(message_length_)),
      boost::bind(&Session::handle_read_length, shared_from_this(),
                  boost::asio::placeholders::error));
}

void Session::handle_read_length(const boost::system::error_code& error) {
  message_length_ = get_uint16(message_length_buffer_.data());
  if (!error) {
    boost::asio::async_read(
        socket_, boost::asio::buffer(recv_buffer_.data(), message_length_),
        boost::bind(&Session::handle_read_message, shared_from_this(),
                    boost::asio::placeholders::error, message_length_));
  } else {
    std::cerr << "Error reading message length: " << error.message()
              << std::endl;
  }
}

void Session::handle_read_message(const boost::system::error_code& error,
                                  std::size_t expected_length) {
  bool stop = false;
  if (!error) {
    std::string message(recv_buffer_.data(), expected_length);
    // std::cout << "Received message: " << message << std::endl;
    std::string response;
    json data = json::parse(message);
#ifdef DEBUG
    std::cout << "Received message: " << std::endl;
    std::cout << data.dump(4) << std::endl;
#endif
    MessageType type = data.at("type");
    int flow_id = data.at("flow_id");
    ResponseCallback send_response =
        std::bind(&Session::send_response, this, data, std::placeholders::_1,
                  std::placeholders::_2);
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
      std::cout << "Remove flow " << flow_id << std::endl;
      handle_flow_removal(flow_id);
      stop = true;
      break;
    }
    default:
      break;
    }
    if (!stop) {
      start();
    } else {
      // close this socket
      socket_.close();
    }
  } else {
    std::cerr << "Error reading message: " << error.message() << std::endl;
  }
}

void Session::handle_flow_init(int& flow_id, ResponseCallback&& send_response) {
  auto& flow_contexts = server_->flow_contexts;
  if (flow_contexts.find(flow_id) != flow_contexts.end()) {
    std::cerr << "Flow " << flow_id << " already exists" << std::endl;
    flow_id = rand();
  }
  flow_contexts[flow_id] = new FlowContext(flow_id);
  json reply;
  reply["flow_id"] = flow_id;
  std::string response = reply.dump();
  send_response(-1, response);
}

void Session::handle_congestion_control(int flow_id, json& data,
                                        ResponseCallback&& send_response) {
  auto& flow_contexts = server_->flow_contexts;
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

void Session::handle_flow_removal(int flow_id) {
  server_->handle_flow_removal(flow_id);
}

void Session::send_response(const json data, float action,
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
  std::cout << "Action: " << reply.dump(4) << std::endl;
  std::cout << "Sending response: " << std::endl;
  std::cout << response << std::endl;
#endif
  auto len = socket_.send(boost::asio::buffer(response));
  if (unlikely(len != response.length())) {
    std::cerr << "UNIX Socket Send Error: " << len << " bytes sent, "
              << response.length() << " bytes expected" << std::endl;
  }
}
