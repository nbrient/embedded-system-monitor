/**
 * @file cpu_collector.c
 *
 * @brief CPU metrics collector — reads /proc/loadavg and /proc/stat.
 *
 * @par State machine
 * @verbatim
 *   S_RUNNING --E_TIME_OUT--> S_RUNNING  (A_COLLECT)
 *   S_RUNNING --E_STOP------> S_DEATH    (A_STOP)
 * @endverbatim
 *
 * @version 1.0
 */

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Include
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "cpu_collector.h"

#include <errno.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>

#include "../../watchdog/watchdog.h"
#include "../../core/types.h"
#include "../../core/debug.h"
#include "../../sink/sink.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Define
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** @brief POSIX mqueue label for the CPU collector. */
#define MQ_LABEL        "/MQ_CpuCollector"

/** @brief Path to the load-average pseudo-file. */
#define CPU_AVG_FILE    "/proc/loadavg"

/** @brief Path to the CPU statistics pseudo-file. */
#define STAT_FILE       "/proc/stat"

/** @brief Maximum number of messages that can be queued at once. */
#define MQ_MAX_MESSAGES (10)

/** @brief mqueue open flags: create if absent, open for reading and writing. */
#define MQ_FLAGS        (O_CREAT | O_RDWR)

/** @brief mqueue permission bits: owner read + write. */
#define MQ_MODE         (S_IRUSR | S_IWUSR)

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Variable and private structures
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief States of the CPU collector state machine.
 */
typedef enum {
    S_NONE    = 0, /**< Default state — should never be reached. */
    S_RUNNING,     /**< Normal operating state. */
    S_DEATH,       /**< Terminal state — worker thread exits. */
    S_NB_STATE     /**< Sentinel: total number of states. */
} StateCpuCollector;

/**
 * @brief Events that drive the CPU collector state machine.
 */
typedef enum {
    E_STOP    = 0, /**< Shutdown requested by cpu_collector_ask_stop(). */
    E_TIME_OUT,    /**< Watchdog tick — time to collect a new snapshot. */
    E_NB_EVENT     /**< Sentinel: total number of events. */
} EventCpuCollector;

/**
 * @brief Actions executed during a state machine transition.
 */
typedef enum {
    A_NONE    = 0, /**< No action — do nothing. */
    A_STOP,        /**< Stop the collector (disarm watchdog, exit loop). */
    A_COLLECT,     /**< Read /proc files and log the snapshot. */
    A_NB_ACTION    /**< Sentinel: total number of actions. */
} ActionCpuCollector;

/**
 * @brief Message structure written to and read from the collector mqueue.
 */
typedef struct {
    EventCpuCollector event; /**< Event associated with this message. */
} MqMsgCpuCollector;

/**
 * @brief One state machine transition: destination state + action to execute.
 */
typedef struct {
    StateCpuCollector  destinationState; /**< State to enter after this transition. */
    ActionCpuCollector action;           /**< Action to execute during this transition. */
} TransitionCpuCollector;

/** @brief Current FSM state, updated inside the worker thread. */
static StateCpuCollector currentState;

/**
 * @brief Transition table indexed by [current state][event].
 */
static const TransitionCpuCollector stateMachine[S_NB_STATE][E_NB_EVENT] = {
    [S_RUNNING][E_TIME_OUT] = { S_RUNNING, A_COLLECT },
    [S_RUNNING][E_STOP]     = { S_DEATH,   A_STOP    },
};

/** @brief Worker thread handle. */
static pthread_t cpuThread;

/** @brief Mutex protecting cpuMetrics against concurrent access. */
static pthread_mutex_t myMutex = PTHREAD_MUTEX_INITIALIZER;

/** @brief POSIX message queue handle. */
static mqd_t cpuMq;

/** @brief Periodic watchdog timer used to trigger acquisitions. */
static Watchdog *wtdTimer;

/** @brief Runtime configuration loaded from monitor.yaml. */
static CollectorCfg cpuCfg;

/** @brief Latest CPU snapshot populated by perform_collect(). */
static CpuMetrics cpuMetrics;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Function prototype
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Read /proc/loadavg and update the load fields of cpuMetrics.
 *
 * @return 0 on success, -1 on I/O error.
 */
static int8_t update_load_average(void);

/**
 * @brief Read /proc/stat and update the jiffies fields of cpuMetrics.
 *
 * @return 0 on success, -1 on I/O error.
 */
static int8_t update_cpu_stat(void);

/**
 * @brief Open and configure the POSIX mqueue.
 *
 * @return 0 on success, -1 on error.
 */
static int8_t set_up_mq(void);

/**
 * @brief Close and unlink the POSIX mqueue.
 *
 * @return 0 on success, -1 on error.
 */
static int8_t tear_down_mq(void);

/**
 * @brief Write a message into the mqueue.
 *
 * @param msg  Message to enqueue. Must not be NULL.
 * @return     0 on success, -1 on error.
 */
static int8_t send_msg_mq(MqMsgCpuCollector *msg);

