/*
 * rover_pin_drv_driver.c - simple red/green LED driver using libgpiod
 */

#include <errno.h>
#include <stdio.h>
#include <gpiod.h>

#include "rover_pin_drv.h"

static struct gpiod_line *s_green  = NULL;
static struct gpiod_line *s_red    = NULL;
static struct gpiod_line *s_buzzer = NULL;

int
rover_pin_drv_init(struct gpiod_chip *chip, int green_pin, int red_pin,int buzzer_pin,
          const char *consumer, int initial_on)
{
  const char *cons = consumer ? consumer : "led_driver";

  if (!chip) {
    errno = EINVAL;
    return -1;
  }

  /* If re-init called, clean up first */
  rover_pin_drv_shutdown();

  s_green = gpiod_chip_get_line(chip, green_pin);
  if (!s_green) {
    perror("rover_pin_drv_init: get_green_line");
    return -1;
  }
  if (gpiod_line_request_output(s_green, cons, initial_on ? 1 : 0) < 0) {
    perror("rover_pin_drv_init: request_output(green)");
    s_green = NULL;
    return -1;
  }

  s_red = gpiod_chip_get_line(chip, red_pin);
  if (!s_red) {
    perror("rover_pin_drv_init: get_red_line");
    rover_pin_drv_shutdown();
    return -1;
  }
  if (gpiod_line_request_output(s_red, cons, initial_on ? 1 : 0) < 0) {
    perror("rover_pin_drv_init: request_output(red)");
    rover_pin_drv_shutdown();
    return -1;
  }

  s_buzzer = gpiod_chip_get_line(chip, buzzer_pin);
  if (!s_buzzer) {
    perror("rover_pin_drv_init: get_buzzer_line");
    rover_pin_drv_shutdown();
    return -1;
  }
  if (gpiod_line_request_output(s_buzzer, cons, initial_on ? 1 : 0) < 0) {
    perror("rover_pin_drv_init: request_output(buzzer)");
    rover_pin_drv_shutdown();
    return -1;
  }

  return 0;
}

void
rover_pin_drv_set_green(int val)
{
  if (s_green)
    (void)gpiod_line_set_value(s_green, val ? 1 : 0);
}

void
rover_pin_drv_set_red(int val)
{
  if (s_red)
    (void)gpiod_line_set_value(s_red, val ? 1 : 0);
}

void
rover_pin_drv_set_buzzer(int val)
{
  if (s_buzzer)
    (void)gpiod_line_set_value(s_buzzer, val ? 1 : 0);
}

void
rover_pin_drv_all_off(void)
{
  rover_pin_drv_set_green(0);
  rover_pin_drv_set_red(0);
  rover_pin_drv_set_buzzer(0);
}

void
rover_pin_drv_all_on(void)
{
  rover_pin_drv_set_green(1);
  rover_pin_drv_set_red(1);
  rover_pin_drv_set_buzzer(1);
}

void
rover_pin_drv_shutdown(void)
{
  if (s_green) {
    gpiod_line_release(s_green);
    s_green = NULL;
  }
  if (s_red) {
    gpiod_line_release(s_red);
    s_red = NULL;
  }
  if (s_buzzer) {
    gpiod_line_release(s_buzzer);
    s_buzzer = NULL;
  }
}


#if 0
/*
 * ------------------------------------------------------------
 * Minimal unit-test main() for rover_pin_drv.c
 *
 * This is intended as a quick sanity test on a Raspberry Pi with
 * libgpiod installed and the chosen GPIOs wired to LEDs (through
 * appropriate resistors / driver circuitry as required).
 *
 * Enable by temporarily changing "#if 0" to "#if 1" OR compile
 * this file alone with your preferred flags.
 *
 * Example:
 *   gcc -O2 -Wall -Wextra -o rover_pin_drv_test rover_pin_drv.c -lgpiod
 *
 * Usage:
 *   ./rover_pin_drv_test [gpiochip_name] [green_pin] [red_pin][buzzer_pin]
 *
 * Defaults:
 *   gpiochip_name = "gpiochip0"
 *   green_pin     = 13
 *   red_pin       = 20 
 *   buzzer_pin    = 16
 * 
 * ------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gpiod.h>

int main(int argc, char **argv)
{
  const char *chip_name = (argc > 1) ? argv[1] : "gpiochip0";
  int green_pin  = (argc > 2) ? atoi(argv[2]) : 13;
  int red_pin    = (argc > 3) ? atoi(argv[3]) : 20;
  int buzzer_pin = (argc > 4) ? atoi(argv[4]) : 16;

  printf("LED unit test: chip=%s green=%d red=%d buzzer=%d\n", chip_name, green_pin, red_pin, buzzer_pin);

  struct gpiod_chip *chip = gpiod_chip_open_by_name(chip_name);
  if (!chip) {
    perror("gpiod_chip_open_by_name");
    return 1;
  }

  if (rover_pin_drv_init(chip, green_pin, red_pin, buzzer_pin, "led_test", 0) < 0) {
    fprintf(stderr, "leds_init() failed\n");
    gpiod_chip_close(chip);
    return 1;
  }

  /* Blink pattern: green, red, both, off */
  for (int i = 0; i < 10; i++) {
    rover_pin_drv_all_off(); usleep(150000);

    rover_pin_drv_set_green(1); rover_pin_drv_set_red(0); rover_pin_drv_set_buzzer(1); usleep(250000);
    rover_pin_drv_set_green(0); rover_pin_drv_set_red(1); rover_pin_drv_set_buzzer(1); usleep(250000);
    rover_pin_drv_set_green(1); rover_pin_drv_set_red(1); rover_pin_drv_set_buzzer(1); usleep(250000);
    rover_pin_drv_all_off(); usleep(350000);
  }

  rover_pin_drv_shutdown();
  gpiod_chip_close(chip);

  printf("rover_pin_drv unit test complete\n");
  return 0;
}
#endif
