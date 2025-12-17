//
// Combines an SSD1306 128x64 I2C status display (IP/SSID/CPU Temp/Uptime)
// with a shutdown button on GPIO 19 and a status LED on GPIO 13 using libgpiod.
//
// Wiring (BCM numbering):
//   - OLED Bonnet (SSD1306) on I2C-1 (SDA/SCL) @ 0x3C
//   - Shutdown button: GPIO19 to GND (active-low). Use a physical pull-up or internal if configured.
//   - Status LED: GPIO13 + series resistor to 3V3 (active-high) or to GND (active-low; invert in code)
//
// Build:
//   gcc -O2 -o rover_monitor_12 rover_monitor_12.c os_calls.c ina260.c -lgpiod'
//
//  sudo cp pi_oled_shutdown_monitor_2 /usr/local/bin
//
//  sudo nano  /etc/systemd/system/ip2oled_monitor_bonnet.service
//
// Run (needs GPIO + I2C access):
//   sudo ./pi_oled_shutdown_monitor
//
// sudo systemctl stop  ip2oled_monitor_bonnet.service
// sudo cp pi_oled_shutdown_monitor_2  /usr/local/bin
// sudo systemctl start  ip2oled_monitor_bonnet.service
//
// systemd service example is provided after the code.


// indent -gnu -br -cli2 -lp -nut -l100 rover_monitor_12.c

#define _GNU_SOURCE
#include <math.h>
#include <gpiod.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <linux/i2c-dev.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>             // For sleep() in the main thread
#include <stdatomic.h>          // Required for atomic operations
#include <pthread.h>            // Required for pthreads
#include "ina260.h"
#include "os_calls.h"
#define VOLATGE_HIGH_LIMIT (16000.0)    // 16 volts
#define VOLATGE_LOW_LIMIT  (12000.0)    // 12 volts
#define CURRENT_HIGH_LIMIT  (7000.0)    // 7 amps
#define OLED_I2C_DEV   "/dev/i2c-1"
#define OLED_ADDR      0x3c     // 0x3C
#define CHIPNAME       "gpiochip0"
#define SHUTDOWN_BUTTON_PIN 19
#define GREEN_LED_PIN       13
#define RED_LED_PIN         20
#define ALARM_PIN           16
#define RUN_STOP_BUTTON_PIN 21
static int i2c_ina260_fd;
static int ina260_online = 0;

// ======== Simple logging ========
static void
simple_logf (const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  vfprintf (stdout, fmt, ap);
  fprintf (stdout, "\n");
  fflush (stdout);
  va_end (ap);
}

// ======== OLED / SSD1306 minimal driver ========
#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT   64
#define SSD1306_BUF_SZ  (SSD1306_WIDTH * SSD1306_HEIGHT / 8)

static int i2c_fd = -1;
static uint8_t oled_buf[SSD1306_BUF_SZ];

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

static int
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

static void
ssd1306_clear (void)
{
  memset (oled_buf, 0x00, sizeof (oled_buf));
}

static void
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

static void
ssd1306_hline (int x, int y, int w, bool on)
{
  for (int i = 0; i < w; ++i)
    ssd1306_set_pixel (x + i, y, on);
}

static int
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

static int
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

// ======== System info helpers ========
static int
get_hostname (char *out, size_t outlen)
{
  int rc = -1;

  memset(out,0,outlen);

  // Call gethostname() to retrieve the name
  if (gethostname(out, outlen) == 0) {
//       printf("Local Hostname: %s\n", out);
        rc = 0;
  } else {
        // If gethostname fails, print an error message
        perror("gethostname failed");
        return rc;
  }
  return rc;
}

static int
get_ip_address (char *out, size_t outlen)
{
  struct ifaddrs *ifaddr, *ifa;
  if (getifaddrs (&ifaddr) == -1)
    return -1;
  int rc = -1;
  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr)
      continue;
    if (ifa->ifa_addr->sa_family == AF_INET) {
      unsigned int flags = ifa->ifa_flags;
      if (!(flags & IFF_UP) || (flags & IFF_LOOPBACK))
        continue;
      struct sockaddr_in *sa = (struct sockaddr_in *) ifa->ifa_addr;
      if (inet_ntop (AF_INET, &sa->sin_addr, out, outlen)) {
        rc = 0;
        break;
      }
    }
  }
  freeifaddrs (ifaddr);
  return rc;
}

