# Manual Test Commands for bridge_routes

This guide provides terminal commands (using standard ROS command-line tools like `rostopic` and MQTT tools like `mosquitto_pub`/`mosquitto_sub`) to manually verify the bridge node.

---

## 1. Setup Parameters & Launch Node

Before starting, open a terminal, set the necessary ROS parameters, and launch the bridge routes node.

```bash
# 1. 设置设备码（设为固定的 TEST_DEVICE）
rosparam set /bridge_routes/device_code "TEST_DEVICE"

# 2. 设置测试话题路由参数（pub, sub, pub_state）
rosparam set /bridge_routes/test_pub "{protocol: mqtt, topic_type: pub, ros_topic: /test/pub, remote_uri: test_pub_topic}"
rosparam set /bridge_routes/test_sub "{protocol: mqtt, topic_type: sub, ros_topic: /test/sub, remote_uri: test_sub_topic}"
rosparam set /bridge_routes/test_pub_state "{protocol: mqtt, topic_type: pub_state, ros_topic: /test/pub_state, remote_uri: test_pub_state_topic}"

# 3. 启动节点（如果以 launch 启动，请确保已运行 roscore）
rosrun bridge_routes bridge_routes_node
```

---

## 2. 测试普通发布 (pub)

验证 ROS 消息原样转发至 MQTT。

* **终端 A (MQTT 监听器)**:
  ```bash
  mosquitto_sub -h localhost -p 1883 -t "device/TEST_DEVICE/test_pub_topic" -v
  ```
* **终端 B (ROS 发布者)**:
  ```bash
  rostopic pub -1 /test/pub std_msgs/String "data: \"{\\\"name\\\": \\\"test\\\", \\\"value\\\": 123}\""
  ```

* **预期结果 (终端 A)**:
  ```text
  device/TEST_DEVICE/test_pub_topic {"name": "test", "value": 123}
  ```

---

## 3. 测试普通订阅 (sub)

验证 MQTT 消息原样转发至 ROS。

* **终端 A (ROS 监听器)**:
  ```bash
  rostopic echo /test/sub
  ```
* **终端 B (MQTT 发布者)**:
  ```bash
  mosquitto_pub -h localhost -p 1883 -t "device/TEST_DEVICE/test_sub_topic" -m "hello_from_mqtt"
  ```

* **预期结果 (终端 A)**:
  ```text
  data: "hello_from_mqtt"
  ---
  ```

---

## 4. 测试状态差量传输 (pub_state)

验证差量检测算法与 `0.01` 浮点数阈值判定。

* **终端 A (MQTT 监听器)**:
  ```bash
  mosquitto_sub -h localhost -p 1883 -t "device/TEST_DEVICE/test_pub_state_topic" -v
  ```

### 步骤 A：发布首包 (全量发送)
* **终端 B**:
  ```bash
  rostopic pub -1 /test/pub_state std_msgs/String "data: '{\"temp\": 25.0, \"voltage\": 12.5, \"status\": \"OK\", \"coords\": [1.0, 2.0]}'"
  ```
* **预期结果 (终端 A 收到全量数据)**:
  ```json
  device/TEST_DEVICE/test_pub_state_topic {"coords":[1.0,2.0],"deviceCode":"TEST_DEVICE","status":"OK","temp":25.0,"timestamp":1723985100000,"voltage":12.5}
  ```

### 步骤 B：微小数据变化比较 (小于 0.01 阈值，忽略发送)
* **终端 B**:
  ```bash
  # 变化量：temp(0.003 < 0.01)，coords[0](0.002 < 0.01)
  rostopic pub -1 /test/pub_state std_msgs/String "data: '{\"temp\": 25.003, \"voltage\": 12.5, \"status\": \"OK\", \"coords\": [1.002, 2.0]}'"
  ```
* **预期结果 (终端 A)**:
  无任何新消息输出（数据被 Tracker 拦截过滤）。

### 步骤 C：较大变化比较 (大于等于 0.01 阈值，仅发送变化字段)
* **终端 B**:
  ```bash
  # 相比步骤 A：temp 变化 0.025 >= 0.01，coords[1] 变化 0.015 >= 0.01，而 voltage 和 status 保持不变
  rostopic pub -1 /test/pub_state std_msgs/String "data: '{\"temp\": 25.025, \"voltage\": 12.5, \"status\": \"OK\", \"coords\": [1.002, 2.015]}'"
  ```
* **预期结果 (终端 A 仅收到变更的 coords 和 temp)**:
  ```json
  device/TEST_DEVICE/test_pub_state_topic {"coords":[1.002,2.015],"deviceCode":"TEST_DEVICE","temp":25.025,"timestamp":1723985200000}
  ```

---

## 5. 测试动态路由注册

验证节点在 1Hz 时钟的 `on_tick` 下，能否动态扫描 ROS 参数服务并注册新路由。

* **终端 A (MQTT 监听器)**:
  ```bash
  mosquitto_sub -h localhost -p 1883 -t "device/TEST_DEVICE/dyn_pub_topic" -v
  ```
* **终端 B (动态设置参数并测试)**:
  ```bash
  # 1. 动态新增路由参数
  rosparam set /bridge_routes/dyn_pub "{protocol: mqtt, topic_type: pub, ros_topic: /test/dyn_pub, remote_uri: dyn_pub_topic}"

  # 2. 等待 2 秒，确保 1Hz 检查函数已处理该参数

  # 3. 发布测试消息
  rostopic pub -1 /test/dyn_pub std_msgs/String "data: 'dynamic_route_ok'"
  ```
* **预期结果 (终端 A)**:
  ```text
  device/TEST_DEVICE/dyn_pub_topic dynamic_route_ok
  ```

---

## 6. 测试断联后重连全量发送

验证 MQTT 连接重建后，节点能自动将各 `pub_state` 话题的最新状态重新整包发送。

* **终端 A (MQTT 监听器)**:
  ```bash
  mosquitto_sub -h localhost -p 1883 -t "device/TEST_DEVICE/test_pub_state_topic" -v
  ```
* **终端 B (重启 MQTT 服务触发断联与重连)**:
  ```bash
  # 重启 MQTT Broker 以触发节点断线重连
  sudo systemctl restart mosquitto
  ```
* **预期结果 (终端 A)**:
  MQTT 重新连上后，终端 A 会立刻收到该状态的最新缓存数据（包含先前所有已知字段，即全量状态）：
  ```json
  device/TEST_DEVICE/test_pub_state_topic {"coords":[1.002,2.015],"deviceCode":"TEST_DEVICE","status":"OK","temp":25.025,"timestamp":1723985300000,"voltage":12.5}
  ```
