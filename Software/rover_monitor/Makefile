# Define the source files and the output executable name
TARGET    = rover_monitor_12
# SOURCES   = rover_monitor_12.c ina260.c os_calls.c 
SOURCES   = rover_monitor_main.c ina260.c os_calls.c ssd1306.c rover_pin_drv.c

CC        = gcc
CFLAGS    = -O2
LIBS      = -lgpiod

# The default target
all: $(TARGET)

# Rule to build the target
$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -g -o $@ $(SOURCES) $(LIBS)
	@echo "Build: $(CC) $(CFLAGS) -o $@ $(SOURCES) $(LIBS)"

# Clean the generated executable
clean:
	rm -f $(TARGET)

.PHONY: all clean
