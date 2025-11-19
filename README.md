# rover-monitor
Rover monitor is a simple info display for the JPL's open-source-rover project
# Initial Rover Testing
![Alt text](https://github.com/jerryok826/rover-monitor/blob/main/images/outside_testing_2.png)

# Rover info monitor.
![Alt text](https://github.com/jerryok826/rover-monitor/blob/main/images/prototype_1.jpeg)

## Rover Monitor Project Description
I wanted a convenient way start/stop, shutdown and monitor the rover for problems. The board just uses the RPI's GPIO to drive the board with some C code. The code boots from startup and monitors the rover's battery voltage and current. It’s also convenient to have the rover's IP address for remote login. The V and I readings come from the rover's on board  ina260 via i2c. Been using the board for IP info and shutdown for about a month.

Features:
1. Local IP address display.
2. Rover shutdown button.
3. Rover ROS run and stop button.
4. Voltage and current monitoring with audio alarm on fault.

When an over voltage or an over current event happens the board will disable its output. The fault state is latched until the control buttons is pressed. Apond a fault an audio alarm is also actived and a fault LEDs is lite. The fault value is indicted by its red measurement value and the fault RED led is lite. A fault is cleared by pressing the adjusement button. The output button needs to be press again to reable the boards output.
 
### Project Status
The project is still working on sofware. Looking into using a large Oled and creating a pcb. 

![Alt text](https://github.com/jerryok826/rover-monitor/blob/main/images/oled_large.jpeg)