static int
get_wifi_ssid (char *out, size_t outlen)
{
  // Use iwgetid -r if available (simple + robust)
  FILE *fp = popen ("iwgetid -r 2>/dev/null", "r");
  if (!fp)
    return -1;
  if (!fgets (out, (int) outlen, fp)) {
    pclose (fp);
    return -1;
  }
  pclose (fp);
  // strip newline
  size_t n = strlen (out);
  while (n && (out[n - 1] == '\n' || out[n - 1] == '\r')) {
    out[--n] = 0;
  }
  if (n == 0)
    return -1;
  return 0;
}

static int
get_cpu_temp_c (double *outC)
{
  FILE *f = fopen ("/sys/class/thermal/thermal_zone0/temp", "r");
  if (!f)
    return -1;
  long t = 0;
  if (fscanf (f, "%ld", &t) != 1) {
    fclose (f);
    return -1;
  }
  fclose (f);
  *outC = t / 1000.0;
  return 0;
}

static void
fmt_uptime (char *out, size_t outlen)
{
  // Read from /proc/uptime
  FILE *f = fopen ("/proc/uptime", "r");
  double up = 0.0;
  if (f) {
    int i = fscanf (f, "%lf", &up);
    fclose (f);
  }
  unsigned long s = (unsigned long) (up + 0.5);
  unsigned long d = s / 86400;
  s %= 86400;
  unsigned long h = s / 3600;
  s %= 3600;
  unsigned long m = s / 60;
  snprintf (out, outlen, "%lud %02lu:%02lu", d, h, m);
}

static void
get_ina260_status (float *voltage_mv, float *current_ma)
{
  *voltage_mv = ina260_read_voltage_mV (i2c_ina260_fd);
  *current_ma = ina260_read_current_mA (i2c_ina260_fd);
}

// ======== GPIO / shutdown handling ========
static volatile sig_atomic_t keepRunning = 1;

static void
sigint_handler (int sig)
{
  (void) sig;
  keepRunning = 0;
}

static struct gpiod_chip *chip = NULL;
static struct gpiod_line *btn_line = NULL;
static struct gpiod_line *rs_btn_line = NULL;
static struct gpiod_line *green_led_line = NULL;
static struct gpiod_line *red_led_line = NULL;
static struct gpiod_line *bell_line = NULL;

static int
gpio_init (void)
{
  chip = gpiod_chip_open_by_name (CHIPNAME);
  if (!chip) {
    perror ("gpiod_chip_open_by_name");
    return -1;
  }

  btn_line = gpiod_chip_get_line (chip, SHUTDOWN_BUTTON_PIN);
  if (!btn_line) {
    perror ("get_line(button)");
    return -1;
  }
  if (gpiod_line_request_both_edges_events (btn_line, "pi_oled_shutdown_monitor") < 0) {
    perror ("request_both_edges_events(button)");
    return -1;
  }

  rs_btn_line = gpiod_chip_get_line (chip, RUN_STOP_BUTTON_PIN);
  if (!rs_btn_line) {
    perror ("get_line(rs_button)");
    return -1;
  }
  if (gpiod_line_request_both_edges_events (rs_btn_line, "pi_oled_shutdown_monitor") < 0) {
    perror ("request_both_edges_events(button)");
    return -1;
  }

  green_led_line = gpiod_chip_get_line (chip, GREEN_LED_PIN);
  if (!green_led_line) {
    perror ("get_green_line(led)");
    return -1;
  }
  if (gpiod_line_request_output (green_led_line, "pi_oled_shutdown_monitor", 1) < 0) {
    perror ("request_output(green led)");
    return -1;
  }

  red_led_line = gpiod_chip_get_line (chip, RED_LED_PIN);
  if (!red_led_line) {
    perror ("get_red_line(led)");
    return -1;
  }
  if (gpiod_line_request_output (red_led_line, "pi_oled_shutdown_monitor", 1) < 0) {
    perror ("request_output(red_led)");
    return -1;
  }

  bell_line = gpiod_chip_get_line (chip, ALARM_PIN);
  if (!bell_line) {
    perror ("get_bell_line(led)");
    return -1;
  }
  if (gpiod_line_request_output (bell_line, "pi_oled_shutdown_monitor", 1) < 0) {
    perror ("request_output(bell_led)");
    return -1;
  }

  return 0;
}

void
gpio_set_green_led (int val)
{
  if (green_led_line)
    gpiod_line_set_value (green_led_line, val ? 1 : 0);
}

void
gpio_set_red_led (int val)
{
  if (red_led_line)
    gpiod_line_set_value (red_led_line, val ? 1 : 0);
}

