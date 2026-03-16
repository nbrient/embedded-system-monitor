/**
 * @file cpu_collector.h
 *
 * @brief Periodic CPU metrics collector.
 *
 * Reads /proc/loadavg and /proc/stat on a configurable interval
 * and forwards each snapshot to the sink via sink_log_cpu().
 *
 * @par Lifecycle
 * @code
 *   cpu_collector_new(cfg);
 *   cpu_collector_ask_start();
 *   // ... running ...
 *   cpu_collector_ask_stop();
 *   cpu_collector_free();
 * @endcode
 */

#ifndef CPU_COLLECTOR_H
#define CPU_COLLECTOR_H

#include "../../core/config.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Public functions
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Initialise the CPU collector.
 *
 * Allocates the per-core array, opens the mqueue and creates the watchdog.
 *
 * @param cfg  Collector configuration (enabled flag + acquisition interval).
 * @return     0 on success, -1 on error.
 */
extern int8_t cpu_collector_new(CollectorCfg cfg);

/**
 * @brief Release all resources held by the CPU collector.
 *
 * Destroys the watchdog, closes and unlinks the mqueue, destroys the mutex
 * and frees the per-core array.
 *
 * @return 0 on success, -1 on error.
 */
extern int8_t cpu_collector_free(void);

/**
 * @brief Spawn the worker thread and arm the watchdog.
 *
 * @return 0 on success, -1 on error.
 */
extern int8_t cpu_collector_ask_start(void);

/**
 * @brief Send E_STOP to the worker thread and join it.
 *
 * Blocks until the worker thread has exited.
 *
 * @return 0 on success, -1 on error.
 */
extern int8_t cpu_collector_ask_stop(void);

#endif /* CPU_COLLECTOR_H */
