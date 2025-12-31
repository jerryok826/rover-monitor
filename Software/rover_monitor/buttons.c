/* buttons.c
 *
 * Implementation for libgpiod v1.6.3 button helper.
 *
 * Build (with a separate main.c):
 *   gcc -Wall -O2 -c buttons.c
 *   gcc -Wall -O2 main.c buttons.o -lgpiod -lpthread -o app
 *
 * Optional unit test main is at bottom under #if 0.
 */

#include "buttons.h"

#include <gpiod.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>   // pause()

// ---------- Configuration ----------
#ifndef BUTTONS_GPIOCHIP_PATH
#define BUTTONS_GPIOCHIP_PATH "/dev/gpiochip0"
#endif

#ifndef BUTTONS_DEBOUNCE_MS
#define BUTTONS_DEBOUNCE_MS 40
#endif

#ifndef BUTTONS_RELEASE_POLL_MS
#define BUTTONS_RELEASE_POLL_MS 5
#endif
// -----------------------------------

/* =========================
 * Private / internal section
 * ========================= */

struct btn_ctx {
    int pin;
    struct gpiod_line *line;

    pthread_t thread;
    int thread_started;

    button_cb_t cb;

    int64_t last_accept_ns;
    int64_t debounce_ns;
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct gpiod_chip *g_chip = NULL;
static struct btn_ctx g_btn[2];
static int g_inited = 0;
static volatile int g_stop = 0;

static void _print_err(const char *where) {
    fprintf(stderr, "%s: %s\n", where, strerror(errno));
}

static inline int64_t _ts_to_ns(const struct timespec *ts) {
    return (int64_t)ts->tv_sec * 1000000000LL + (int64_t)ts->tv_nsec;
}

static void _sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000L * 1000L;
    nanosleep(&ts, NULL);
}

/* Pull-up wiring: released=1, pressed=0 */
static void _wait_for_release_pullup(struct gpiod_line *line, int pin) {
    while (!g_stop) {
        int v = gpiod_line_get_value(line);
        if (v < 0) {
            fprintf(stderr, "GPIO %d get_value error: %s\n", pin, strerror(errno));
            return; // during shutdown, don't hard-exit
        }
        if (v == 1) return; // released
        _sleep_ms(BUTTONS_RELEASE_POLL_MS);
    }
}

static void *_button_thread(void *arg) {
    struct btn_ctx *ctx = (struct btn_ctx *)arg;

    for (;;) {
        if (g_stop) break;

        /* Use a finite timeout so shutdown doesn't hang waiting forever */
        struct timespec timeout = { .tv_sec = 0, .tv_nsec = 200 * 1000 * 1000 }; // 200ms
        int w = gpiod_line_event_wait(ctx->line, &timeout);
        if (w < 0) {
            if (g_stop) break;
            fprintf(stderr, "GPIO %d event_wait error: %s\n", ctx->pin, strerror(errno));
            break;
        }
        if (w == 0) continue; // timeout, loop to check g_stop

        /* Drain queued events */
        for (;;) {
            if (g_stop) break;

            struct gpiod_line_event ev;
            if (gpiod_line_event_read(ctx->line, &ev) < 0) {
                if (errno == EAGAIN) { errno = 0; break; }
                if (g_stop) break;
                fprintf(stderr, "GPIO %d event_read error: %s\n", ctx->pin, strerror(errno));
                break;
            }

            /* press-only for pull-up: FALLING edge (1 -> 0) */
            if (ev.event_type != GPIOD_LINE_EVENT_FALLING_EDGE)
                continue;

            /* debounce */
            int64_t now_ns = _ts_to_ns(&ev.ts);
            if (ctx->last_accept_ns >= 0 &&
                (now_ns - ctx->last_accept_ns) < ctx->debounce_ns) {
                continue;
            }
            ctx->last_accept_ns = now_ns;

            /* callback */
            button_cb_t cb_local = NULL;

            /* Take a local copy under lock so callback registration is thread-safe */
            pthread_mutex_lock(&g_lock);
            cb_local = ctx->cb;
            pthread_mutex_unlock(&g_lock);

            if (cb_local) cb_local(ctx->pin);

            /* release gate */
            _wait_for_release_pullup(ctx->line, ctx->pin);
        }
    }

    return NULL;
}

static struct btn_ctx *_find_ctx(int pin_num) {
    for (int i = 0; i < 2; i++) {
        if (g_btn[i].pin == pin_num) return &g_btn[i];
    }
    return NULL;
}

/* =========================
 * Public API section
 * ========================= */

