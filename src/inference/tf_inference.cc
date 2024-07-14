#include <thread>

#include "define.hh"
#include "tf_inference.hh"
// TFInference* tf_infer_session = nullptr;

TFInference::TFInference(const std::string& graph_path,
                         const std::string& checkpoint_path, const int batch) {
  create_session();
  TF_CHECK_OK(LoadModel(session_, graph_path, checkpoint_path));
  // spawn a new thread to run the inference session
  if (batch) {
    inference_thread_ = new std::thread(&TFInference::inference_loop, this);
  }
  // perform a dummy inference to warm up the session
  std::vector<std::vector<float>> states = {
      std::vector<float>(kNNInputSize, 0.0)};
  tensorflow::Tensor input = prepare_batch_input(states);
  std::vector<tensorflow::Tensor> output;
  internal_inference(input, output);
}

void TFInference::inference_loop() {
  // this loop check the inference request queue at a fixed interval
  while (keep_running_.load()) {
    std::vector<InferenceRequest> requests;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      // wait until there is at least one request
      cv_.wait(lock, [this] { return (!keep_running_.load()) || (!inference_req_queue_.empty()); });
      // requests = std::move(inference_req_queue_);
      requests.insert(requests.end(), std::make_move_iterator(inference_req_queue_.begin()), 
                    std::make_move_iterator(inference_req_queue_.end()));
      inference_req_queue_.erase(inference_req_queue_.begin(), inference_req_queue_.end());
      // inference_req_queue_.clear();
    }
    if (requests.size() > 0) {
      std::vector<std::vector<float>> states;
      std::vector<int> flow_ids;
      for (auto& req : requests) {
        flow_ids.push_back(req.first);
        states.push_back(req.second);
      }
      std::vector<float> actions = batch_inference(states);
      for (size_t i = 0; i < flow_ids.size(); ++i) {
        send_reply(flow_ids[i], actions[i]);
      }
    }
    std::this_thread::sleep_for(std::chrono::microseconds(kBatchInterval));
  }
}

void TFInference::send_reply(int flow_id, float action) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& send_response = flow_callbacks_[flow_id];
  try{
    send_response(action, "");
  } catch (const std::exception& e) {
    std::cerr << "Error sending response: " << e.what() << std::endl;
  }
  flow_callbacks_.erase(flow_id);
}

float TFInference::inference_imdt(int flow_id, std::vector<float>&& state,
                                  ResponseCallback&& send_response) {
  register_flow_callback(flow_id, send_response);
#ifdef PROFILE
  auto start = std::chrono::high_resolution_clock::now();
#endif
  std::vector<std::vector<float>> states = {state};
  tensorflow::Tensor input = prepare_batch_input(states);
  std::vector<tensorflow::Tensor> output;
  auto start = std::chrono::high_resolution_clock::now();
  internal_inference(input, output);
  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  std::cout << "Inference time: " << duration.count() << " microseconds"
            << std::endl;
  float action = output[0].flat<float>().data()[0];
#ifdef DEBUG
  std::cout << "Inference: "
            << " flow_id " << flow_id << ", state: " << print_state(state)
            << ", action: " << action << std::endl;
#endif

  send_reply(flow_id, action);
#ifdef PROFILE
  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  std::cout << "Inference time: " << duration.count() << " microseconds"
            << std::endl;
#endif
  return action;
}

void TFInference::submit_inference_request(int flow_id,
                                           std::vector<float>&& state,
                                           ResponseCallback&& send_response) {
  // store the inference request
  std::lock_guard<std::mutex> lock(mutex_);
  register_flow_callback(flow_id, std::move(send_response));
  inference_req_queue_.push_back({flow_id, state});
  cv_.notify_all();
}

std::vector<float> TFInference::batch_inference(
    const std::vector<std::vector<float>>& states) {
  tensorflow::Tensor input = prepare_batch_input(states, states.size());
  std::vector<tensorflow::Tensor> output;
  internal_inference(input, output);
  std::vector<float> actions;
  actions.resize(states.size());
  auto values = output[0].flat<float>().data();
  for (size_t i = 0; i < states.size(); ++i) {
    actions[i] = values[i];
  }
  return actions;
}

