# Bridge_Routes

启动：

```
roslaunch bridge_routes bridge_routes.launch
```


外部节点的配置文件：

```yaml
bridge_routes:
  # 键名是唯一的任务名，其他节点可以随时向 /bridge_routes 中添加新的键
  lidar:
    protocol: "mqtt"
    topic_type: "pub"
    ros_topic: "/sensor/lidar/json"
    remote_uri: "robot01/lidar"  # mqtt topic 或 http url
    
  control:
    protocol: "ws"
    topic_type: "sub"
    ros_topic: "/cmd_vel/json"
    remote_uri: "/ws/control"
```

| 参数名称       | 含义                          | 支持的值              | 默认值  |
| ---------- | --------------------------- | ----------------- | ---- |
| ros_topic  | 对应的ros话题，类型是std_msgs/String |                   |      |
| remote_uri | 云端的URL                      |                   |      |
| topic_type | 消息类型，发布或者是订阅                | sub/pub/pub_state |      |
| protocol   | 需要订阅或者发布的协议类型               | mqtt/ws/http      | mqtt |

**NOTE**：
- 当topic_type=pub_state的时候，会将内容解析为一个json对象，并且尝试差量传输，并对number或者number数组类型的进行阈值过滤（阈值为0.01）
- ros消息类型全部是std_msgs/String，数据格式为json （防止动态加载message类型，以及如何从message提取想要的数据）
- gateway会1hz频率检查rosparam，如果是关键信息，发布前需要保证：`pub.getNumSubscribers() > 0`
- 以下字符不能作为任务名称：device_code

代码注册方式（将名为task的mqtt话题转发到/task/json的rostopic中：
(如果是mqtt，会自动拼接`/device/deviceCode/{mqtt_topic}`，实际订阅的mqtt话题是/device/deviceCode/task)
```python
import rospy


node_name = rospy.get_name().strip('/')
task_key = f"{node_name}_task" 

rospy.set_param(f'/bridge_routes/{task_key}', {
    'protocol': 'mqtt',
    'direction': 'sub',
    'ros_topic': '/task/json',
    'remote_uri': '/task'
})
```

launch注册方式

```xml
<rosparam param="/bridge_routes/$(arg node_name)_task">
	protocol: mqtt
	direction: sub
	ros_topic: /task/json
	remote_uri: /task
</rosparam>
```

## WebSocket 桥接与 HTTP ROS 服务桥接

本节点支持通过 `WebAdapter` 实现 WebSocket 桥接和 HTTP 转发 ROS 服务功能。

### 1. WebSocket 桥接配置

在配置文件/参数中，若 `protocol` 为 `ws` 或 `websocket`：
*   **`topic_type` 为 `sub`**：订阅外部客户端发往 `remote_uri` 的 WebSocket 消息，并将其发布到指定的 `ros_topic` 中。
*   **`topic_type` 为 `pub`**：订阅 `ros_topic`，当收到 ROS 消息时，将其以 JSON 形式广播给所有连接在 `/remote_uri` 上的 WebSocket 客户端。
*   **`topic_type` 为 `pub_state`**：同上，但结合了差量传输机制（`StateDiffTracker`），并且会在新客户端连接时立即通过可靠传输（`send_state`）向其补发最新的全量状态。

**端口配置**：通过私有参数 `web_port` 配置 Web 监听端口（默认值为 `8000`）。

### 2. HTTP ROS 服务桥接

本节点支持在 `rosparam` 中动态绑定外部 HTTP 路由与 ROS 服务，将外部 HTTP POST 请求转换为 ROS Service 调用，并将返回值作为 HTTP 响应返回。

**配置示例**：
若 `protocol` 为 `http`（`topic_type` 字段可选或被忽略）：
*   **`ros_topic`**：目标 ROS 服务的名字（例如 `/camera/get_config`）。
*   **`remote_uri`**：外部调用该服务的 HTTP 接口路径（例如 `/api/camera/config`）。

**接口调用方式**：
*   **接口地址**：`POST http://<host>:<web_port>/api/camera/config` (根据 `remote_uri` 自动注册路由)
*   **请求 Body 格式**：直接发送业务原生的 JSON 报文（无需任何特殊外层包装）。
*   **说明**：目标 ROS Service 必须为 [`bridge_routes/StringSrv`](file:///home/hggshiwo/catkin_ws/src/bridge_routes/srv/StringSrv.srv) 类型。当外部 HTTP POST 过来时，请求体的内容将直接作为 ROS Service 的 `request` 字段传入。我们在非阻塞后台线程中发起该 ROS 服务调用，调用成功后会将 Service 响应的 `response` 字段内容直接作为 HTTP 响应体返回给客户端。