#include "dk/RosTimeProvider.hpp"
#include "dk/adapters/mqtt.hpp"
#include "dk/engine.hpp"
#include "nlohmann/json_fwd.hpp"
#include "ros/node_handle.h"
#include "ros/publisher.h"
#include "ros/subscriber.h"
#include "std_msgs/String.h"
#include <boost/filesystem.hpp>
#include <dk_auto_json.hpp>
#include <exception>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <ros/package.h>
#include <ros/ros.h>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <functional>
#include <yaml-cpp/yaml.h>

const std::string ROSNODE_NAME = "bridge_routes";
namespace fs = std::filesystem;

struct AppContext {};

#include "bridge_routes/events.hpp"
#include "bridge_routes/state_diff_tracker.hpp"

class MyReactorEngine : public dk::BaseEngine<AppContext, MyReactorEngine> {
public:
  // 声明当前引擎关心的事件列表
  using AllowedEvents = std::tuple<dk::MqttConnectEvent>;
  using MqttAdapter = dk::MqttClientAdapter<AppContext, MyReactorEngine>;
  using BaseEngine::BaseEngine;

  std::shared_ptr<MqttAdapter> mqtt_adapter_;
  std::map<std::string, ros::Publisher> ros_pub_;
  std::map<std::string, ros::Subscriber> ros_sub_;

  std::vector<std::function<void()>> reconnect_callbacks_;

  void on_event(const dk::MqttConnectEvent &event, AppContext &ctx) {
    if(!device_code_.has_value()) return;
    ROS_INFO("[BridgeRoutes] MQTT connected, publishing full states...");
    for (const auto &cb : reconnect_callbacks_) {
      cb();
    }
  }

  ros::NodeHandle nh_;
  ros::NodeHandle private_nh_{"~"};
  std::optional<std::string> device_code_;

  // 已经注册的task名称
  std::set<std::string> task_set_;

  // 启动的回调函数
  void on_start() override {
    std::string mqtt_host =
        private_nh_.param("mqtt_host", std::string("localhost"));
    unsigned short mqtt_port = private_nh_.param("mqtt_port", 1883);
    std::string device_code_str;
    if (private_nh_.getParam("device_code", device_code_str)) {
      device_code_ = device_code_str;
    }

    mqtt_adapter_ =
        std::make_shared<MqttAdapter>(shared_from_this(), mqtt_host, mqtt_port);
    ROS_INFO_STREAM("[BridgeRoutes] mqtt start at " << mqtt_host << ":"
                                                    << mqtt_port);
    setup_mqtt();
  }

  // 1hz的回调函数
  void on_tick(double dt, AppContext &ctx) override {
    if (is_hz(1.0)) {
      setup_all_topic();
    }
  }

  // 配置mqtt业务连接
  void setup_mqtt() {
    if (device_code_.has_value()) {
      ROS_INFO_STREAM("[Mqtt] use existing code: " << device_code_.value());
      mqtt_adapter_->connect(device_code_.value());
      setup_all_topic();
    } else {
      mqtt_adapter_->register_publish_handler<RegisterEvent>(
          "$exclusive/register", [this](const RegisterEvent &data) -> void {
            if (!device_code_.has_value()) {
              device_code_ = data.deviceCode;

              mqtt_adapter_->connect(device_code_.value());
              setup_all_topic();

              private_nh_.setParam("device_code", device_code_.value());
              save_param("device_code", std::string(""));

              ROS_INFO_STREAM(
                  "[Mqtt] register with code: " << device_code_.value());
            }
          });

      mqtt_adapter_->connect(); // 先注册
    }
  }

  // 从父参数根据key获取参数
  template <typename T>
  std::optional<T> extract_param(const std::string &parent_key,
                                 XmlRpc::XmlRpcValue &val,
                                 const std::string &key, bool verbose = true) {
    if (!val.hasMember(key)) {
      if (verbose)
        ROS_ERROR_STREAM("[BridgeRoutes] " << parent_key << " must include "
                                           << key);
      return std::nullopt;
    }

    T ret;
    try {
      ret = (T)val[key];
    } catch (const std::exception_ptr &ex) {
      ROS_ERROR_STREAM("[BridgeRoutes] " << parent_key << "convert " << key
                                         << " failed!");
      return std::nullopt;
    }
    return ret;
  }

  // 从ROS param中读取并配置所有协议的消息
  void setup_all_topic() {
    if (!device_code_.has_value())
      return;

    XmlRpc::XmlRpcValue namespace_params;
    if (ros::param::get(ROSNODE_NAME, namespace_params)) {
      // 检查是否为结构（字典）
      if (namespace_params.getType() != XmlRpc::XmlRpcValue::TypeStruct) {
        ROS_ERROR_STREAM("[BridgeRoute] param in " << ROSNODE_NAME
                                                   << " is not struct!");
        return;
      }

      for (auto it = namespace_params.begin(); it != namespace_params.end();
           ++it) {
        std::string key = it->first;
        
        // device_code本身不作为配置项
        if(key == "device_code") continue;

        if (task_set_.find(key) != task_set_.end())
          continue;
        task_set_.insert(key);

        XmlRpc::XmlRpcValue &val = it->second;

        auto protocal = extract_param<std::string>(key, val, "protocol");
        if (!protocal.has_value())
          continue;

        auto topic_type = extract_param<std::string>(key, val, "topic_type");
        if (!topic_type.has_value())
          continue;

        auto ros_topic = extract_param<std::string>(key, val, "ros_topic");
        if (!ros_topic.has_value())
          continue;

        auto remote_uri = extract_param<std::string>(key, val, "remote_uri");
        if (!remote_uri.has_value())
          continue;

        if (protocal.value() == "mqtt") {
          auto qos = extract_param<int>(key, val, "qos", false);
          auto retain = extract_param<bool>(key, val, "retain", false);
          auto mqtt_topic = remove_slash(remote_uri.value());

          if (topic_type == "pub") {
            register_mqtt_pub(ros_topic.value(), mqtt_topic, qos.value_or(0),
                               retain.value_or(false), false);
          } else if (topic_type == "pub_state") {
            register_mqtt_pub(ros_topic.value(), mqtt_topic, qos.value_or(0),
                               retain.value_or(false), true);
          } else if (topic_type == "sub") {
            register_mqtt_sub(ros_topic.value(), mqtt_topic, qos.value_or(0));
          }
        }
      }
    }
  }

