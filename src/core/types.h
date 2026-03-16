/**
 * @file types.h
 *
 * @brief Shared data types for embedded-monitor.
 *
 * This file grows as new collectors are added.
 * Only types used by currently compiled modules are declared here.
 */

#ifndef CORE_TYPES_H
#define CORE_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              CPU types
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief CPU time spent in each scheduling mode, expressed in jiffies.
 *
 * Values are read directly from a @c cpu* line in @c /proc/stat.
 */
typedef struct {
    uint32_t user;    /**< Time in user mode. */
    uint32_t nice;    /**< Time in user mode with low priority. */
    uint32_t system;  /**< Time in kernel mode. */
    uint32_t idle;    /**< Idle time. */
    uint32_t iowait;  /**< Time waiting for I/O completion. */
    uint32_t irq;     /**< Time servicing hardware interrupts. */
    uint32_t softirq; /**< Time servicing software interrupts. */
} CoreTimes;

/**
 * @brief Full CPU snapshot: load averages and per-core jiffies counters.
 */
typedef struct {
    float     load1min;         /**< 1-minute load average from /proc/loadavg. */
    float     load5min;         /**< 5-minute load average from /proc/loadavg. */
    float     load15min;        /**< 15-minute load average from /proc/loadavg. */
    int       runnableEntities; /**< Number of currently runnable scheduling entities. */
    int       totalEntities;    /**< Total number of kernel scheduling entities. */
    char      lastPid[16];      /**< PID of the most recently created process. */
    CoreTimes allCores;         /**< Aggregate jiffies counters across all CPUs. */
    CoreTimes *perCore;         /**< Per-CPU jiffies array [numCores], heap-allocated. */
    uint8_t   numCores;         /**< Number of online CPU cores. */
} CpuMetrics;

#endif /* CORE_TYPES_H */
