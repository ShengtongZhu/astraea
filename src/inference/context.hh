#ifndef CONTEXT_HH
#define CONTEXT_HH

#include "define.hh"
#include "tf_inference.hh"

int map_action(float action, float cwnd);

class FlowContext {
 public:
  FlowContext(int flow_id);

  // get new cwnd from model
  std::vector<float> format_state(json& data);

 private:
  void transform_state(json& state_dict);

 private:
  int flow_id_;
  // 1 * 10
  std::vector<float> current_;
  // 1 * 50
  std::vector<float> state_;
};

#endif  // CONTEXT_HH