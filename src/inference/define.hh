#ifndef DEFINE_HH
#define DEFINE_HH

#include <iostream>
#include <string>

#include "json.hpp"

using json = nlohmann::json;

#define PORT 8888

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

typedef std::function<void(float, const std::string&)> ResponseCallback;

const size_t kStateSize = 10;
const size_t kRecurrentNum = 5;
const size_t kNNInputSize = 50;

// 3000us batch
const size_t kBatchInterval = 5000;

extern std::string graphPath;
extern std::string checkpointPath;

// use UDP or UNIX socket
extern std::string channel;

extern int batchMode;
std::string print_state(const std::vector<float>& state);

#endif  // DEFINE_HH