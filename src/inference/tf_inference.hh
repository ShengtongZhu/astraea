#ifndef TF_INFERENCE_HH
#define TF_INFERENCE_HH

#include <deque>
#include <mutex>
#include <thread>

#include <tensorflow/core/platform/env.h>
#include <tensorflow/core/protobuf/meta_graph.pb.h>
#include <tensorflow/core/public/session.h>

#include "define.hh"
typedef std::vector<std::pair<std::string, tensorflow::Tensor>> TensorDict;

class TFInference;
class TFInference {
 public:
  static TFInference* Get() {
    static TFInference tf_inference(graphPath, checkpointPath, batchMode);
    return &tf_inference;
  }

  void stop() {
    cv_.notify_all();
    keep_running_ = false;
    if (inference_thread_) {
      inference_thread_->join();
    }
  }

 private:
  TFInference(const std::string& graph_path, const std::string& checkpoint_path,
              const int batch);
  // disallow copy and assign
  TFInference(const TFInference&) = delete;
  TFInference& operator=(const TFInference&) = delete;
  ~TFInference() { delete session_; }

 public:
  void submit_inference_request(int flow_id, std::vector<float>&& state,
                                ResponseCallback&& send_response);
  /**
   * @brief Perform the inference immediately and send the response back
   *
   * @param state
   * @param send_response
   * @return float
   */
  float inference_imdt(int flow_id, std::vector<float>&& state,
                       ResponseCallback&& send_response);

 private:
  /**
   * @brief The main inference loop
   * This function runs the batch inference service. It operates in a new thread
   * and stores a queue of inference requests.
   *
   */
  void inference_loop();
  tensorflow::Tensor prepare_batch_input(
      const std::vector<std::vector<float>>& states, int batch = 1);

  /**
   * @brief Perform batch inference asynchronously
   *
   * @param states
   * @return std::vector<float>
   */
  public:
  std::vector<float> batch_inference(
      const std::vector<std::vector<float>>& states);

  int internal_inference(const tensorflow::Tensor& data,
                         std::vector<tensorflow::Tensor>& output);

  void send_reply(int flow_id, float action);

  int create_session();

  tensorflow::Status LoadModel(tensorflow::Session* sess, std::string graph_fn,
                               std::string checkpoint_fn = "");

  inline void register_flow_callback(int flow_id,
                                     ResponseCallback send_response) {
    flow_callbacks_[flow_id] = send_response;
  }

 private:
  using InferenceRequest = std::pair<int, std::vector<float>>;
  tensorflow::Session* session_;
  // for batch inference
  std::vector<InferenceRequest> inference_req_queue_;
  std::unordered_map<int, ResponseCallback> flow_callbacks_;
  std::mutex mutex_;
  std::condition_variable cv_;

  // for batch inference
  std::thread* inference_thread_;
  // flag to indicate whether stop 
  std::atomic<bool> keep_running_ = true;
};

#endif  // TF_INFERENCE_HH