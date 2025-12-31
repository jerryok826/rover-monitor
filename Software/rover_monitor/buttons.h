/* buttons.h
 *
 * Public interface for libgpiod v1.6.3 button helper.
 * Raspberry Pi: BCM GPIO numbers (gpiod offsets), pull-up wiring (idle=1, press=0),
 * press-only (FALLING), debounce + release gate.
 */

#ifndef BUTTONS_H
#define BUTTONS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*button_cb_t)(int pin_num);

/* Initialize with exactly two BCM GPIO pins (gpiod offsets), e.g. 19, 21.
 * Returns 0 on success, -1 on error.
 */
int buttons_init(int pin_num1, int pin_num2);

/* Register/replace callback for a pin. cb may be NULL to disable.
 * Returns 0 on success, -1 on error.
 */
int button_callback(int pin_num, button_cb_t cb);

/* Stop threads, release lines, close chip.
 * Safe to call even if not initialized (returns 0).
 * Returns 0 on success, -1 on error.
 */
int buttons_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* BUTTONS_H */
