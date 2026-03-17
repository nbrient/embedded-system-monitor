/**
 * @file sink.h
 *
 * @brief Logging sink — one typed output function per collector.
 *
 * Call sink_init() once at startup to select the backend.
 * This file grows as new collectors are added.
 */

#ifndef SINK_H
#define SINK_H

#include "../core/types.h"
#include "../core/config.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Public functions
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Select the active logging backend.
 *
 * Must be called once before any sink_log_*() function.
 *
 * @param target  LOG_TARGET_JOURNALCTL or LOG_TARGET_SYSLOG.
 */
extern void sink_init(LogTarget target);

/**
 * @brief Log a CPU metrics snapshot to the active backend.
 *
 * @param cpuMetrics  Snapshot produced by the CPU collector. Must not be NULL.
 */
extern void sink_log_cpu(const CpuMetrics *cpuMetrics);

/**
 * @brief Log a memory metrics snapshot to the active backend.
 *
 * @param memStats  Snapshot produced by the memory collector. Must not be NULL.
 */
extern void sink_log_mem(const MemStats *memStats);

#endif /* SINK_H */
