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
#include <gpiod.h>  // libgpiod v1.6.3
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
#include "rover_pin_drv.h"

#define VOLATGE_HIGH_LIMIT (16000.0)    // 16 volts
#define VOLATGE_LOW_LIMIT  (12000.0)    // 12 volts
#define CURRENT_HIGH_LIMIT  (7000.0)    // 7 amps
#define OLED_I2C_DEV   "/dev/i2c-1"
#define OLED_ADDR      0x3c     // 0x3C
#define CHIPNAME       "gpiochip0"

// gpio outputs
#define GREEN_LED_PIN       13
#define RED_LED_PIN         20
#define ALARM_PIN           16

// gpio inputs
#define SHUTDOWN_BUTTON_PIN 19
#define RUN_STOP_BUTTON_PIN 21

static int i2c_ina260_fd;
static int ina260_online = 0;
static int rover_run_state = 0;

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

// ======== OLED / SSD1306 driver ========
#include "ssd1306.h"

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

static int
gpio_init (void)
{
  chip = gpiod_chip_open_by_name (CHIPNAME);
  if (!chip) {
    perror ("gpiod_chip_open_by_name");
    return -1;
  }

if (rover_pin_drv_init(chip, GREEN_LED_PIN, RED_LED_PIN, ALARM_PIN, "led_test", 0) < 0) {
    fprintf(stderr, "rover_pin_drv_init() failed\n");
    fprintf(stderr, "Try: \"sudo systemctl stop ip2oled_monitor_bonnet.service\"\n");
    gpiod_chip_close(chip);
    exit(EXIT_FAILURE);  // fix this jerry
    return 1;
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

  return 0;
}

static void
gpio_cleanup (void)
{

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
  if (rover_run_state) {
     snprintf (rbuf, sizeof (rbuf), "%s", "Rover App:  On");
  } else {
     snprintf (rbuf, sizeof (rbuf), "%s", "Rover App:  Off");
  }
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

// Sound Function runs in a background thread
void *
background_sound_thread (void *arg)
{
  while (1) {
    while (atomic_load (&sound_enabled)) {
      rover_pin_drv_set_red(1);
      rover_pin_drv_set_buzzer(1);

      usleep (300000);
      // } else {
      rover_pin_drv_set_red(0);
      rover_pin_drv_set_buzzer(0);
      // Sleep briefly to prevent a busy-wait loop when sound is off
      usleep (300000);
    }

  }
  return NULL;
}

// ======== Main loop ========
int
main (void)
{
  signal (SIGINT, sigint_handler);
  signal (SIGTERM, sigint_handler);

  if (is_raspberry_pi()) {
        printf("Running on a Raspberry Pi.\n");
  } else {
        printf("Not running on a Raspberry Pi. Bye\n");
        return 1;
  }

  pthread_t sound_tid;

  // Create the background sound thread
  if (pthread_create (&sound_tid, NULL, background_sound_thread, NULL) != 0) {
    perror ("pthread_create error");
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

  // Startup LED blink & bell
  sound_enabled = true;
  sleep (1);
  sound_enabled = false;

  rover_pin_drv_set_green(0);
  rover_pin_drv_set_red(0);
  rover_pin_drv_set_buzzer(0);

  char ip[64] = { 0 }, last_ip[64] = { 0 };
  char ssid[64] = { 0 }, last_ssid[64] = { 0 };
  double tempC = 0.0, last_tempC = -999.0;
  float voltage_mv = 0.0, current_ma = 0.0;
  char upbuf[32] = { 0 };
  int tick_cntr = 0;
//  int rover_run_state = 0;

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
          rover_pin_drv_set_green(1);
          rover_pin_drv_set_red(1);
          rover_pin_drv_set_buzzer(1);
          usleep (400 * 1000);

          // brief delay so the message is visible
          rover_pin_drv_set_green(0);
          rover_pin_drv_set_red(0);
          rover_pin_drv_set_buzzer(0);
          usleep (400 * 1000);

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
            stop_rover();

            printf ("'stop_rover.sh' script finished.\n");
            rover_pin_drv_set_green(0);
          }
          else {
            printf ("Start Rover\n");
            rover_run_state = 1;
//                       stop_rover(); // make sure everthing thing is stopped
            usleep (10 * 1000); // 10 millsec ?

            start_rover ();

            printf ("'start_rover.sh' script finished.\n");
            rover_pin_drv_set_green(1);
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
        draw_status_screen (hostname,last_ip, last_ssid, last_tempC, upbuf, voltage_mv, current_ma); // 1200.0, 500.0);
    }
    tick_cntr++;
  }

  gpio_cleanup ();
  rover_pin_drv_shutdown();

  ssd1306_shutdown();

// if (i2c_fd >= 0)
//    close (i2c_fd);
  return 0;
}
