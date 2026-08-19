#include "bridge_routes/reactor_engine.hpp"

class BridgeRoutesNode {

  ros::NodeHandle nh_;
  boost::asio::io_context ioc_;
  std::shared_ptr<ReactorEngine> engine_;
  std::thread asio_thread_;

public:
  BridgeRoutesNode() {
    auto time_provider = std::make_shared<dk::RosTimeProvider>(nh_, ioc_);
    engine_ = std::make_shared<ReactorEngine>(ioc_, time_provider);
    engine_->start(std::chrono::milliseconds(100));

    asio_thread_ = std::thread([this]() {
      auto work_guard = boost::asio::make_work_guard(ioc_);
      ioc_.run();
    });
  }

  ~BridgeRoutesNode() {
    ioc_.stop();
    if (asio_thread_.joinable()) {
      asio_thread_.join();
    }
  }
};

int main(int argc, char **argv) {
  ros::init(argc, argv, ROSNODE_NAME);

  ros::AsyncSpinner spinner(4);
  spinner.start();

  BridgeRoutesNode n;

  ros::waitForShutdown();
  return 0;
}