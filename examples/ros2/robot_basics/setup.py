from glob import glob
from setuptools import find_packages, setup

package_name = 'robot_basics'

setup(
    name=package_name,
    version='0.1.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        ('share/' + package_name + '/launch', glob('launch/*.launch.py')),
        ('share/' + package_name + '/urdf', glob('urdf/*')),
        ('share/' + package_name + '/config', glob('config/*')),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='you',
    maintainer_email='you@example.com',
    description='Robotics Roadmap 2026: ROS 2 basics',
    license='MIT',
    entry_points={
        'console_scripts': [
            'fake_range_sensor = robot_basics.fake_range_sensor:main',
            'obstacle_monitor = robot_basics.obstacle_monitor:main',
            'safety_driver = robot_basics.safety_driver:main',
        ],
    },
)
