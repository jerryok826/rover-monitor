#!/bin/bash

# Sourcing the ROS environment
#source /opt/ros/jazzy/setup.bash

#ros2 launch osr_bringup osr_launch.py #&

#pkill -9 -f  roboclaw_wrapper servo_wrapper rover teleop_twist_joy ina260_node joy 
pkill -9 -f  ros2
pkill -9 -f  roboclaw_wrapper 
pkill -9 -f  servo_wrapper
pkill -9 -f  teleop_twist_joy
pkill -9 -f  ina260_node 
pkill -9 -f  joy
#pkill -9 -f  rover



