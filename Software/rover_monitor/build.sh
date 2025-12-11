# Add some commands:
#!/bin/bash

#gcc -O2 -o pi_oled_shutdown_monitor_2_2 pi_oled_shutdown_monitor_2_2.c ssd1306_driver.c ina260.c -lgpiod

#gcc -O2 -o pi_oled_shutdown_monitor_2_1 pi_oled_shutdown_monitor_2_1.c ina260.c -lgpiod

#gcc -O2 -o rover_monitor_10 rover_monitor_10.c ina260.c -lgpiod

code_to_build='gcc -O2 -o rover_monitor_12 rover_monitor_12.c os_calls.c ina260.c -lgpiod'

echo "Build: $code_to_build"
$code_to_build
