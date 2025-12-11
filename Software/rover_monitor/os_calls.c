

#include <stdlib.h>
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

    const char* ros2_stop_cmd = "pkill -9 -f 'launch|roboclaw_wrapper|servo_control|rover|joy_node|ina260'";
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