int buttons_init(int pin_num1, int pin_num2) {
    pthread_mutex_lock(&g_lock);

    if (g_inited) {
        fprintf(stderr, "buttons_init: already initialized\n");
        pthread_mutex_unlock(&g_lock);
        return -1;
    }
    if (pin_num1 == pin_num2) {
        fprintf(stderr, "buttons_init: pins must be different\n");
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    g_stop = 0;
    memset(g_btn, 0, sizeof(g_btn));

    g_chip = gpiod_chip_open(BUTTONS_GPIOCHIP_PATH);
    if (!g_chip) {
        _print_err("gpiod_chip_open");
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    g_btn[0].pin = pin_num1;
    g_btn[1].pin = pin_num2;

    for (int i = 0; i < 2; i++) {
        g_btn[i].last_accept_ns = -1;
        g_btn[i].debounce_ns = (int64_t)BUTTONS_DEBOUNCE_MS * 1000LL * 1000LL;
        g_btn[i].cb = NULL;

        g_btn[i].line = gpiod_chip_get_line(g_chip, g_btn[i].pin);
        if (!g_btn[i].line) {
            fprintf(stderr, "gpiod_chip_get_line failed for GPIO %d\n", g_btn[i].pin);
            pthread_mutex_unlock(&g_lock);
            buttons_shutdown(); // cleanup partial init
            return -1;
        }

        if (gpiod_line_request_falling_edge_events(g_btn[i].line, "buttons_lib") < 0) {
            fprintf(stderr, "request_falling_edge_events failed for GPIO %d: %s\n",
                    g_btn[i].pin, strerror(errno));
            pthread_mutex_unlock(&g_lock);
            buttons_shutdown(); // cleanup partial init
            return -1;
        }
    }

    for (int i = 0; i < 2; i++) {
        if (pthread_create(&g_btn[i].thread, NULL, _button_thread, &g_btn[i]) != 0) {
            fprintf(stderr, "pthread_create failed for GPIO %d\n", g_btn[i].pin);
            pthread_mutex_unlock(&g_lock);
            buttons_shutdown(); // cleanup partial init
            return -1;
        }
        g_btn[i].thread_started = 1;
    }

    g_inited = 1;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

int button_callback(int pin_num, button_cb_t cb) {
    pthread_mutex_lock(&g_lock);

    if (!g_inited) {
        fprintf(stderr, "button_callback: buttons_init() must be called first\n");
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    struct btn_ctx *ctx = _find_ctx(pin_num);
    if (!ctx) {
        fprintf(stderr, "button_callback: pin %d not initialized\n", pin_num);
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    ctx->cb = cb;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

int buttons_shutdown(void) {
    pthread_mutex_lock(&g_lock);

    if (!g_inited && !g_chip) {
        pthread_mutex_unlock(&g_lock);
        return 0; // nothing to do
    }

    g_stop = 1;

    /* Copy thread handles locally so we can join without holding the lock */
    pthread_t t0 = g_btn[0].thread;
    pthread_t t1 = g_btn[1].thread;
    int s0 = g_btn[0].thread_started;
    int s1 = g_btn[1].thread_started;

    pthread_mutex_unlock(&g_lock);

    /* Join threads (they wake at most within the event_wait timeout) */
    if (s0) pthread_join(t0, NULL);
    if (s1) pthread_join(t1, NULL);

    pthread_mutex_lock(&g_lock);

    /* Release lines */
    for (int i = 0; i < 2; i++) {
        if (g_btn[i].line) {
            gpiod_line_release(g_btn[i].line);
            g_btn[i].line = NULL;
        }
        g_btn[i].thread_started = 0;
        g_btn[i].cb = NULL;
        g_btn[i].pin = 0;
        g_btn[i].last_accept_ns = -1;
    }

    /* Close chip */
    if (g_chip) {
        gpiod_chip_close(g_chip);
        g_chip = NULL;
    }

    g_inited = 0;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

#if 0
/* --------------------- Unit test main() ---------------------
 * Enable by changing #if 0 to #if 1 above.
 *
 * Build:
 *   gcc -Wall -O2 buttons.c -lgpiod -lpthread -o buttons_test
 * Run:
 *   sudo ./buttons_test
 */

static void cb_print(int pin) {
    printf("CB: GPIO %d pressed\n", pin);
    fflush(stdout);
}

int main(void) {
    if (buttons_init(19, 21) != 0) {
        fprintf(stderr, "buttons_init failed\n");
        return 1;
    }

    button_callback(19, cb_print);
    button_callback(21, cb_print);

    printf("Buttons initialized. Press GPIO19/21. Ctrl-C to stop.\n");
    fflush(stdout);

    for (;;) pause();

    // If you ever break out, clean shutdown:
    // buttons_shutdown();
    // return 0;
}
#endif