void
gpio_set_bell (int val)
{
  if (bell_line)
    gpiod_line_set_value (bell_line, val ? 1 : 0);
}

static void
gpio_cleanup (void)
{
  if (green_led_line) {
    gpiod_line_release (green_led_line);
    green_led_line = NULL;
  }
  if (red_led_line) {
    gpiod_line_release (red_led_line);
    red_led_line = NULL;
  }
  if (bell_line) {
    gpiod_line_release (bell_line);
    bell_line = NULL;
  }
  if (btn_line) {
    gpiod_line_release (btn_line);
    btn_line = NULL;
  }
  if (rs_btn_line) {
    gpiod_line_release (rs_btn_line);
    rs_btn_line = NULL;
  }
  if (chip) {
    gpiod_chip_close (chip);
    chip = NULL;
  }
}

char status_line[32] = { "Status: Okay" };

// ======== UI helpers ========
static void
draw_status_screen (const char *hostname, const char *ip, const char *ssid, double tempC, const char *uptime,
                    double voltage_mv, double current_ma)
{
  ssd1306_clear ();
  int y = 0;

  draw_text_prop (0, y, "Host: ");
  draw_text_prop (34, y, hostname && *hostname ? hostname : "—");

  y += 12;
  draw_text_prop (0, y, "IP: ");
  draw_text_prop (24, y, ip && *ip ? ip : "—");

//  draw_text_prop (0, y, "SSID: ");
//  draw_text_prop (34, y, ssid && *ssid ? ssid : "—");
  y += 12;

  // char tbuf[32]; snprintf(tbuf, sizeof tbuf, "CPU: %.1f\xC2\xB0""C", tempC);
  char tbuf[32];
  snprintf (tbuf, sizeof tbuf, "CPU: %.1f " "C", tempC);

  draw_text_prop (0, y, tbuf);
  y += 12;

//    char ubuf[32]; snprintf(ubuf, sizeof ubuf, "Up: %s", uptime);
//    draw_text_prop(0, y, ubuf); y += 12;

//    draw_text_prop(0, y, "Btn: Shutdown"); // hint line
  char vbuf[32];
  snprintf (vbuf, sizeof (vbuf), "Bat:  %3.2fV,   %3.2fA", voltage_mv / 1000.0,
            current_ma / 1000.0);
  draw_text_prop (0, y, vbuf);
  y += 12;

//    char sbuf[32]; snprintf(sbuf,sizeof(sbuf),"%s",status_line);
//    draw_text_prop(0, y, sbuf); y += 12;

  char rbuf[32];
  snprintf (rbuf, sizeof (rbuf), "%s", "Rover App: Off");
  draw_text_prop (0, y, rbuf);
  y += 12;

  // Optional: underline separator
//    ssd1306_hline(0, 10, 128, true);
  ssd1306_update ();
}

static void
draw_message_center (const char *msg)
{
  ssd1306_clear ();
  // crude center: we just start near center; the prop renderer will help
  int y = (SSD1306_HEIGHT / 2) - 4;
  int x = 8;
  draw_text_prop (x, y, msg);
  ssd1306_update ();
}

int
ina260_setup ()
{
  i2c_ina260_fd = open ("/dev/i2c-1", O_RDWR);
  if (i2c_ina260_fd < 0) {
    perror ("Unable to open I2C device");
    return 1;
  }

  if (ioctl (i2c_ina260_fd, I2C_SLAVE, INA260_ADDRESS) < 0) {
    perror ("Failed to set I2C address");
    return 2;
  }

  if (ina260_init (i2c_ina260_fd) != 0) {
    printf ("INA260 init failed\n");
    return 3;
  }
  return 0;
}

// Shared flag to control the background sound thread
// Using atomic ensures visibility across threads without a mutex
atomic_bool sound_enabled = false;

// Function that runs in the background thread
void *
background_sound_thread (void *arg)
{
  while (1) {
    while (atomic_load (&sound_enabled)) {
      // Placeholder for sound playback logic
      // In a real application, you would check sound_enabled frequently
      // (e.g., in an audio buffer loop or before a blocking PlaySound call).
      printf ("Sound: ON (playing...)\n");
      gpio_set_red_led (1);
      gpio_set_bell (1);

      // Simulate sound work
      usleep (300000);
      // } else {
      printf ("Sound: OFF (paused)\n");
      gpio_set_red_led (0);
      gpio_set_bell (0);
      // Sleep briefly to prevent a busy-wait loop when sound is off
      usleep (300000);
    }
//        usleep(10000); // idea time between actions

    // Add an exit condition if needed, also using an atomic flag
    // if (atomic_load(&should_exit)) break; 
  }
  return NULL;
}