  // 订阅mqtt消息，转发到ros
  void register_mqtt_sub(std::string ros_topic, std::string mqtt_topic,
                         int qos) {

    if (!device_code_.has_value())
      return;

    auto it = ros_pub_.begin();
    it = ros_pub_.find(ros_topic);
    if (it != ros_pub_.end()) {
      ROS_ERROR_STREAM("[Mqtt] rostopic " << ros_topic << " exists!"
                                          << "mqtt_topic=" << mqtt_topic);
      return;
    }

    auto pub = nh_.advertise<std_msgs::String>(ros_topic, 1000);
    ros_pub_[ros_topic] = pub;

    mqtt_topic = fmt::format("device/{}/{}", device_code_.value(), mqtt_topic);
    mqtt_adapter_->register_raw_handler(
        mqtt_topic,
        [pub](const dk::MqttMessage &msg) {
          std_msgs::String ros_msg;
          ros_msg.data = msg.payload;
          pub.publish(ros_msg);
        },
        qos);
    ROS_INFO_STREAM("[Mqtt] mqtt->ros: " << ros_topic << " -> " << mqtt_topic);
  }

  // 订阅ros消息，以差量方式转发到mqtt (如果是 state 类型，进行差量传输)
  void register_mqtt_pub(std::string ros_topic, std::string mqtt_topic, int qos,
                         bool retain, bool is_state) {
    if (!device_code_.has_value())
      return;

    auto it = ros_sub_.begin();
    it = ros_sub_.find(ros_topic);
    if (it != ros_sub_.end()) {
      ROS_ERROR_STREAM("[Mqtt] rostopic " << ros_topic << " exists!"
                                          << "mqtt_topic=" << mqtt_topic);
      return;
    }

    std::shared_ptr<StateDiffTracker> tracker;
    if (is_state) {
      tracker = std::make_shared<StateDiffTracker>();

      // 注册一个发布全量状态的闭包回调
      reconnect_callbacks_.push_back([this, mqtt_topic, qos, retain, tracker]() {
        nlohmann::json last_state = tracker->get_last_state();
        if (!last_state.empty()) {
          publish_mqtt_msg(last_state, mqtt_topic, qos, retain);
        }
      });
    }

    auto sub = nh_.subscribe<std_msgs::String>(
        ros_topic, 1000,
        [this, mqtt_topic, qos, retain, ros_topic, is_state,
         tracker](const std_msgs::String::ConstPtr &msg) -> void {
          if (is_state) {
            nlohmann::json current_json;
            if (!parse_ros_msg(ros_topic, msg->data, current_json)) {
              return;
            }
            nlohmann::json diff_json = tracker->update_and_get_diff(current_json);
            if (diff_json.empty()) {
              return; // 没有改变，跳过发布
            }
            nlohmann::json data_to_publish = diff_json;
            publish_mqtt_msg(data_to_publish, mqtt_topic, qos, retain);
          } else {
            // 普通 pub 直接原样转发
            mqtt_adapter_->publish(
                fmt::format("device/{}/{}", device_code_.value(), mqtt_topic),
                msg->data, qos, retain);
          }
        });

    ros_sub_[ros_topic] = sub;
    ROS_INFO_STREAM("[Mqtt] ros[state=" << is_state << "]"
                                        << "->mqtt: " << ros_topic << " -> "
                                        << mqtt_topic);
  }

  // ROS_MSG -> JSON
  bool parse_ros_msg(const std::string &ros_topic, std::string msg_data,
                     nlohmann::json &current_json) {
    try {
      current_json = nlohmann::json::parse(msg_data);
    } catch (const std::exception &e) {
      ROS_ERROR_STREAM("[BridgeRoutes] Failed to parse JSON from "
                       << ros_topic << ": " << e.what());
      return false;
    }

    if (!current_json.is_object()) {
      ROS_ERROR_STREAM("[BridgeRoutes] JSON from " << ros_topic
                                                   << " is not an object!");
      return false;
    }
    return true;
  }

  // 底层发送函数，包装为云端需要的格式并且发送
  void publish_mqtt_msg(nlohmann::json &data, const std::string &mqtt_topic,
                        const int &qos, const bool &retain) {
    data["deviceCode"] = device_code_.value();
    data["timestamp"] = (uint64_t)(get_time_provider()->now() * 1000);
    mqtt_adapter_->publish(
        fmt::format("device/{}/{}", device_code_.value(), mqtt_topic),
        data.dump(), qos, retain);
  }


  // 持久化参数
  template <typename T>
  void save_param(const std::string &param_name, T default_value) {
    T value = private_nh_.param(param_name, default_value);
    std::string str = ros::package::getPath(ROSNODE_NAME);
    fs::path p(str); // 直接构造

    YAML::Node root;
    root[param_name] = value;
    std::ofstream fout(p / "config" / "device_code.yaml");
    fout << root;
  }

  // 去掉mqtt_topic前面的/（如果有人误写的话）
  std::string remove_slash(std::string str) {
    if (!str.empty() && str.front() == '/') {
      str.erase(0, 1); // 从索引 0 开始，删除 1 个字符
    }
    return str;
  }
};