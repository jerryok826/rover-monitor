/*
 * ssd1306.c - Minimal SSD1306 128x64 I2C framebuffer driver
 * Extracted from rover_monitor_12.c and made reusable.
 */

#include "ssd1306.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef OLED_I2C_DEV
#define OLED_I2C_DEV   "/dev/i2c-1"
#endif

#ifndef OLED_ADDR
#define OLED_ADDR      0x3c
#endif

static int i2c_fd = -1;
static uint8_t oled_buf[SSD1306_BUF_SZ];


//static int i2c_fd = -1;
// static uint8_t oled_buf[SSD1306_BUF_SZ];

static int
i2c_open_dev (const char *dev, int addr)
{
  int fd = open (dev, O_RDWR);
  if (fd < 0)
    return -1;
  if (ioctl (fd, I2C_SLAVE, addr) < 0) {
    close (fd);
    return -1;
  }
  return fd;
}

static int
ssd1306_cmd (uint8_t c)
{
  uint8_t buf[2] = { 0x00, c }; // control byte 0x00 = command
  if (write (i2c_fd, buf, 2) != 2)
    return -1;
  return 0;
}

static int
ssd1306_cmd2 (uint8_t c1, uint8_t c2)
{
  if (ssd1306_cmd (c1) < 0)
    return -1;
  if (ssd1306_cmd (c2) < 0)
    return -1;
  return 0;
}

static int
ssd1306_data (const uint8_t *data, size_t len)
{
  // Send as chunks prefixed with 0x40 control byte.
  uint8_t chunk[17];
  chunk[0] = 0x40;
  while (len > 0) {
    size_t n = len > 16 ? 16 : len;
    memcpy (&chunk[1], data, n);
    if (write (i2c_fd, chunk, n + 1) != (ssize_t) (n + 1))
      return -1;
    data += n;
    len -= n;
  }
  return 0;
}

int
ssd1306_init (void)
{
  if ((i2c_fd = i2c_open_dev (OLED_I2C_DEV, OLED_ADDR)) < 0) {
    perror ("i2c_open_dev");
    return -1;
  }
  // Init sequence (typical)
  if (ssd1306_cmd (0xAE) < 0)
    return -1;                  // display off
  if (ssd1306_cmd2 (0xD5, 0x80) < 0)
    return -1;                  // clock divide
  if (ssd1306_cmd2 (0xA8, 0x3F) < 0)
    return -1;                  // multiplex
  if (ssd1306_cmd2 (0xD3, 0x00) < 0)
    return -1;                  // display offset
  if (ssd1306_cmd (0x40) < 0)
    return -1;                  // start line
  if (ssd1306_cmd2 (0x8D, 0x14) < 0)
    return -1;                  // charge pump
  if (ssd1306_cmd2 (0x20, 0x00) < 0)
    return -1;                  // memory mode: horizontal

  // rotated the disply around
  if (ssd1306_cmd (0xA1) < 0)
    return -1;                  // seg remap, cmd 0xA0 sets the segment remap to normal, while 0xA1 reverses it.
  if (ssd1306_cmd (0xC8) < 0)
    return -1;                  // COM scan dec, cmd 0xC0 sets the scan direction to normal, while 0xC8 reverses it.

  if (ssd1306_cmd2 (0xDA, 0x12) < 0)
    return -1;                  // compins
  if (ssd1306_cmd2 (0x81, 0xCF) < 0)
    return -1;                  // contrast
  if (ssd1306_cmd2 (0xD9, 0xF1) < 0)
    return -1;                  // precharge
  if (ssd1306_cmd2 (0xDB, 0x40) < 0)
    return -1;                  // vcom detect
  if (ssd1306_cmd (0xA4) < 0)
    return -1;                  // entire display on (resume)
  if (ssd1306_cmd (0xA6) < 0)
    return -1;                  // normal display

  if (ssd1306_cmd (0xAF) < 0)
    return -1;                  // display on
  memset (oled_buf, 0x00, sizeof (oled_buf));
  return 0;
}

void
ssd1306_clear (void)
{
  memset (oled_buf, 0x00, sizeof (oled_buf));
}

void
ssd1306_set_pixel (int x, int y, bool on)
{
  if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT)
    return;
  size_t idx = (y / 8) * SSD1306_WIDTH + x;
  uint8_t bit = 1 << (y & 7);
  if (on)
    oled_buf[idx] |= bit;
  else
    oled_buf[idx] &= ~bit;
}

