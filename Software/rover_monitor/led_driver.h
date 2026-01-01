#pragma once
/*
 * led_driver.h - simple red/green LED driver using libgpiod
 *
 * This module owns two output GPIO lines (green + red) on an already-open
 * gpiod_chip. It provides init/set/shutdown helpers.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations to avoid forcing <gpiod.h> on all includers */
struct gpiod_chip;

/*
 * leds_init()
 *   chip:     open gpiod chip (e.g., gpiod_chip_open_by_name("gpiochip0"))
 *   green_pin BCM line number for green LED
 *   red_pin   BCM line number for red LED
 *   consumer  consumer string shown in gpiod tools (may be NULL)
 *   initial_on: if non-zero, drive both outputs high initially; else low
 *
 * Returns 0 on success, -1 on error (errno set by libgpiod).
 */
int  leds_init(struct gpiod_chip *chip, int green_pin, int red_pin, int buzzer_pin,
               const char *consumer, int initial_on);

/* Set individual LEDs: val!=0 -> on (drive high), val==0 -> off (drive low) */
void leds_set_green(int val);
void leds_set_red(int val);
void leds_set_buzzer(int val);

/* Convenience helpers */
void leds_all_off(void);
void leds_all_on(void);

/* Release owned GPIO lines */
void leds_shutdown(void);

#ifdef __cplusplus
}
#endif
