#pragma once
/*
 * rover_pin_drv.h - simple GPIO output driver using libgpiod
 *
 * This module owns three output GPIO lines (green + red + buzzer) on an already-open
 * gpiod_chip. It provides init/set/shutdown helpers.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations to avoid forcing <gpiod.h> on all includers */
struct gpiod_chip;

/*
 * gpio_output_drv_init()
 *   chip:     open gpiod chip (e.g., gpiod_chip_open_by_name("gpiochip0"))
 *   green_pin BCM line number for green LED
 *   red_pin   BCM line number for red LED
 *   buzzer_pin   BCM line number for buzzer LED
 *   consumer  consumer string shown in gpiod tools (may be NULL)
 *   initial_on: if non-zero, drive both outputs high initially; else low
 *
 * Returns 0 on success, -1 on error (errno set by libgpiod).
 */
int  rover_pin_drv_init(struct gpiod_chip *chip, int green_pin, int red_pin, int buzzer_pin,
               const char *consumer, int initial_on);

/* Set individual LEDs: val!=0 -> on (drive high), val==0 -> off (drive low) */
void rover_pin_drv_set_green(int val);
void rover_pin_drv_set_red(int val);
void rover_pin_drv_set_buzzer(int val);

/* Convenience helpers */
void rover_pin_drv_all_off(void);
void rover_pin_drv_all_on(void);

/* Release owned GPIO lines */
void rover_pin_drv_shutdown(void);

#ifdef __cplusplus
}
#endif
