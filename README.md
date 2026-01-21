# Rover Monitor
Rover monitor is a simple info display for the JPL's open-source-rover project
# My initial Rover Testing
![Alt text](https://github.com/jerryok826/rover-monitor/blob/main/images/rover_lawn_1.jpeg)

# Rover info monitor.
### Rover ROS2 Off
![Alt text](https://github.com/jerryok826/rover-monitor/blob/main/images/rover_ros2_off.jpeg)

### Rover ROS2 Running
![Alt text](https://github.com/jerryok826/rover-monitor/blob/main/images/rover_ros2_on.jpeg)

### Voltage fault: Below 12.0 V
![Alt text](https://github.com/jerryok826/rover-monitor/blob/main/images/voltage_fault.jpeg)

## Rover Monitor Project Description
I wanted a convenient way to start/stop, shutdown and monitor the rover for problems. The board just uses the RPI's GPIO to drive the board with some C code. The code boots from startup and monitors the rover's battery voltage and current. It’s also convenient to have the rover's hostname for remote login. The V and I readings come from the rover's on board ina260 via i2c. Been using the board for login info and shutdown for about a month.

Monitor Features:
1. Local login info.
2. Rover shutdown button.
3. Rover ROS run and stop button.
4. Voltage and current monitoring with audio alarm on fault.
5. Monitor beeps when system has complteted booting and ready for operation.

### Project Status
Basically complete, except for a fault event detail line to the display. Below is the original prototype version.

![Alt text](https://github.com/jerryok826/rover-monitor/blob/main/images/prototype_1.jpeg)



