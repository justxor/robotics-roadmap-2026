"""Нода-«датчик»: публикует sensor_msgs/Range (как HC-SR04/VL53L0X) — без железа.

Расстояние «плавает» по синусоиде + шум, иногда к роботу «подходит» препятствие.
Параметры: rate_hz, frame_id, noise_m.
"""
import math
import random

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Range


class FakeRangeSensor(Node):
    def __init__(self):
        super().__init__('fake_range_sensor')
        self.declare_parameter('rate_hz', 20.0)
        self.declare_parameter('frame_id', 'range_front')
        self.declare_parameter('noise_m', 0.01)

        rate = self.get_parameter('rate_hz').value
        self.pub = self.create_publisher(Range, 'range', 10)
        self.t = 0.0
        self.dt = 1.0 / rate
        self.timer = self.create_timer(self.dt, self.tick)
        self.get_logger().info(f'Публикую /range с частотой {rate:.0f} Гц')

    def tick(self):
        self.t += self.dt
        distance = 1.0 + 0.8 * math.sin(0.4 * self.t)        # 0.2 … 1.8 м
        distance += random.gauss(0.0, self.get_parameter('noise_m').value)

        msg = Range()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = self.get_parameter('frame_id').value
        msg.radiation_type = Range.ULTRASOUND
        msg.field_of_view = math.radians(15.0)
        msg.min_range = 0.02
        msg.max_range = 4.0
        msg.range = float(max(msg.min_range, min(msg.max_range, distance)))
        self.pub.publish(msg)


def main(args=None):
    rclpy.init(args=args)
    node = FakeRangeSensor()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
