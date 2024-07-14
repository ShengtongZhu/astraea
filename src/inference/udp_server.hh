#ifndef UDP_SERVER_HH
#define UDP_SERVER_HH

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>
#include <iostream>
#include <memory>

#include "context.hh"
#include "serialization.hh"
#include "server.hh"

// class Server;

class UdpServer : public Server {
 public:
  UdpServer(boost::asio::io_service& io_service);

  virtual void start() override;

 protected:
  virtual void handle_flow_init(int& flow_id,
                                ResponseCallback&& send_response) override;
  virtual void handle_congestion_control(
      int flow_id, json& data, ResponseCallback&& send_response) override;

 private:
  void handle_receive(const boost::system::error_code& error,
                      std::size_t bytes_transferred);

  void send_response(boost::asio::ip::udp::endpoint remote_endpoint,
                     const json data, float action,
                     const std::string& info = "");

  void handle_send(const boost::system::error_code& error,
                   std::size_t bytes_transferred);

 private:
  boost::asio::ip::udp::socket socket_;
  boost::asio::ip::udp::endpoint remote_endpoint_;
  std::array<char, 1024> recv_buffer_;
};

#endif  // UDP_SERVER_HH