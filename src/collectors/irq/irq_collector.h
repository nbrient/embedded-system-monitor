/**
 * @file irq_collector.h
 *
 * @brief Periodic IRQ counters collector.
 *
 * Reads /proc/interrupts on a configurable interval, computes per-CPU
 * deltas since the last acquisition, and forwards the snapshot
 * via sink_log_irq().
 *
 * @par Lifecycle
 * @code
 *   irq_collector_new(cfg);
 *   irq_collector_ask_start();
 *   // ... running ...
 *   irq_collector_ask_stop();
 *   irq_collector_free();
 * @endcode
 */

#ifndef IRQ_COLLECTOR_H
#define IRQ_COLLECTOR_H

#include "../../core/config.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Public functions
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Initialise the IRQ collector.
 *
 * Counts interrupt lines, allocates the IrqEntry array, opens the mqueue
 * and creates the watchdog timer.
 *
 * @param cfg  Collector configuration (enabled flag + acquisition interval).
 * @return     0 on success, -1 on error.
 */
extern int8_t irq_collector_new(CollectorCfg cfg);

/**
 * @brief Release all resources held by the IRQ collector.
 *
 * Destroys the watchdog, frees the IrqEntry array, closes and unlinks
 * the mqueue, destroys the mutex.
 *
 * @return 0 on success, -1 on error.
 */
extern int8_t irq_collector_free(void);

/**
 * @brief Spawn the worker thread and arm the watchdog.
 *
 * @return 0 on success, -1 on error.
 */
extern int8_t irq_collector_ask_start(void);

/**
 * @brief Send E_STOP to the worker thread and join it.
 *
 * Blocks until the worker thread has exited.
 *
 * @return 0 on success, -1 on error.
 */
extern int8_t irq_collector_ask_stop(void);

#endif /* IRQ_COLLECTOR_H */