void
ssd1306_hline (int x, int y, int w, bool on)
{
  for (int i = 0; i < w; ++i)
    ssd1306_set_pixel (x + i, y, on);
}

int
ssd1306_update (void)
{
  if (ssd1306_cmd2 (0x21, 0x00) < 0)
    return -1;                  // set column addr: start 0
  if (ssd1306_cmd (0x7F) < 0)
    return -1;                  // end 127
  if (ssd1306_cmd2 (0x22, 0x00) < 0)
    return -1;                  // set page addr: start 0
  if (ssd1306_cmd (0x07) < 0)
    return -1;                  // end 7
  return ssd1306_data (oled_buf, sizeof (oled_buf));
}

// 5x7 Font (ASCII 32..127), each char 5 columns, LSB = top pixel.
// (Shortened comment; full table included.)
static const uint8_t font5x7[96][5] = {
  {0x00, 0x00, 0x00, 0x00, 0x00},       // 32 ' '
  {0x00, 0x00, 0x5F, 0x00, 0x00},       // 33 '!'
  {0x00, 0x07, 0x00, 0x07, 0x00},       // 34 '"'
  {0x14, 0x7F, 0x14, 0x7F, 0x14},       // 35 '#'
  {0x24, 0x2A, 0x7F, 0x2A, 0x12},       // 36 '$'
  {0x23, 0x13, 0x08, 0x64, 0x62},       // 37 '%'
  {0x36, 0x49, 0x55, 0x22, 0x50},       // 38 '&'
  {0x00, 0x05, 0x03, 0x00, 0x00},       // 39 '''
  {0x00, 0x1C, 0x22, 0x41, 0x00},       // 40 '('
  {0x00, 0x41, 0x22, 0x1C, 0x00},       // 41 ')'
  {0x14, 0x08, 0x3E, 0x08, 0x14},       // 42 '*'
  {0x08, 0x08, 0x3E, 0x08, 0x08},       // 43 '+'
  {0x00, 0x50, 0x30, 0x00, 0x00},       // 44 ','
  {0x08, 0x08, 0x08, 0x08, 0x08},       // 45 '-'
  {0x00, 0x60, 0x60, 0x00, 0x00},       // 46 '.'
  {0x20, 0x10, 0x08, 0x04, 0x02},       // 47 '/'
  {0x3E, 0x51, 0x49, 0x45, 0x3E},       // 48 '0'
  {0x00, 0x42, 0x7F, 0x40, 0x00},       // 49 '1'
  {0x42, 0x61, 0x51, 0x49, 0x46},       // 50 '2'
  {0x21, 0x41, 0x45, 0x4B, 0x31},       // 51 '3'
  {0x18, 0x14, 0x12, 0x7F, 0x10},       // 52 '4'
  {0x27, 0x45, 0x45, 0x45, 0x39},       // 53 '5'
  {0x3C, 0x4A, 0x49, 0x49, 0x30},       // 54 '6'
  {0x01, 0x71, 0x09, 0x05, 0x03},       // 55 '7'
  {0x36, 0x49, 0x49, 0x49, 0x36},       // 56 '8'
  {0x06, 0x49, 0x49, 0x29, 0x1E},       // 57 '9'
  {0x00, 0x36, 0x36, 0x00, 0x00},       // 58 ':'
  {0x00, 0x56, 0x36, 0x00, 0x00},       // 59 ';'
  {0x08, 0x14, 0x22, 0x41, 0x00},       // 60 '<'
  {0x14, 0x14, 0x14, 0x14, 0x14},       // 61 '='
  {0x00, 0x41, 0x22, 0x14, 0x08},       // 62 '>'
  {0x02, 0x01, 0x51, 0x09, 0x06},       // 63 '?'
  {0x32, 0x49, 0x79, 0x41, 0x3E},       // 64 '@'
  {0x7E, 0x11, 0x11, 0x11, 0x7E},       // 65 'A'
  {0x7F, 0x49, 0x49, 0x49, 0x36},       // 66 'B'
  {0x3E, 0x41, 0x41, 0x41, 0x22},       // 67 'C'
  {0x7F, 0x41, 0x41, 0x22, 0x1C},       // 68 'D'
  {0x7F, 0x49, 0x49, 0x49, 0x41},       // 69 'E'
  {0x7F, 0x09, 0x09, 0x09, 0x01},       // 70 'F'
  {0x3E, 0x41, 0x49, 0x49, 0x7A},       // 71 'G'
  {0x7F, 0x08, 0x08, 0x08, 0x7F},       // 72 'H'
  {0x00, 0x41, 0x7F, 0x41, 0x00},       // 73 'I'
  {0x20, 0x40, 0x41, 0x3F, 0x01},       // 74 'J'
  {0x7F, 0x08, 0x14, 0x22, 0x41},       // 75 'K'
  {0x7F, 0x40, 0x40, 0x40, 0x40},       // 76 'L'
  {0x7F, 0x02, 0x0C, 0x02, 0x7F},       // 77 'M'
  {0x7F, 0x04, 0x08, 0x10, 0x7F},       // 78 'N'
  {0x3E, 0x41, 0x41, 0x41, 0x3E},       // 79 'O'
  {0x7F, 0x09, 0x09, 0x09, 0x06},       // 80 'P'
  {0x3E, 0x41, 0x51, 0x21, 0x5E},       // 81 'Q'
  {0x7F, 0x09, 0x19, 0x29, 0x46},       // 82 'R'
  {0x46, 0x49, 0x49, 0x49, 0x31},       // 83 'S'
  {0x01, 0x01, 0x7F, 0x01, 0x01},       // 84 'T'
  {0x3F, 0x40, 0x40, 0x40, 0x3F},       // 85 'U'
  {0x1F, 0x20, 0x40, 0x20, 0x1F},       // 86 'V'
  {0x3F, 0x40, 0x38, 0x40, 0x3F},       // 87 'W'
  {0x63, 0x14, 0x08, 0x14, 0x63},       // 88 'X'
  {0x07, 0x08, 0x70, 0x08, 0x07},       // 89 'Y'
  {0x61, 0x51, 0x49, 0x45, 0x43},       // 90 'Z'
  {0x00, 0x7F, 0x41, 0x41, 0x00},       // 91 '['
  {0x02, 0x04, 0x08, 0x10, 0x20},       // 92 backslash
  {0x00, 0x41, 0x41, 0x7F, 0x00},       // 93 ']'
  {0x04, 0x02, 0x01, 0x02, 0x04},       // 94 '^'
  {0x80, 0x80, 0x80, 0x80, 0x80},       // 95 '_'
  {0x00, 0x01, 0x02, 0x04, 0x00},       // 96 '`'
  {0x20, 0x54, 0x54, 0x54, 0x78},       // 97 'a'
  {0x7F, 0x48, 0x44, 0x44, 0x38},       // 98 'b'
  {0x38, 0x44, 0x44, 0x44, 0x20},       // 99 'c'
  {0x38, 0x44, 0x44, 0x48, 0x7F},       // 100 'd'
  {0x38, 0x54, 0x54, 0x54, 0x18},       // 101 'e'
  {0x08, 0x7E, 0x09, 0x01, 0x02},       // 102 'f'
  {0x0C, 0x52, 0x52, 0x52, 0x3E},       // 103 'g'
  {0x7F, 0x08, 0x04, 0x04, 0x78},       // 104 'h'
  {0x00, 0x44, 0x7D, 0x40, 0x00},       // 105 'i'
  {0x20, 0x40, 0x44, 0x3D, 0x00},       // 106 'j'
  {0x7F, 0x10, 0x28, 0x44, 0x00},       // 107 'k'
  {0x00, 0x41, 0x7F, 0x40, 0x00},       // 108 'l'
  {0x7C, 0x04, 0x18, 0x04, 0x78},       // 109 'm'
  {0x7C, 0x08, 0x04, 0x04, 0x78},       // 110 'n'
  {0x38, 0x44, 0x44, 0x44, 0x38},       // 111 'o'
  {0x7C, 0x14, 0x14, 0x14, 0x08},       // 112 'p'
  {0x08, 0x14, 0x14, 0x14, 0x7C},       // 113 'q'
  {0x7C, 0x08, 0x04, 0x04, 0x08},       // 114 'r'
  {0x48, 0x54, 0x54, 0x54, 0x20},       // 115 's'
  {0x04, 0x3F, 0x44, 0x40, 0x20},       // 116 't'
  {0x3C, 0x40, 0x40, 0x20, 0x7C},       // 117 'u'
  {0x1C, 0x20, 0x40, 0x20, 0x1C},       // 118 'v'
  {0x3C, 0x40, 0x30, 0x40, 0x3C},       // 119 'w'
  {0x44, 0x28, 0x10, 0x28, 0x44},       // 120 'x'
  {0x0C, 0x50, 0x50, 0x50, 0x3C},       // 121 'y'
  {0x44, 0x64, 0x54, 0x4C, 0x44},       // 122 'z'
  {0x08, 0x36, 0x41, 0x41, 0x00},       // 123 '{'
  {0x00, 0x00, 0x7F, 0x00, 0x00},       // 124 '|'
  {0x00, 0x41, 0x41, 0x36, 0x08},       // 125 '}'
  {0x08, 0x04, 0x08, 0x10, 0x08}        // 126 '~'
};