/**
 * @brief Blocking read of one message from the mqueue.
 *
 * @param dest  Buffer to receive the message. Must not be NULL.
 * @return      0 on success, -1 on error.
 */
static int8_t read_msg_mq(MqMsgCpuCollector *dest);

/**
 * @brief Dispatch the given FSM action.
 *
 * @param action  Action to execute.
 * @return        0 on success, -1 on error.
 */
static int8_t perform_action(ActionCpuCollector action);

/**
 * @brief Worker thread entry point — drives the FSM until S_DEATH.
 *
 * @param _  Unused thread argument.
 * @return   NULL.
 */
static void *run_cpu_collector(void *_);

/**
 * @brief A_COLLECT action: disarm watchdog, read /proc, log snapshot, re-arm.
 *
 * @return 0 on success, -1 on error.
 */
static int8_t perform_collect(void);

/**
 * @brief A_STOP action: disarm watchdog and re-enqueue E_STOP to exit the loop.
 *
 * @return 0 on success, -1 on error.
 */
static int8_t perform_stop(void);

/**
 * @brief Watchdog callback — pushes E_TIME_OUT into the mqueue.
 */
static void time_out(void);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Public functions
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern int8_t cpu_collector_new(CollectorCfg cfg) {
    int8_t returnErr = 0;
    cpuCfg = cfg;

    if (cpuCfg.enabled == false) {
        return returnErr;
    }

    TRACE("[CpuCollector] new\n");

    cpuMetrics.numCores = (uint8_t)sysconf(_SC_NPROCESSORS_ONLN);
    cpuMetrics.perCore  = calloc(cpuMetrics.numCores, sizeof(CoreTimes));

    wtdTimer  = watchdog_create(cpuCfg.intervalSec * 1000000U, time_out);
    returnErr = set_up_mq();
    CHECK_ERR(returnErr < 0, "[CpuCollector] Error during set_up_mq");

    if (returnErr >= 0) {
        returnErr = pthread_mutex_init(&myMutex, NULL);
        CHECK_ERR(returnErr < 0, "[CpuCollector] Error when initialising the mutex");
    }

    CHECK_ERR(returnErr < 0, "[CpuCollector] Error during initialisation");
    return returnErr;
}

extern int8_t cpu_collector_free(void) {
    int8_t returnErr = 0;

    if (cpuCfg.enabled == false) {
        return returnErr;
    }

    TRACE("[CpuCollector] free\n");

    watchdog_destroy(wtdTimer);
    free(cpuMetrics.perCore);

    returnErr = tear_down_mq();

    if (returnErr >= 0) {
        returnErr = pthread_mutex_destroy(&myMutex);
        CHECK_ERR(returnErr < 0, "[CpuCollector] Error when destroying the mutex");
    }

    CHECK_ERR(returnErr < 0, "[CpuCollector] Error during destruction");
    return returnErr;
}

extern int8_t cpu_collector_ask_start(void) {
    int8_t returnErr = 0;

    if (cpuCfg.enabled == false) {
        return returnErr;
    }

    TRACE("[CpuCollector] ask start\n");

    returnErr = pthread_create(&cpuThread, NULL, &run_cpu_collector, NULL);
    CHECK_ERR(returnErr < 0, "[CpuCollector] Error when creating the worker thread");

    currentState = S_RUNNING;
    watchdog_arm(wtdTimer);

    return returnErr;
}

