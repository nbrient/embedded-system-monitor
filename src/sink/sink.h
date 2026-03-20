/**
 * @file sink.h
 * @brief Logging sink — one typed output function per collector.
 * This file grows as new collectors are added.
 */
#ifndef SINK_H
#define SINK_H
#include "../core/types.h"
#include "../core/config.h"
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                              Public functions
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/** @brief Select the active logging backend. Must be called once before any sink_log_*(). @param target LOG_TARGET_JOURNALCTL or LOG_TARGET_SYSLOG. */
extern void sink_init(LogTarget target);
/** @brief Log a CPU metrics snapshot. @param cpuMetrics Snapshot from the CPU collector. Must not be NULL. */
extern void sink_log_cpu(const CpuMetrics *cpuMetrics);
/** @brief Log a memory metrics snapshot. @param memStats Snapshot from the memory collector. Must not be NULL. */
extern void sink_log_mem(const MemStats *memStats);
/**
 * @brief Log an IRQ metrics snapshot.
 * @param irqData   IRQ entry array [nbLines]. Must not be NULL.
 * @param nbLines   Number of entries in irqData.
 * @param nbCpu     Number of online CPUs.
 * @param firstAcq  If true, log all lines regardless of delta.
 */
extern void sink_log_irq(const IrqEntry *irqData, uint8_t nbLines, uint8_t nbCpu, bool firstAcq);
/**
 * @brief Log a process metrics snapshot.
 * @param procData   Array of ProcStatEntry [nbProcess]. Must not be NULL.
 * @param nbProcess  Number of processes in the snapshot.
 */
extern void sink_log_proc(const ProcStatEntry *procData, uint16_t nbProcess);
#endif