tensorflow::Tensor TFInference::prepare_batch_input(
    const std::vector<std::vector<float>>& states, int batch) {
  tensorflow::TensorShape input_shape({batch, kNNInputSize});
  tensorflow::Tensor tmp(tensorflow::DT_FLOAT, input_shape);
  // copy state to input tensor
  for (int i = 0; i < batch; ++i) {
    auto state = states[i];
    auto input = tmp.tensor<float, 2>();
    for (size_t j = 0; j < kNNInputSize; ++j) {
      input(i, j) = state[j];
    }
  }
  return tmp;
}

int TFInference::internal_inference(const tensorflow::Tensor& data,
                                    std::vector<tensorflow::Tensor>& output) {
  static tensorflow::Tensor train_flag(tensorflow::DT_BOOL,
                                       tensorflow::TensorShape());
  *train_flag.flat<bool>().data() = false;

  // std::cout << data.DebugString() << std::endl;
  // std::cout << train_flag.DebugString() << std::endl;
  TensorDict feedDict = {
      {"s0:0", data},
      {"Actor_is_training:0", train_flag},
  };
  std::vector<std::string> outputOps = {
      {"actor/Mul:0"},
  };
  // std::vector<tensorflow::Tensor> outputTensors;
  tensorflow::Status status = session_->Run(feedDict, outputOps, {}, &output);
  if (!status.ok()) {
    std::cout << status.ToString() << "\n";
    throw std::runtime_error("Error during inference");
  }
  // output = outputTensors;
  return 0;
}

int TFInference::create_session() {
  tensorflow::SessionOptions options;

  tensorflow::ConfigProto* config = &options.config;
  config->set_allow_soft_placement(true);
  tensorflow::Status status = NewSession(options, &session_);
  if (!status.ok()) {
    std::cout << status.ToString() << "\n";
    return 1;
  }
  std::cout << "Session successfully created.\n";
  return 1;
}

tensorflow::Status TFInference::LoadModel(tensorflow::Session* sess,
                                          std::string graph_fn,
                                          std::string checkpoint_fn) {
  tensorflow::Status status;

  // Read in the protobuf graph we exported
  tensorflow::MetaGraphDef graph_def;
  status = ReadBinaryProto(tensorflow::Env::Default(), graph_fn, &graph_def);
  if (status != tensorflow::Status::OK()) {
    std::cout << status.ToString() << std::endl;
    return status;
  }

  // create the graph in the current session
  status = sess->Create(graph_def.graph_def());
  if (status != tensorflow::Status::OK()) {
    std::cout << status.ToString() << std::endl;
    return status;
  }

  // restore model from checkpoint, iff checkpoint is given
  if (checkpoint_fn != "") {
    const std::string restore_op_name = graph_def.saver_def().restore_op_name();
    const std::string filename_tensor_name =
        graph_def.saver_def().filename_tensor_name();

    tensorflow::Tensor filename_tensor(tensorflow::DT_STRING,
                                       tensorflow::TensorShape());
    filename_tensor.scalar<std::string>()() = checkpoint_fn;

    TensorDict feed_dict = {{filename_tensor_name, filename_tensor}};
    status = sess->Run(feed_dict, {}, {restore_op_name}, nullptr);
    if (status != tensorflow::Status::OK()) {
      std::cout << status.ToString() << std::endl;
      return status;
    }
  } else {
    // virtual Status Run(const std::vector<std::pair<string, Tensor> >&
    // inputs,
    //                  const std::vector<string>& output_tensor_names,
    //                  const std::vector<string>& target_node_names,
    //                  std::vector<Tensor>* outputs) = 0;
    status = sess->Run({}, {}, {"init"}, nullptr);
    if (status != tensorflow::Status::OK()) {
      std::cout << status.ToString() << std::endl;
      return status;
    }
  }

  return tensorflow::Status::OK();
}