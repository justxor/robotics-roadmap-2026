"""Нода «водитель»: едет вперёд, пока нет препятствия, и поворачивает, если есть.

Подписка:  /obstacle (std_msgs/Bool)
Публикует: /cmd_vel (geometry_msgs/Twist) — тот же интерфейс, что у реального робота и Nav2.
Параметры: linear_speed, angular_speed

Проверка с TurtleSim:
  ros2 run turtlesim turtlesim_node --ros-args -r /turtle1/cmd_vel:=/cmd_vel
"""
import rclpy
from geometry_msgs.msg import Twist
from rclpy.node import Node
from std_msgs.msg import Bool


class SafetyDriver(Node):
    def __init__(self):
        super().__init__('safety_driver')
        self.declare_parameter('linear_speed', 0.2)
        self.declare_parameter('angular_speed', 1.0)
        self.obstacle = False
        self.create_subscription(Bool, 'obstacle', self.on_obstacle, 10)
        self.pub = self.create_publisher(Twist, 'cmd_vel', 10)
        self.create_timer(0.1, self.tick)

    def on_obstacle(self, msg: Bool):
        self.obstacle = msg.data

    def tick(self):
        cmd = Twist()
        if self.obstacle:
            cmd.angular.z = float(self.get_parameter('angular_speed').value)
        else:
            cmd.linear.x = float(self.get_parameter('linear_speed').value)
        self.pub.publish(cmd)


def main(args=None):
    rclpy.init(args=args)
    node = SafetyDriver()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.pub.publish(Twist())   # стоп при выходе
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
