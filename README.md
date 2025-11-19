# Rover Monitor
Rover monitor is a simple info display for the JPL's open-source-rover project
# My initial Rover Testing
![Alt text](https://github.com/jerryok826/rover-monitor/blob/main/images/outside_testing_2.png)

# Rover info monitor.
![Alt text](https://github.com/jerryok826/rover-monitor/blob/main/images/prototype_1.jpeg)

## Rover Monitor Project Description
I wanted a convenient way to start/stop, shutdown and monitor the rover for problems. The board just uses the RPI's GPIO to drive the board with some C code. The code boots from startup and monitors the rover's battery voltage and current. It’s also convenient to have the rover's IP address for remote login. The V and I readings come from the rover's on board ina260 via i2c. Been using the board for IP info and shutdown for about a month.

Features:
1. Local IP address display.
2. Rover shutdown button.
3. Rover ROS run and stop button.
4. Voltage and current monitoring with audio alarm on fault.

### Project Status
Still testing the software. Also looking into using a larger Oled display and creating a pcb. 

![Alt text](https://github.com/jerryok826/rover-monitor/blob/main/images/oled_large.jpeg)

![Alt text](https://github.com/jerryok826/rover-monitor/blob/main/images/rover_monitor_r2.jpg)


