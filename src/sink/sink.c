/**
 * @file sink.c
 *
 * @brief Logging sink — dispatches to journalctl or syslog backends.
 *
 * @version 1.0
 */

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Include
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "sink.h"
#include "../core/debug.h"

#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <systemd/sd-journal.h>
#include <unistd.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Variable and private structures
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Active logging backend, set once by sink_init().
 */
static LogTarget activeTarget = LOG_TARGET_JOURNALCTL;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Function prototype
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void journalctl_log_cpu(const CpuMetrics *cpuMetrics);
static void syslog_log_cpu(const CpuMetrics *cpuMetrics);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Public functions
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern void sink_init(LogTarget target) {
  activeTarget = target;
  TRACE("[Sink] target=%d\n", target);
}

extern void sink_log_cpu(const CpuMetrics *cpuMetrics) {
  if (activeTarget == LOG_TARGET_SYSLOG) {
    syslog_log_cpu(cpuMetrics);
  } else {
    journalctl_log_cpu(cpuMetrics);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Static functions — CPU
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Write a CPU snapshot to the systemd journal.
 *
 * Formats load averages, aggregate jiffies and per-core jiffies into
 * structured journal fields.
 *
 * @param cpuMetrics  CPU metrics snapshot. Must not be NULL.
 */
static void journalctl_log_cpu(const CpuMetrics *cpuMetrics) {
  char loadBuf[256];
  char statBuf[512] = "";
  char perCoreBuf[2048] = "";

  snprintf(loadBuf, sizeof(loadBuf),
           "load1=%.2f load5=%.2f load15=%.2f runnable=%d total=%d lastpid=%s",
           cpuMetrics->load1min, cpuMetrics->load5min, cpuMetrics->load15min,
           cpuMetrics->runnableEntities, cpuMetrics->totalEntities,
           cpuMetrics->lastPid);

  snprintf(statBuf, sizeof(statBuf),
           "user:%u nice:%u system:%u idle:%u iowait:%u irq:%u softirq:%u",
           cpuMetrics->allCores.user, cpuMetrics->allCores.nice,
           cpuMetrics->allCores.system, cpuMetrics->allCores.idle,
           cpuMetrics->allCores.iowait, cpuMetrics->allCores.irq,
           cpuMetrics->allCores.softirq);

  for (uint8_t i = 0; i < cpuMetrics->numCores; i++) {
    char coreEntry[256];
    snprintf(
        coreEntry, sizeof(coreEntry),
        "cpu%u user:%u nice:%u system:%u idle:%u iowait:%u irq:%u softirq:%u\n",
        i, cpuMetrics->perCore[i].user, cpuMetrics->perCore[i].nice,
        cpuMetrics->perCore[i].system, cpuMetrics->perCore[i].idle,
        cpuMetrics->perCore[i].iowait, cpuMetrics->perCore[i].irq,
        cpuMetrics->perCore[i].softirq);
    strncat(perCoreBuf, coreEntry, sizeof(perCoreBuf) - strlen(perCoreBuf) - 1);
  }

  sd_journal_send("MESSAGE=cpu metrics snapshot",
                  "MESSAGE_ID=31ec43a6b4c24545bf21791c041c9f89", "PRIORITY=5",
                  "NB_CORES=%u", (unsigned)cpuMetrics->numCores, "CPU_LOAD=%s",
                  loadBuf, "CPU_STAT=%s", statBuf, "CPU_PER_CORE=%s",
                  perCoreBuf, NULL);
}

/**
 * @brief Write a CPU snapshot to syslog.
 *
 * Logs only the three load averages (detailed fields require journalctl).
 *
 * @param cpuMetrics  CPU metrics snapshot. Must not be NULL.
 */
static void syslog_log_cpu(const CpuMetrics *cpuMetrics) {
  openlog("embedded-monitor", LOG_PID, LOG_DAEMON);
  syslog(LOG_INFO, "cpu load1=%.2f load5=%.2f load15=%.2f",
         cpuMetrics->load1min, cpuMetrics->load5min, cpuMetrics->load15min);
  closelog();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Static functions — Memory
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void journalctl_log_mem(const MemStats *memStats);
static void syslog_log_mem(const MemStats *memStats);

extern void sink_log_mem(const MemStats *memStats) {
  if (activeTarget == LOG_TARGET_SYSLOG) {
    syslog_log_mem(memStats);
  } else {
    journalctl_log_mem(memStats);
  }
}

/**
 * @brief Write a memory snapshot to the systemd journal.
 *
 * Formats the key MemStats fields into a single MEM_STATS journal field.
 *
 * @param memStats  Memory metrics snapshot. Must not be NULL.
 */
static void journalctl_log_mem(const MemStats *memStats) {
  char buf[1024];
  snprintf(
      buf, sizeof(buf),
      "MemTotal:%lu MemFree:%lu MemAvail:%lu Buffers:%lu Cached:%lu "
      "SwapTotal:%lu SwapFree:%lu Active:%lu Inactive:%lu Shmem:%lu Slab:%lu",
      memStats->memTotal, memStats->memFree, memStats->memAvailable,
      memStats->buffers, memStats->cached, memStats->swapTotal,
      memStats->swapFree, memStats->active, memStats->inactive, memStats->shmem,
      memStats->slab);

  sd_journal_send("MESSAGE=memory metrics snapshot",
                  "MESSAGE_ID=510ce2bb9c3c407092acbbec567f4593", "PRIORITY=5",
                  "MEM_STATS=%s", buf, NULL);
}

/**
 * @brief Write a memory snapshot to syslog.
 *
 * Logs only the three most-used fields (detailed data requires journalctl).
 *
 * @param memStats  Memory metrics snapshot. Must not be NULL.
 */
static void syslog_log_mem(const MemStats *memStats) {
  openlog("embedded-monitor", LOG_PID, LOG_DAEMON);
  syslog(LOG_INFO, "mem total=%lu free=%lu avail=%lu", memStats->memTotal,
         memStats->memFree, memStats->memAvailable);
  closelog();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Static functions — IRQ
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void journalctl_log_irq(const IrqEntry *irqData, uint8_t nbLines,
                               uint8_t nbCpu, bool firstAcq);
static void syslog_log_irq(const IrqEntry *irqData, uint8_t nbLines,
                           uint8_t nbCpu, bool firstAcq);

extern void sink_log_irq(const IrqEntry *irqData, uint8_t nbLines,
                         uint8_t nbCpu, bool firstAcq) {
  if (activeTarget == LOG_TARGET_SYSLOG) {
    syslog_log_irq(irqData, nbLines, nbCpu, firstAcq);
  } else {
    journalctl_log_irq(irqData, nbLines, nbCpu, firstAcq);
  }
}

/**
 * @brief Write an IRQ snapshot to the systemd journal.
 *
 * Skips unchanged lines (deltaPerCpu all zero) unless firstAcq is true.
 *
 * @param irqData   IRQ entry array. Must not be NULL.
 * @param nbLines   Number of entries.
 * @param nbCpu     Number of online CPUs.
 * @param firstAcq  If true, log all lines regardless of delta.
 */
static void journalctl_log_irq(const IrqEntry *irqData, uint8_t nbLines,
                               uint8_t nbCpu, bool firstAcq) {
  char irqDataStr[8192] = "";

  for (uint8_t i = 0; i < nbLines; i++) {
    bool hasChanged = false;
    for (uint8_t k = 0; k < nbCpu; k++) {
      if (irqData[i].deltaPerCpu[k] != 0) {
        hasChanged = true;
        break;
      }
    }
    if (hasChanged == false && firstAcq == false)
      continue;

    char line[512];
    int pos = snprintf(line, sizeof(line), "id:%s\t", irqData[i].id);
    for (uint8_t k = 0; k < nbCpu; k++) {
      pos += snprintf(line + pos, sizeof(line) - pos, "cpu%u:%u\t", k,
                      irqData[i].deltaPerCpu[k]);
    }
    snprintf(line + pos, sizeof(line) - pos, "description:%s\n",
             irqData[i].description);
    strncat(irqDataStr, line, sizeof(irqDataStr) - strlen(irqDataStr) - 1);
  }

  sd_journal_send("MESSAGE=irq metrics snapshot",
                  "MESSAGE_ID=5dc39e996fd64d0d9acbbeee1db3fd4b", "PRIORITY=5",
                  "NB_CPUS=%u", (unsigned)nbCpu, "IRQ_DATA=%s", irqDataStr,
                  NULL);
}

/**
 * @brief Write an IRQ snapshot to syslog.
 *
 * Logs only the CPU count (detailed data requires journalctl).
 *
 * @param irqData   IRQ entry array (unused for syslog detail).
 * @param nbLines   Number of entries (unused for syslog detail).
 * @param nbCpu     Number of online CPUs.
 * @param firstAcq  First acquisition flag (unused for syslog detail).
 */
static void syslog_log_irq(const IrqEntry *irqData, uint8_t nbLines,
                           uint8_t nbCpu, bool firstAcq) {
  (void)irqData;
  (void)nbLines;
  (void)firstAcq;
  openlog("embedded-monitor", LOG_PID, LOG_DAEMON);
  syslog(LOG_INFO, "irq snapshot nbcpu=%u", (unsigned)nbCpu);
  closelog();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Static functions — Process
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void journalctl_log_proc(const ProcStatEntry *procData,
                                uint16_t nbProcess);
static void syslog_log_proc(const ProcStatEntry *procData, uint16_t nbProcess);

extern void sink_log_proc(const ProcStatEntry *procData, uint16_t nbProcess) {
  if (activeTarget == LOG_TARGET_SYSLOG) {
    syslog_log_proc(procData, nbProcess);
  } else {
    journalctl_log_proc(procData, nbProcess);
  }
}

/**
 * @brief Write a process snapshot to the systemd journal.
 *
 * Formats pid, comm, state, ppid, utime, stime and thread count
 * for each process into a single PROCESS_DATA journal field.
 *
 * @param procData   Process entry array. Must not be NULL.
 * @param nbProcess  Number of entries in procData.
 */
static void journalctl_log_proc(const ProcStatEntry *procData,
                                uint16_t nbProcess) {
  char processLine[300] = "";
  char procDataStr[50000] = "";

  for (uint16_t i = 0; i < nbProcess; i++) {
    strcpy(processLine, "");
    snprintf(processLine, sizeof(processLine),
             "pid:%hu\tcomm:%s\tstate:%c\tppid:%hu\tutime:%u\tstime:%"
             "u\tthreads:%u\n",
             procData[i].pid, procData[i].comm, procData[i].state,
             procData[i].ppid, procData[i].utime, procData[i].stime,
             procData[i].numThreads);
    strncat(procDataStr, processLine,
            sizeof(procDataStr) - strlen(procDataStr) - 1);
  }

  sd_journal_send("MESSAGE=process metrics snapshot",
                  "MESSAGE_ID=35fd4cc7bd5e419d801a090eb084693f", "PRIORITY=5",
                  "PROCESS_DATA=%s", procDataStr, NULL);
}

/**
 * @brief Write a process snapshot to syslog.
 *
 * Logs only the process count (detailed data requires journalctl).
 *
 * @param procData   Process entry array (unused for syslog detail).
 * @param nbProcess  Number of processes.
 */
static void syslog_log_proc(const ProcStatEntry *procData, uint16_t nbProcess) {
  (void)procData;
  openlog("embedded-monitor", LOG_PID, LOG_DAEMON);
  syslog(LOG_INFO, "proc snapshot nbprocess=%u", (unsigned)nbProcess);
  closelog();
}
