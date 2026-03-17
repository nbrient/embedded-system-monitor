/**
 * @file mem_collector.h
 *
 * @brief Periodic memory metrics collector.
 *
 * Reads /proc/meminfo on a configurable interval and forwards
 * each snapshot to the sink via sink_log_mem().
 *
 * @par Lifecycle
 * @code
 *   mem_collector_new(cfg);
 *   mem_collector_ask_start();
 *   // ... running ...
 *   mem_collector_ask_stop();
 *   mem_collector_free();
 * @endcode
 */

#ifndef MEM_COLLECTOR_H
#define MEM_COLLECTOR_H

#include "../../core/config.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Public functions
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Initialise the memory collector.
 *
 * Opens the mqueue and creates the watchdog timer.
 *
 * @param cfg  Collector configuration (enabled flag + acquisition interval).
 * @return     0 on success, -1 on error.
 */
extern int8_t mem_collector_new(CollectorCfg cfg);

/**
 * @brief Release all resources held by the memory collector.
 *
 * Destroys the watchdog, closes and unlinks the mqueue, destroys the mutex.
 *
 * @return 0 on success, -1 on error.
 */
extern int8_t mem_collector_free(void);

/**
 * @brief Spawn the worker thread and arm the watchdog.
 *
 * @return 0 on success, -1 on error.
 */
extern int8_t mem_collector_ask_start(void);

/**
 * @brief Send E_STOP to the worker thread and join it.
 *
 * Blocks until the worker thread has exited.
 *
 * @return 0 on success, -1 on error.
 */
extern int8_t mem_collector_ask_stop(void);

#endif /* MEM_COLLECTOR_H */
