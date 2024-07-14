#include "define.hh"

std::string graphPath = "models/my-model.meta";
std::string checkpointPath = "models/my-model";
int batchMode = false;
std::string channel = "unix";

std::string print_state(const std::vector<float>& state) {
  std::string str = "[";
  for (auto& s : state) {
    str += std::to_string(s) + ", ";
  }
  str += "]";
  return str;
}