#ifndef UNIX_SOCKET_SERVER_HH
#define UNIX_SOCKET_SERVER_HH

#include <string>
#include <unordered_map>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "server.hh"

class UnixSocketServer;
class Session : public std::enable_shared_from_this<Session>, Server {
 public:
  Session(boost::asio::io_service& io_service);

  boost::asio::local::stream_protocol::socket& socket();

  virtual void start() override;

  void set_udp_server(UnixSocketServer* server) { server_ = server; }

 protected:
  virtual void handle_flow_init(int& flow_id,
                                ResponseCallback&& send_response) override;
  virtual void handle_congestion_control(
      int flow_id, json& data, ResponseCallback&& send_response) override;

  virtual void handle_flow_removal(int flow_id) override;

 private:
  void handle_read_length(const boost::system::error_code& error);
  void handle_read_message(const boost::system::error_code& error,
                           std::size_t expected_length);
  void send_response(const json data, float action, const std::string& info);

 private:
  boost::asio::local::stream_protocol::socket socket_;
  std::array<char, 1024> recv_buffer_;
  std::array<char, 2> message_length_buffer_;
  uint16_t message_length_;
  // per flow inference context
  UnixSocketServer* server_;
};

class UnixSocketServer : public Server {
 public:
  friend class Session;
  UnixSocketServer(boost::asio::io_service& io_service,
                   const std::string& socket_path);

  virtual void start() override;

 protected:
  virtual void handle_flow_init(int& flow_id,
                                ResponseCallback&& send_response) override {}
  virtual void handle_congestion_control(
      int flow_id, json& data, ResponseCallback&& send_response) override {}

 private:
  void handle_accept(std::shared_ptr<Session> new_session,
                     const boost::system::error_code& error);

 private:
  boost::asio::io_service& io_service_;
  boost::asio::local::stream_protocol::acceptor acceptor_;
};

#endif  // UNIX_SOCKET_SERVER_HH