// ======== Main loop ========
int
main (void)
{
  signal (SIGINT, sigint_handler);
  signal (SIGTERM, sigint_handler);

  pthread_t sound_tid;

  // Create the background sound thread
  if (pthread_create (&sound_tid, NULL, background_sound_thread, NULL) != 0) {
    perror ("pthread_create");
    return 1;
  }

  ina260_online = 0;
  if (ina260_setup () == 0) {
    ina260_online = 1;
  }
  else {
    fprintf (stderr, "ina260 init failed.\n");
  }

  if (ssd1306_init () < 0) {
    fprintf (stderr, "SSD1306 init failed.\n");
    return 1;
  }
  if (gpio_init () < 0) {
    fprintf (stderr, "GPIO init failed.\n");
    return 1;
  }

  char hostname[50];
  get_hostname (hostname, sizeof(hostname));

#if 0
  simple_logf ("Service started. Button GPIO%d, LED GPIO%d, OLED on %s addr 0x%02X",
               BUTTON_PIN, LED_PIN, OLED_I2C_DEV, OLED_ADDR);
#endif

#if 0
  // Startup LED blink for 3s (visible boot indicator)
  for (int i = 0; i < 6; ++i) {
    gpio_set_green_led (i % 2);
    gpio_set_red_led (i % 2);
    gpio_set_bell (i % 2);
    struct timespec ts = {.tv_sec = 0,.tv_nsec = 250 * 1000 * 1000 };
    nanosleep (&ts, NULL);
    if (!keepRunning)
      break;
  }
#endif
  // Startup LED blink & bell
  sound_enabled = true;
  sleep (1);
  sound_enabled = false;

  // Steady on while running
  gpio_set_green_led (0);
  gpio_set_red_led (0);
  gpio_set_bell (0);

  char ip[64] = { 0 }, last_ip[64] = { 0 };
  char ssid[64] = { 0 }, last_ssid[64] = { 0 };
  double tempC = 0.0, last_tempC = -999.0;
  float voltage_mv = 0.0, current_ma = 0.0;
  char upbuf[32] = { 0 };
  int tick_cntr = 0;
  int rover_run_state = 0;

  // Initial read
  get_ip_address (ip, sizeof ip);
  if (get_wifi_ssid (ssid, sizeof ssid) != 0)
    strncpy (ssid, "—", sizeof ssid);
  get_cpu_temp_c (&tempC);
  fmt_uptime (upbuf, sizeof upbuf);
  get_ina260_status (&voltage_mv, &current_ma);

  draw_status_screen (hostname, ip, ssid, tempC, upbuf, voltage_mv, current_ma);
  strncpy (last_ip, ip, sizeof last_ip);
  strncpy (last_ssid, ssid, sizeof last_ssid);
  last_tempC = tempC;

  // Main loop: refresh every ~1s, handle button events
  while (keepRunning) {
    // Periodic screen refresh
    bool changed = false;
    // Wait up to 1s for a button event
    struct timespec timeout = {.tv_sec = 1,.tv_nsec = 0 };

    int event_btn_1 = gpiod_line_event_wait (btn_line, &timeout);
    if (event_btn_1 < 0) {
      perror ("event_btn_1 gpiod_line_event_wait");
      break;
    }
    if (event_btn_1 == 1) {
      // Read and process event (debounced: only act on falling edge and ignore repeats for 500ms)
      struct gpiod_line_event ev;
      if (gpiod_line_event_read (btn_line, &ev) == 0) {
        if (ev.event_type == GPIOD_LINE_EVENT_FALLING_EDGE) {
          simple_logf ("Button pressed: initiating shutdown");
          draw_message_center ("Shutting down...");
          // turn off LED to indicate it's safe to cut power *after* OS halts
          gpio_set_green_led (1);
          gpio_set_red_led (1);
          gpio_set_bell (0);

          // brief delay so the message is visible
          usleep (800 * 1000);
          // Request shutdown
          int s = system ("shutdown -h now");
          break;
        }
      }
      // simple debounce delay
      usleep (150 * 1000);
    }

    struct timespec timeout2 = {.tv_sec = 1,.tv_nsec = 0 };
    int event_btn_2 = gpiod_line_event_wait (rs_btn_line, &timeout2);
    if (event_btn_2 < 0) {
      perror ("event_btn_2 gpiod_line_event_wait");
      break;
    }
    if (event_btn_2 == 1) {
      // Read and process event (debounced: only act on falling edge and ignore repeats for 500ms)
      struct gpiod_line_event ev;
      int system_call_status = 0;
      if (gpiod_line_event_read (rs_btn_line, &ev) == 0) {
        if (ev.event_type == GPIOD_LINE_EVENT_FALLING_EDGE) {
          simple_logf ("RS Button pressed: ??");
          draw_message_center ("Bell button pressed");
          // Toggle Rover run state
          if (rover_run_state != 0) {
            printf ("Stop Rover\n");
            rover_run_state = 0;
//                       stop_rover();

            printf ("'stop_rover.sh' script finished.\n");
            gpio_set_green_led (0);
          }
          else {
            printf ("Start Rover\n");
            rover_run_state = 1;
//                       stop_rover(); // make sure everthing thing is stopped
            usleep (10 * 1000); // 10 millsec ?

            start_rover ();

            printf ("'start_rover.sh' script finished.\n");
            gpio_set_green_led (1);
          }

          // brief delay so the message is visible
          usleep (800 * 1000);
        }
      }
      // simple debounce delay
      usleep (150 * 1000);
      changed = true;
    }

    // Periodic screen refresh
//        bool changed = false;

    if (get_ip_address (ip, sizeof ip) == 0) {
      if (strcmp (ip, last_ip) != 0) {
        changed = true;
        strncpy (last_ip, ip, sizeof last_ip);
      }
    }
    else {
      if (strcmp (last_ip, "—") != 0) {
        strncpy (last_ip, "—", sizeof last_ip);
        changed = true;
      }
    }

    if (get_wifi_ssid (ssid, sizeof ssid) == 0) {
      if (strcmp (ssid, last_ssid) != 0) {
        changed = true;
        strncpy (last_ssid, ssid, sizeof last_ssid);
      }
    }
    else {
      if (strcmp (last_ssid, "—") != 0) {
        strncpy (last_ssid, "—", sizeof last_ssid);
        changed = true;
      }
    }

    if (get_cpu_temp_c (&tempC) == 0) {
      // Update if temp changed by >= 0.5 C
      if (fabs (tempC - last_tempC) >= 0.5) {
        changed = true;
        last_tempC = tempC;
      }
    }

    fmt_uptime (upbuf, sizeof upbuf);

    voltage_mv = 0.0;
    current_ma = 0.0;
    if (ina260_online) {        // check if ina260 is connedted. 
      get_ina260_status (&voltage_mv, &current_ma);     // ina260_str
//            printf("Volts: %3.3fmV, Current: %3.3fmA\n", voltage_mv, current_ma);
      sound_enabled = false;
    }
    else {
//            printf("Status:ina260 off line\n");
      strcpy (status_line, "Status:ina260 off line");
    }

    if (ina260_online == 1) {
      if ((voltage_mv < VOLATGE_LOW_LIMIT) && (tick_cntr & 1)) {
        //    printf("Voltage fault: %3.3f V\n",voltage_mv);
        sound_enabled = true;
        changed = true;         // ??
        strcpy (status_line, "Under Voltage Fault");
        // Should we do something else here? ie shut down ROS2??
      }
      else if ((voltage_mv > VOLATGE_HIGH_LIMIT) && (tick_cntr & 1)) {
        sound_enabled = true;
        strcpy (status_line, "Over Voltage Fault");
        // Should we do something else here? ie shut down ROS2??
      }
      else if ((current_ma > CURRENT_HIGH_LIMIT) && (tick_cntr & 1)) {
        sound_enabled = true;
        strcpy (status_line, "Over Current Fault");
        // Should we do something else here? ie shut down ROS2??
      }
      else {
        sound_enabled = false;
        strcpy (status_line, "Status: Okay");
      }
    }

    if (changed) {
      draw_status_screen (hostname,last_ip, last_ssid, last_tempC, upbuf, voltage_mv, current_ma);
    }
    else {
      // Still refresh once every ~10 seconds to keep uptime current
      static int counter = 0;
      counter = (counter + 1) % 10;
      if (counter == 0)
        draw_status_screen (hostname,last_ip, last_ssid, last_tempC, upbuf, 1200.0, 500.0);
    }
    tick_cntr++;
  }

  gpio_cleanup ();
  if (i2c_fd >= 0)
    close (i2c_fd);
  return 0;
}
