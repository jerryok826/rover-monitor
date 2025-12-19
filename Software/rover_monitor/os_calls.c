

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "os_calls.h"

int start_rover(void)
{
    int system_call_status = 0;
    const char* ros2_start_cmd = "/bin/bash -c 'source /opt/ros/jazzy/setup.bash; ros2 launch osr_bringup osr_launch.py &'";

    system_call_status = system(ros2_start_cmd);
    return system_call_status;
}

int stop_rover(void)
{
    int system_call_status = 0;

// This worked
// sudo pkill -9 -f 'launch|roboclaw_wrapper|servo_control|rover|joy_node|ina260'

#if 0
pkill -9 -f  ros2
pkill -9 -f  roboclaw_wrapper 
pkill -9 -f  servo_wrapper
pkill -9 -f  teleop_twist_joy
pkill -9 -f  ina260_node 
pkill -9 -f  joy
#pkill -9 -f  rover
pkill -9 -f  osr_control
#endif

//    const char* ros2_stop_cmd = "pkill -9 -f 'launch|roboclaw_wrapper|servo_control|rover|joy_node|ina260'";
    const char* ros2_stop_cmd = "pkill -9 -f 'ros2|roboclaw_wrapper|servo_wrapper|teleop_twist_joy|ina260_node|joy|osr_control'";

    system_call_status = system(ros2_stop_cmd);

    return system_call_status;
}

int os_shutdown(void) {
    int system_call_status = system("shutdown -h now");
    return system_call_status;
}

int os_reboot(void) {
    int  system_call_status = system("reboot");
    return system_call_status;
}

/**
 * Checks if the current hardware is a Raspberry Pi.
 * Returns 1 if true, 0 otherwise.
 */
int is_raspberry_pi() {
    FILE *fp = fopen("/proc/device-tree/model", "r");
    if (fp == NULL) {
        return 0; // File doesn't exist; likely not a Pi or not Linux
    }

    char model[128];
    if (fgets(model, sizeof(model), fp) != NULL) {
        fclose(fp);
        // Check if the string starts with "Raspberry Pi"
        return (strncmp(model, "Raspberry Pi", 12) == 0);
    }

    fclose(fp);
    return 0;
}

// Some test code
#if 0
// gcc -O2 os_calls.c -o os_calls_test

int main(){

    if (is_raspberry_pi()) {
        printf("Running on a Raspberry Pi.\n");
    } else {
        printf("Not running on a Raspberry Pi.\n");
    }

  stop_rover();

  return 0; 
}
#endif
