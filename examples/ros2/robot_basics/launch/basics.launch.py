"""Запуск системы из трёх нод с параметрами из YAML и аргументами командной строки.

  ros2 launch robot_basics basics.launch.py
  ros2 launch robot_basics basics.launch.py threshold:=0.8 namespace:=robot1
"""
import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    params = os.path.join(get_package_share_directory('robot_basics'), 'config', 'params.yaml')
    ns = LaunchConfiguration('namespace')
    threshold = LaunchConfiguration('threshold')

    return LaunchDescription([
        DeclareLaunchArgument('namespace', default_value='', description='пространство имён робота'),
        DeclareLaunchArgument('threshold', default_value='0.5', description='порог препятствия, м'),

        Node(package='robot_basics', executable='fake_range_sensor', namespace=ns,
             parameters=[params], output='screen'),
        Node(package='robot_basics', executable='obstacle_monitor', namespace=ns,
             parameters=[params, {'threshold_m': threshold}], output='screen'),
        Node(package='robot_basics', executable='safety_driver', namespace=ns,
             parameters=[params], output='screen'),
    ])