// Compute proportional width by trimming empty columns
static int
glyph_width (uint8_t glyph[5])
{
  int left = 0, right = 4;
  while (left < 5) {
    if (glyph[left])
      break;
    left++;
  }
  while (right >= 0) {
    if (glyph[right])
      break;
    right--;
  }
  int w = right - left + 1;
  if (w <= 0)
    w = 1;
  if (w > 5)
    w = 5;
  return w;
}

static void
draw_char_prop (int x, int y, char c)
{
  if (c < 32 || c > 127)
    c = '?';
  const uint8_t *g = font5x7[c - 32];
  // Determine visible columns
  int left = 0, right = 4;
  while (left < 5 && g[left] == 0)
    left++;
  while (right >= 0 && g[right] == 0)
    right--;
  if (right < left) {
    right = left;
  }                             // at least 1 col
  int w = right - left + 1;
  for (int col = 0; col < w; ++col) {
    uint8_t bits = g[left + col];
    for (int row = 0; row < 7; ++row) {
      bool on = (bits >> row) & 1;
      ssd1306_set_pixel (x + col, y + row, on);
    }
  }
}

int
draw_text_prop (int x, int y, const char *s)
{
  int cursor = x;
  for (; *s; ++s) {
    char c = *s;
    if (c == '\n') {
      y += 10;
      cursor = x;
      continue;
    }
    if (c < 32 || c > 127)
      c = '?';
    uint8_t glyph[5];
    memcpy (glyph, font5x7[c - 32], 5);
    // draw
    draw_char_prop (cursor, y, c);
    int w = glyph_width (glyph);
    cursor += w + 1;            // 1px spacing
    if (cursor >= SSD1306_WIDTH)
      break;
  }
  return cursor;
}

