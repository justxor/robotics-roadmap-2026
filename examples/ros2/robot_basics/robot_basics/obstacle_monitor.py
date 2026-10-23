"""Нода обработки: фильтрует /range и решает, есть ли препятствие.

Подписка:  /range (sensor_msgs/Range)
Публикует: /range_filtered (sensor_msgs/Range), /obstacle (std_msgs/Bool)
Параметры: threshold_m (меняется на лету!), filter_alpha
Сервис:    ~/enable (std_srvs/SetBool) — включить/выключить мониторинг

Попробуй:
  ros2 param set /obstacle_monitor threshold_m 0.8
  ros2 service call /obstacle_monitor/enable std_srvs/srv/SetBool "{data: false}"
"""
import rclpy
from rcl_interfaces.msg import SetParametersResult
from rclpy.node import Node
from sensor_msgs.msg import Range
from std_msgs.msg import Bool
from std_srvs.srv import SetBool


class ObstacleMonitor(Node):
    def __init__(self):
        super().__init__('obstacle_monitor')
        self.declare_parameter('threshold_m', 0.5)
        self.declare_parameter('filter_alpha', 0.3)   # экспоненциальное сглаживание
        self.add_on_set_parameters_callback(self.on_params)

        self.enabled = True
        self.filtered = None
        self.last_state = None

        self.sub = self.create_subscription(Range, 'range', self.on_range, 10)
        self.pub_filtered = self.create_publisher(Range, 'range_filtered', 10)
        self.pub_obstacle = self.create_publisher(Bool, 'obstacle', 10)
        self.srv = self.create_service(SetBool, '~/enable', self.on_enable)

    def on_params(self, params):
        for p in params:
            if p.name == 'threshold_m' and p.value <= 0:
                return SetParametersResult(successful=False, reason='threshold_m должен быть > 0')
            if p.name == 'filter_alpha' and not 0.0 < p.value <= 1.0:
                return SetParametersResult(successful=False, reason='filter_alpha в (0, 1]')
        for p in params:
            self.get_logger().info(f'Параметр {p.name} = {p.value}')
        return SetParametersResult(successful=True)

    def on_enable(self, request, response):
        self.enabled = request.data
        response.success = True
        response.message = 'мониторинг ' + ('включён' if self.enabled else 'выключен')
        self.get_logger().info(response.message)
        return response

    def on_range(self, msg: Range):
        if not self.enabled:
            return
        alpha = self.get_parameter('filter_alpha').value
        self.filtered = msg.range if self.filtered is None else alpha * msg.range + (1 - alpha) * self.filtered

        out = Range()
        out.header = msg.header
        out.radiation_type, out.field_of_view = msg.radiation_type, msg.field_of_view
        out.min_range, out.max_range = msg.min_range, msg.max_range
        out.range = float(self.filtered)
        self.pub_filtered.publish(out)

        obstacle = self.filtered < self.get_parameter('threshold_m').value
        self.pub_obstacle.publish(Bool(data=obstacle))
        if obstacle != self.last_state:
            self.get_logger().info(('⚠️  ПРЕПЯТСТВИЕ' if obstacle else '✅ путь свободен') + f' ({self.filtered:.2f} м)')
            self.last_state = obstacle


def main(args=None):
    rclpy.init(args=args)
    node = ObstacleMonitor()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
