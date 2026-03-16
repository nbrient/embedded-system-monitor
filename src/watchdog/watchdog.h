/**
 * @file watchdog.h
 *
 * @brief One-shot repeating timer using POSIX SIGEV_THREAD.
 *
 * Each call to watchdog_arm() schedules a single one-shot fire.
 * The callback is responsible for re-arming if periodic behaviour is needed.
 *
 * @par Usage
 * @code
 *   Watchdog *wd = watchdog_create(5000000, my_callback);
 *   watchdog_arm(wd);
 *   ...
 *   watchdog_disarm(wd);
 *   watchdog_destroy(wd);
 * @endcode
 */

#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Structures
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Callback invoked when the timer fires.
 *
 * The callback runs in a new POSIX thread created by the OS.
 */
typedef void (*WatchdogCb)(void);

/**
 * @brief Opaque watchdog handle returned by watchdog_create().
 */
typedef struct Watchdog Watchdog;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Public functions
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Allocate and configure a watchdog timer.
 *
 * Creates a POSIX timer on CLOCK_MONOTONIC using SIGEV_THREAD notification.
 *
 * @param periodUs  Fire delay in microseconds.
 * @param cb        Function called on expiry.
 * @return          Pointer to the new Watchdog, or NULL on failure.
 */
extern Watchdog *watchdog_create(uint32_t periodUs, WatchdogCb cb);

/**
 * @brief Arm (or re-arm) the watchdog for one shot.
 *
 * Safe to call from inside the callback to achieve periodic behaviour.
 *
 * @param wd  Watchdog handle — silently ignored if NULL.
 */
extern void watchdog_arm(Watchdog *wd);

/**
 * @brief Cancel a pending fire without releasing resources.
 *
 * The watchdog can be re-armed after disarming.
 *
 * @param wd  Watchdog handle — silently ignored if NULL.
 */
extern void watchdog_disarm(Watchdog *wd);

/**
 * @brief Disarm, delete the POSIX timer and free the handle.
 *
 * @param wd  Watchdog handle — silently ignored if NULL.
 */
extern void watchdog_destroy(Watchdog *wd);

#endif /* WATCHDOG_H */