void
ssd1306_shutdown (void)
{
  if (i2c_fd >= 0) {
    close(i2c_fd);
    i2c_fd = -1;
  }
}

#if 0
/*
 * Tiny unit-test main() for ssd1306.c
 *
 * Enable by changing #if 0 -> #if 1, then build:
 *   gcc -O2 -Wall -Wextra -o ssd1306_test ssd1306.c
 *
 * It will draw a border + some text for a few seconds, then blink.
 */
#include <stdio.h>
#include <unistd.h>

int main(void)
{
  printf("SSD1306 unit test start\n");

  if (ssd1306_init() < 0) {
    fprintf(stderr, "ssd1306_init() failed\n");
    return 1;
  }

  ssd1306_clear();
  ssd1306_update();
  sleep(1);

  // Border
  for (int x = 0; x < SSD1306_WIDTH; x++) {
    ssd1306_set_pixel(x, 0, true);
    ssd1306_set_pixel(x, SSD1306_HEIGHT - 1, true);
  }
  for (int y = 0; y < SSD1306_HEIGHT; y++) {
    ssd1306_set_pixel(0, y, true);
    ssd1306_set_pixel(SSD1306_WIDTH - 1, y, true);
  }

  draw_text_prop(8, 8, "SSD1306");
  draw_text_prop(8, 22, "UNIT TEST");
  draw_text_prop(8, 36, "OK");
  ssd1306_update();
  sleep(3);

  for (int i = 0; i < 3; i++) {
    ssd1306_clear();
    ssd1306_update();
    usleep(200000);

    draw_text_prop(20, 24, "BLINK");
    ssd1306_update();
    usleep(200000);
  }

  ssd1306_shutdown();
  printf("SSD1306 unit test complete\n");
  return 0;
}
#endif
