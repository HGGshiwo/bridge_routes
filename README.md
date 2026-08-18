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