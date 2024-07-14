#include "context.hh"

int map_action(float action, float cwnd) {
  int out;
  float tmp;
  if (action >= 0) {
    tmp = 1 + 0.025 * action;
    out = std::ceil(tmp * cwnd);
  } else {
    tmp = 1 / (1 - 0.025 * action);
    out = std::floor(tmp * cwnd);
  }
  return out;
}

FlowContext::FlowContext(int flow_id) : flow_id_(flow_id) {
  state_.resize(kStateSize * kRecurrentNum);
  std::fill(state_.begin(), state_.end(), 0);
  current_.resize(kStateSize);
  std::fill(current_.begin(), current_.end(), 0);
}

std::vector<float> FlowContext::format_state(json& data) {
  // store latest in current_
  transform_state(data);
  std::vector<float> tmp;
  tmp.resize(state_.size());
  // first copy state [10:]
  std::memcpy(tmp.data(), &state_[kStateSize],
              (state_.size() - kStateSize) * sizeof(float));
  // append current state to the end
  std::memcpy(&tmp[state_.size() - kStateSize], current_.data(),
              kStateSize * sizeof(float));
  // move slide window
  state_ = tmp;
  return tmp;
}

void FlowContext::transform_state(json& state_dict) {
  current_.clear();
  uint32_t avg_thr = state_dict["avg_thr"];
  uint32_t avg_urtt = state_dict["avg_urtt"];
  uint32_t srtt_us = state_dict["srtt_us"];
  uint32_t min_rtt = state_dict["min_rtt"];
  uint32_t max_tput = state_dict["max_tput"];
  uint32_t cwnd = state_dict["cwnd"];
  uint32_t packets_out = state_dict["packets_out"];
  uint32_t pacing_rate = state_dict["pacing_rate"];
  uint32_t retrans_out = state_dict["retrans_out"];
  double loss_ratio = state_dict["loss_ratio"];
  if (avg_thr == 0) {
    current_.push_back(0.5);
  } else {
    current_.push_back(max_tput > 0 ? (float)avg_thr / avg_thr : 0);
  }
  if (avg_urtt == 0) {
    current_.push_back(2);
  } else if (min_rtt == 0) {
    current_.push_back(0);
  } else {
    current_.push_back((float)avg_urtt / min_rtt);
  }

  if (srtt_us == 0) {
    current_.push_back(2);
  } else if (min_rtt == 0) {
    current_.push_back(0);
  } else {
    current_.push_back((float)srtt_us / 8 / min_rtt);
  }

  if (min_rtt == 0 or max_tput == 0) {
    current_.push_back(0);
  } else {
    current_.push_back((float)cwnd * 1460 * 8 / (min_rtt / 1e6) / max_tput /
                       10);
  }
  current_.push_back((float)max_tput / 1e7);
  current_.push_back((float)min_rtt / 5e5);
  current_.push_back(max_tput > 0 ? (float)loss_ratio / max_tput : 0);
  current_.push_back((float)packets_out / cwnd);
  current_.push_back(max_tput > 0 ? (float)pacing_rate / max_tput : 0);
  current_.push_back(packets_out > 0 ? (float)retrans_out / packets_out : 0);

  if (current_[2] > 2) {
    current_[2] = 2;
  }
  if (current_[1] > 2) {
    current_[1] = 2;
  }
  if (current_[3] > 2) {
    current_[3] = 2;
  }
  if (current_[8] > 2) {
    current_[8] = 2;
  }
  assert(current_.size() == kStateSize);
}