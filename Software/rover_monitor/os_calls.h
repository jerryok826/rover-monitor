#ifndef OS_CALLS_H
#define OS_CALLS_H

#include <stdint.h>

// Function prototypes
int start_rover(void);
int stop_rover(void);
int os_reboot(void);
int os_shutdown(void);

#endif
