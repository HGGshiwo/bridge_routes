#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <cmath>

class StateDiffTracker {
public:
  // 递归比较 json 节点差异，数字或数字数组进行阈值 (0.01) 比较
  static bool is_value_changed(const nlohmann::json &v1, const nlohmann::json &v2) {
    if (v1.type() != v2.type()) {
      return true;
    }
    if (v1.is_number()) {
      return std::abs(v1.get<double>() - v2.get<double>()) >= 0.01;
    }
    if (v1.is_array()) {
      if (v1.size() != v2.size()) {
        return true;
      }
      for (size_t i = 0; i < v1.size(); ++i) {
        if (is_value_changed(v1[i], v2[i])) {
          return true;
        }
      }
      return false;
    }
    if (v1.is_object()) {
      if (v1.size() != v2.size()) {
        return true;
      }
      for (auto it = v1.begin(); it != v1.end(); ++it) {
        if (!v2.contains(it.key()) || is_value_changed(it.value(), v2.at(it.key()))) {
          return true;
        }
      }
      return false;
    }
    return v1 != v2;
  }

  // 传入新的 json，更新内部缓存的 last_state，并返回发生变化字段的 diff json。
  // 如果没有变化，返回空 json 对象。
  nlohmann::json update_and_get_diff(const nlohmann::json &current_json) {
    if (!current_json.is_object()) {
      return nlohmann::json::object();
    }

    nlohmann::json diff_json = nlohmann::json::object();

    // 找出新增或改变的字段
    for (auto it = current_json.begin(); it != current_json.end(); ++it) {
      const std::string &key = it.key();
      if (!last_state_.contains(key) || is_value_changed(it.value(), last_state_[key])) {
        diff_json[key] = it.value();
      }
    }



    // 更新本地缓存
    for (auto it = diff_json.begin(); it != diff_json.end(); ++it) {
      last_state_[it.key()] = it.value();
    }

    return diff_json;
  }

  // 获取当前的全量状态
  nlohmann::json get_last_state() const {
    return last_state_;
  }

private:
  nlohmann::json last_state_ = nlohmann::json::object();
};
