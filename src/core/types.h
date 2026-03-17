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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Memory types
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Key fields from /proc/meminfo, all values in kB.
 *
 * Fields are populated by key-dispatch in the memory collector.
 * Unrecognised /proc/meminfo lines are silently ignored.
 */
typedef struct {
    uint64_t memTotal;     /**< Total usable RAM. */
    uint64_t memFree;      /**< Free RAM (not including caches). */
    uint64_t memAvailable; /**< Estimated RAM available for new processes. */
    uint64_t buffers;      /**< Kernel I/O buffers. */
    uint64_t cached;       /**< Page cache (excluding SwapCached). */
    uint64_t swapCached;   /**< Swap space used as cache. */
    uint64_t active;       /**< Recently used memory, not easily reclaimed. */
    uint64_t inactive;     /**< Memory not recently used. */
    uint64_t swapTotal;    /**< Total swap space. */
    uint64_t swapFree;     /**< Unused swap space. */
    uint64_t dirty;        /**< Memory waiting to be written back to disk. */
    uint64_t writeback;    /**< Memory actively being written back. */
    uint64_t shmem;        /**< Shared memory (tmpfs). */
    uint64_t slab;         /**< In-kernel data-structure cache. */
    uint64_t sReclaimable; /**< Reclaimable portion of slab. */
    uint64_t sUnreclaim;   /**< Non-reclaimable portion of slab. */
    uint64_t kernelStack;  /**< Memory used for kernel stacks. */
    uint64_t pageTables;   /**< Memory used by page tables. */
    uint64_t vmallocTotal; /**< Total vmalloc address space size. */
    uint64_t vmallocUsed;  /**< Used vmalloc space. */
    uint64_t commitLimit;  /**< Total RAM + swap available for allocation. */
    uint64_t committed;    /**< Committed virtual address space (Committed_AS). */
} MemStats;
