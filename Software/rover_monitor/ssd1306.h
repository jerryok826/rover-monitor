#ifndef SSD1306_H
#define SSD1306_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT   64
#define SSD1306_BUF_SZ  (SSD1306_WIDTH * SSD1306_HEIGHT / 8)

/* Initialize SSD1306 on I2C (defaults: OLED_I2C_DEV=/dev/i2c-1, OLED_ADDR=0x3c).
 * Returns 0 on success, -1 on error.
 */
int  ssd1306_init(void);
void ssd1306_shutdown(void);

void ssd1306_clear(void);
void ssd1306_set_pixel(int x, int y, bool on);
void ssd1306_hline(int x0, int x1, int y, bool on);

/* Push framebuffer to display. Returns 0 on success, -1 on error. */
int  ssd1306_update(void);

/* Proportional 5x7-ish text helper used by rover_monitor.
 * Returns the x cursor after drawing.
 */
int  draw_text_prop(int x, int y, const char *s);

#ifdef __cplusplus
}
#endif

#endif
