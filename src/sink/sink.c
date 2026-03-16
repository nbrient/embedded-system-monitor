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
#include <unistd.h>
#include <syslog.h>
#include <systemd/sd-journal.h>

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
    char statBuf[512]     = "";
    char perCoreBuf[2048] = "";

    snprintf(loadBuf, sizeof(loadBuf),
        "load1=%.2f load5=%.2f load15=%.2f runnable=%d total=%d lastpid=%s",
        cpuMetrics->load1min,         cpuMetrics->load5min,
        cpuMetrics->load15min,        cpuMetrics->runnableEntities,
        cpuMetrics->totalEntities,    cpuMetrics->lastPid);

    snprintf(statBuf, sizeof(statBuf),
        "user:%u nice:%u system:%u idle:%u iowait:%u irq:%u softirq:%u",
        cpuMetrics->allCores.user,    cpuMetrics->allCores.nice,
        cpuMetrics->allCores.system,  cpuMetrics->allCores.idle,
        cpuMetrics->allCores.iowait,  cpuMetrics->allCores.irq,
        cpuMetrics->allCores.softirq);

    for (uint8_t i = 0; i < cpuMetrics->numCores; i++) {
        char coreEntry[256];
        snprintf(coreEntry, sizeof(coreEntry),
            "cpu%u user:%u nice:%u system:%u idle:%u iowait:%u irq:%u softirq:%u\n",
            i,
            cpuMetrics->perCore[i].user,    cpuMetrics->perCore[i].nice,
            cpuMetrics->perCore[i].system,  cpuMetrics->perCore[i].idle,
            cpuMetrics->perCore[i].iowait,  cpuMetrics->perCore[i].irq,
            cpuMetrics->perCore[i].softirq);
        strncat(perCoreBuf, coreEntry, sizeof(perCoreBuf) - strlen(perCoreBuf) - 1);
    }

    sd_journal_send(
        "MESSAGE=cpu metrics snapshot",
        "MESSAGE_ID=31ec43a6b4c24545bf21791c041c9f89",
        "PRIORITY=5",
        "NB_CORES=%u",     (unsigned)cpuMetrics->numCores,
        "CPU_LOAD=%s",     loadBuf,
        "CPU_STAT=%s",     statBuf,
        "CPU_PER_CORE=%s", perCoreBuf,
        NULL);
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