extern int8_t cpu_collector_ask_stop(void) {
    int8_t returnErr = 0;

    if (cpuCfg.enabled == false) {
        return returnErr;
    }

    TRACE("[CpuCollector] ask stop\n");

    MqMsgCpuCollector msg = { .event = E_STOP };
    returnErr = send_msg_mq(&msg);
    CHECK_ERR(returnErr < 0, "[CpuCollector] Error when sending E_STOP");

    pthread_join(cpuThread, NULL);
    return returnErr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Static functions
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int8_t update_load_average(void) {
    int8_t returnErr = 0;

    FILE *fileToRead = fopen(CPU_AVG_FILE, "r");

    if (fileToRead == NULL) {
        CHECK_ERR(true, "[CpuCollector] Error when opening /proc/loadavg");
        returnErr = -1;
    } else if (!feof(fileToRead)) {
        char line[128];
        if (fgets(line, sizeof(line), fileToRead)) {
            sscanf(line, "%f %f %f %d/%d %15s",
                   &cpuMetrics.load1min,
                   &cpuMetrics.load5min,
                   &cpuMetrics.load15min,
                   &cpuMetrics.runnableEntities,
                   &cpuMetrics.totalEntities,
                   cpuMetrics.lastPid);
        }
        returnErr = fclose(fileToRead);
    } else {
        CHECK_ERR(true, "[CpuCollector] /proc/loadavg is empty");
    }

    return returnErr;
}

static int8_t update_cpu_stat(void) {
    int8_t returnErr = 0;

    FILE *fileToRead = fopen(STAT_FILE, "r");

    if (fileToRead == NULL) {
        CHECK_ERR(true, "[CpuCollector] Error when opening /proc/stat");
        returnErr = -1;
    } else if (!feof(fileToRead)) {
        char    line[256];
        uint8_t coreIdx = 0;

        while (fgets(line, sizeof(line), fileToRead)) {
            char      tag[16];
            CoreTimes ct = { 0 };

            if (sscanf(line, "%15s %u %u %u %u %u %u %u", tag,
                       &ct.user, &ct.nice, &ct.system, &ct.idle,
                       &ct.iowait, &ct.irq, &ct.softirq) < 8) continue;

            if (strcmp(tag, "cpu") == 0) {
                cpuMetrics.allCores = ct;
            } else if (strncmp(tag, "cpu", 3) == 0 && coreIdx < cpuMetrics.numCores) {
                cpuMetrics.perCore[coreIdx++] = ct;
            }
        }

        returnErr = fclose(fileToRead);
    } else {
        CHECK_ERR(true, "[CpuCollector] /proc/stat is empty");
    }

    return returnErr;
}

static int8_t set_up_mq(void) {
    int8_t returnErr = 0;

    mq_unlink(MQ_LABEL);

    struct mq_attr attr;
    attr.mq_flags   = 0;
    attr.mq_maxmsg  = MQ_MAX_MESSAGES;
    attr.mq_msgsize = sizeof(MqMsgCpuCollector);
    attr.mq_curmsgs = 0;

    cpuMq = mq_open(MQ_LABEL, MQ_FLAGS, MQ_MODE, &attr);

    if (cpuMq < 0) {
        CHECK_ERR(true, "[CpuCollector] Fail to open the mqueue");
        returnErr = -1;
    }

    return returnErr;
}

static int8_t tear_down_mq(void) {
    int8_t returnErr = 0;

    returnErr = mq_unlink(MQ_LABEL);

    if (returnErr >= 0) {
        returnErr = mq_close(cpuMq);
        CHECK_ERR(returnErr < 0, "[CpuCollector] Error when closing the mqueue");
    } else {
        CHECK_ERR(true, "[CpuCollector] Error when unlinking the mqueue");
    }

    return returnErr;
}

static int8_t send_msg_mq(MqMsgCpuCollector *msg) {
    int8_t returnErr = 0;

    returnErr = mq_send(cpuMq, (char *)msg, sizeof(MqMsgCpuCollector), 0);
    CHECK_ERR(returnErr < 0, "[CpuCollector] Error when sending a message");

    return returnErr;
}

static int8_t read_msg_mq(MqMsgCpuCollector *dest) {
    int8_t returnErr = 0;

    returnErr = mq_receive(cpuMq, (char *)dest, sizeof(MqMsgCpuCollector), NULL);
    CHECK_ERR(returnErr < 0, "[CpuCollector] Error when receiving a message");

    return returnErr;
}

static void time_out(void) {
    TRACE("[CpuCollector] time out\n");
    MqMsgCpuCollector msg = { .event = E_TIME_OUT };
    send_msg_mq(&msg);
}

static void *run_cpu_collector(void *_) {
    (void)_;

    while (currentState != S_DEATH) {
        MqMsgCpuCollector msg;
        read_msg_mq(&msg);

        TransitionCpuCollector transition = stateMachine[currentState][msg.event];

        if (transition.destinationState != S_NONE) {
            perform_action(transition.action);
            currentState = transition.destinationState;
        } else {
            TRACE("[CpuCollector] FSM lost an event\n");
        }
    }

    return NULL;
}

static int8_t perform_action(ActionCpuCollector action) {
    int8_t returnErr = 0;

    switch (action) {
        case A_NONE:    break;
        case A_COLLECT: returnErr = perform_collect(); break;
        case A_STOP:    returnErr = perform_stop();    break;
        default:        break;
    }

    CHECK_ERR(returnErr < 0, "[CpuCollector] Error when performing action");
    return returnErr;
}

static int8_t perform_collect(void) {
    watchdog_disarm(wtdTimer);
    int8_t returnErr = 0;

    pthread_mutex_lock(&myMutex);
    CHECK_ERR(update_load_average() < 0, "[CpuCollector] Error updating load average");
    CHECK_ERR(update_cpu_stat()     < 0, "[CpuCollector] Error updating cpu stat");
    pthread_mutex_unlock(&myMutex);

    sink_log_cpu(&cpuMetrics);
    TRACE("[CpuCollector] snapshot logged\n");

    watchdog_arm(wtdTimer);
    return returnErr;
}

static int8_t perform_stop(void) {
    int8_t returnErr = 0;
    watchdog_disarm(wtdTimer);

    MqMsgCpuCollector msg = { .event = E_STOP };
    returnErr = send_msg_mq(&msg);

    if (returnErr < 0) {
        CHECK_ERR(true, "[CpuCollector] Error when sending E_STOP in perform_stop");
    } else {
        returnErr = pthread_join(cpuThread, NULL);
        CHECK_ERR(returnErr < 0, "[CpuCollector] Error when joining the worker thread");
    }

    CHECK_ERR(returnErr < 0, "[CpuCollector] Error in perform_stop");
    return returnErr;
}
