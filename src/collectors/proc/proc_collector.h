/**
 * @file proc_collector.h
 *
 * @brief Periodic process stats collector.
 *
 * Enumerates /proc for numeric directory entries (PIDs), reads
 * /proc/[pid]/stat for each, and forwards the snapshot via sink_log_proc().
 * PIDs that disappear between enumeration and open are silently skipped.
 *
 * @par Lifecycle
 * @code
 *   proc_collector_new(cfg);
 *   proc_collector_ask_start();
 *   // ... running ...
 *   proc_collector_ask_stop();
 *   proc_collector_free();
 * @endcode
 */

#ifndef PROC_COLLECTOR_H
#define PROC_COLLECTOR_H

#include "../../core/config.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Public functions
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Initialise the process collector.
 *
 * Opens the mqueue and creates the watchdog timer.
 *
 * @param cfg  Collector configuration (enabled flag + acquisition interval).
 * @return     0 on success, -1 on error.
 */
extern int8_t proc_collector_new(CollectorCfg cfg);

/**
 * @brief Release all resources held by the process collector.
 *
 * Destroys the watchdog, frees the snapshot buffer, closes and unlinks
 * the mqueue, destroys the mutex.
 *
 * @return 0 on success, -1 on error.
 */
extern int8_t proc_collector_free(void);

/**
 * @brief Spawn the worker thread and arm the watchdog.
 *
 * @return 0 on success, -1 on error.
 */
extern int8_t proc_collector_ask_start(void);

/**
 * @brief Send E_STOP to the worker thread and join it.
 *
 * Blocks until the worker thread has exited.
 *
 * @return 0 on success, -1 on error.
 */
extern int8_t proc_collector_ask_stop(void);

#endif /* PROC_COLLECTOR_H */
