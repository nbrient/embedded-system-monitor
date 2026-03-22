/**
 * @file watchdog.c
 *
 * @brief Watchdog timer — POSIX timer_create with SIGEV_THREAD notification.
 *
 * @version 1.0
 */

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Include
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define _POSIX_C_SOURCE 200809L

#include "watchdog.h"
#include "../core/debug.h"

#include <signal.h>
#include <stdlib.h>
#include <time.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Variable and private structures
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Internal watchdog state (opaque to callers).
 */
struct Watchdog {
  timer_t timerId;     /**< Underlying POSIX timer handle. */
  uint32_t periodUs;   /**< Period in microseconds, set at construction. */
  WatchdogCb callback; /**< User callback invoked when the timer fires. */
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Static functions
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief SIGEV_THREAD handler — extracts the Watchdog and calls the user
 * callback.
 *
 * Invoked by the OS in a new thread when the timer fires.
 *
 * @param sv  Signal value carrying the Watchdog pointer set at construction.
 */
static void timer_handler(union sigval sv) {
  Watchdog *wd = (Watchdog *)sv.sival_ptr;
  if (wd && wd->callback) {
    wd->callback();
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Public functions
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern Watchdog *watchdog_create(uint32_t periodUs, WatchdogCb cb) {
  Watchdog *wd = malloc(sizeof(*wd));
  if (!wd)
    return NULL;

  wd->periodUs = periodUs;
  wd->callback = cb;

  struct sigevent sev = {
      .sigev_notify = SIGEV_THREAD,
      .sigev_value.sival_ptr = wd,
      .sigev_notify_function = timer_handler,
      .sigev_notify_attributes = NULL,
  };

  if (timer_create(CLOCK_MONOTONIC, &sev, &wd->timerId) != 0) {
    TRACE("[Watchdog] timer_create failed\n");
    free(wd);
    return NULL;
  }

  return wd;
}

extern void watchdog_arm(Watchdog *wd) {
  if (!wd)
    return;

  struct itimerspec spec = {
      .it_interval = {0, 0},
      .it_value =
          {
              .tv_sec = (time_t)(wd->periodUs / 1000000U),
              .tv_nsec = (long)((wd->periodUs % 1000000U) * 1000U),
          },
  };

  timer_settime(wd->timerId, 0, &spec, NULL);
}

extern void watchdog_disarm(Watchdog *wd) {
  if (!wd)
    return;
  struct itimerspec spec = {0};
  timer_settime(wd->timerId, 0, &spec, NULL);
}

extern void watchdog_destroy(Watchdog *wd) {
  if (!wd)
    return;
  watchdog_disarm(wd);
  timer_delete(wd->timerId);
  free(wd);
}
