#ifndef SERVER_HH
#define SERVER_HH

#include <string>

#include "context.hh"
#include "define.hh"

class FlowContext;
class Server {
 public:
  Server() {}
  virtual ~Server() {}
  virtual void start() = 0;

 protected:
  virtual void handle_flow_init(int& flow_id,
                                ResponseCallback&& send_response) = 0;
  virtual void handle_congestion_control(int flow_id, json& data,
                                         ResponseCallback&& send_response) = 0;

  virtual void handle_flow_removal(int flow_id) {
    if (flow_contexts.find(flow_id) == flow_contexts.end()) {
      std::cerr << "Flow " << flow_id << " does not exist" << std::endl;
      return;
    }
    delete flow_contexts[flow_id];
    flow_contexts.erase(flow_id);
  }

 protected:
  // per flow inference context
  std::unordered_map<int, FlowContext*> flow_contexts;
  enum class MessageType {
    INIT = 0,
    START = 1,
    END = 2,
    ALIVE = 3,
    OBSERVE = 4
  };
};

#endif  // SERVER_HH