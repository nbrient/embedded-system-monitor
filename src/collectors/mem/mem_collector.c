/**
 * @file mem_collector.c
 *
 * @brief Memory metrics collector — reads /proc/meminfo.
 *
 * @par State machine
 * @verbatim
 *   S_RUNNING --E_TIME_OUT--> S_RUNNING  (A_COLLECT)
 *   S_RUNNING --E_STOP------> S_DEATH    (A_STOP)
 * @endverbatim
 *
 * @par /proc/meminfo parsing strategy
 * Each line is fed to apply_meminfo_field() which maps the key string
 * to the corresponding MemStats field. Unknown keys are silently ignored,
 * making the parser forward-compatible with future kernel additions.
 *
 * @version 1.0
 */

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Include
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "mem_collector.h"

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

/** @brief POSIX mqueue label for the memory collector. */
#define MQ_LABEL        "/MQ_MemCollector"

/** @brief Path to the memory information pseudo-file. */
#define MEM_INFO_FILE   "/proc/meminfo"

/** @brief Maximum number of messages that can be queued at once. */
#define MQ_MAX_MESSAGES (10)

/** @brief mqueue open flags. */
#define MQ_FLAGS        (O_CREAT | O_RDWR)

/** @brief mqueue permission bits. */
#define MQ_MODE         (S_IRUSR | S_IWUSR)

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Variable and private structures
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief States of the memory collector state machine.
 */
typedef enum {
    S_NONE    = 0, /**< Default state — should never be reached. */
    S_RUNNING,     /**< Normal operating state. */
    S_DEATH,       /**< Terminal state — worker thread exits. */
    S_NB_STATE     /**< Sentinel: total number of states. */
} StateMemCollector;

/**
 * @brief Events that drive the memory collector state machine.
 */
typedef enum {
    E_STOP    = 0, /**< Shutdown requested by mem_collector_ask_stop(). */
    E_TIME_OUT,    /**< Watchdog tick — time to collect a new snapshot. */
    E_NB_EVENT     /**< Sentinel: total number of events. */
} EventMemCollector;

/**
 * @brief Actions executed during a state machine transition.
 */
typedef enum {
    A_NONE    = 0, /**< No action — do nothing. */
    A_STOP,        /**< Stop the collector. */
    A_COLLECT,     /**< Read /proc/meminfo and log the snapshot. */
    A_NB_ACTION    /**< Sentinel: total number of actions. */
} ActionMemCollector;

/**
 * @brief Message structure written to and read from the collector mqueue.
 */
typedef struct {
    EventMemCollector event; /**< Event associated with this message. */
} MqMsgMemCollector;

/**
 * @brief One state machine transition: destination state + action to execute.
 */
typedef struct {
    StateMemCollector  destinationState; /**< State to enter after this transition. */
    ActionMemCollector action;           /**< Action to execute during this transition. */
} TransitionMemCollector;

/** @brief Current FSM state, updated inside the worker thread. */
static StateMemCollector currentState;

/**
 * @brief Transition table indexed by [current state][event].
 */
static const TransitionMemCollector stateMachine[S_NB_STATE][E_NB_EVENT] = {
    [S_RUNNING][E_TIME_OUT] = { S_RUNNING, A_COLLECT },
    [S_RUNNING][E_STOP]     = { S_DEATH,   A_STOP    },
};

/** @brief Worker thread handle. */
static pthread_t memThread;

/** @brief Mutex protecting memStats against concurrent access. */
static pthread_mutex_t myMutex = PTHREAD_MUTEX_INITIALIZER;

/** @brief POSIX message queue handle. */
static mqd_t memMq;

/** @brief Periodic watchdog timer used to trigger acquisitions. */
static Watchdog *wtdTimer;

/** @brief Runtime configuration loaded from monitor.yaml. */
static CollectorCfg memCfg;

/** @brief Latest memory snapshot populated by perform_collect(). */
static MemStats memStats;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Function prototype
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Map one /proc/meminfo key-value pair to the corresponding MemStats field.
 *
 * The trailing colon is stripped from the key before comparison.
 * Unknown keys are silently ignored.
 *
 * @param key  Field name string (colon not yet stripped).
 * @param val  Parsed numeric value in kB.
 */
static void apply_meminfo_field(const char *key, uint64_t val);

/**
 * @brief Read /proc/meminfo line-by-line and update memStats.
 *
 * @return 0 on success, -1 on I/O error.
 */
static int8_t update_mem_data(void);

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
static int8_t send_msg_mq(MqMsgMemCollector *msg);

/**
 * @brief Blocking read of one message from the mqueue.
 *
 * @param dest  Buffer to receive the message. Must not be NULL.
 * @return      0 on success, -1 on error.
 */
static int8_t read_msg_mq(MqMsgMemCollector *dest);

/**
 * @brief Dispatch the given FSM action.
 *
 * @param action  Action to execute.
 * @return        0 on success, -1 on error.
 */
static int8_t perform_action(ActionMemCollector action);

/**
 * @brief Worker thread entry point — drives the FSM until S_DEATH.
 *
 * @param _  Unused thread argument.
 * @return   NULL.
 */
static void *run_mem_collector(void *_);

/**
 * @brief A_COLLECT action: disarm watchdog, read /proc/meminfo, log, re-arm.
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

extern int8_t mem_collector_new(CollectorCfg cfg) {
    int8_t returnErr = 0;
    memCfg = cfg;

    if (memCfg.enabled == false) {
        return returnErr;
    }

    TRACE("[MemCollector] new\n");

    wtdTimer  = watchdog_create(memCfg.intervalSec * 1000000U, time_out);
    returnErr = set_up_mq();
    CHECK_ERR(returnErr < 0, "[MemCollector] Error during set_up_mq");

    if (returnErr >= 0) {
        returnErr = pthread_mutex_init(&myMutex, NULL);
        CHECK_ERR(returnErr < 0, "[MemCollector] Error when initialising the mutex");
    }

    CHECK_ERR(returnErr < 0, "[MemCollector] Error during initialisation");
    return returnErr;
}

extern int8_t mem_collector_free(void) {
    int8_t returnErr = 0;

    if (memCfg.enabled == false) {
        return returnErr;
    }

    TRACE("[MemCollector] free\n");

    watchdog_destroy(wtdTimer);
    returnErr = tear_down_mq();

    if (returnErr >= 0) {
        returnErr = pthread_mutex_destroy(&myMutex);
        CHECK_ERR(returnErr < 0, "[MemCollector] Error when destroying the mutex");
    }

    CHECK_ERR(returnErr < 0, "[MemCollector] Error during destruction");
    return returnErr;
}

extern int8_t mem_collector_ask_start(void) {
    int8_t returnErr = 0;

    if (memCfg.enabled == false) {
        return returnErr;
    }

    TRACE("[MemCollector] ask start\n");

    returnErr = pthread_create(&memThread, NULL, &run_mem_collector, NULL);
    CHECK_ERR(returnErr < 0, "[MemCollector] Error when creating the worker thread");

    currentState = S_RUNNING;
    watchdog_arm(wtdTimer);
    return returnErr;
}

extern int8_t mem_collector_ask_stop(void) {
    int8_t returnErr = 0;

    if (memCfg.enabled == false) {
        return returnErr;
    }

    TRACE("[MemCollector] ask stop\n");

    MqMsgMemCollector msg = { .event = E_STOP };
    returnErr = send_msg_mq(&msg);
    CHECK_ERR(returnErr < 0, "[MemCollector] Error when sending E_STOP");

    pthread_join(memThread, NULL);
    return returnErr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Static functions
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void apply_meminfo_field(const char *key, uint64_t val) {
#define M(f, m) if (strcmp(key, f) == 0) { memStats.m = val; return; }
    M("MemTotal",memTotal)         M("MemFree",memFree)
    M("MemAvailable",memAvailable) M("Buffers",buffers)
    M("Cached",cached)             M("SwapCached",swapCached)
    M("Active",active)             M("Inactive",inactive)
    M("SwapTotal",swapTotal)       M("SwapFree",swapFree)
    M("Dirty",dirty)               M("Writeback",writeback)
    M("Shmem",shmem)               M("Slab",slab)
    M("SReclaimable",sReclaimable) M("SUnreclaim",sUnreclaim)
    M("KernelStack",kernelStack)   M("PageTables",pageTables)
    M("VmallocTotal",vmallocTotal) M("VmallocUsed",vmallocUsed)
    M("CommitLimit",commitLimit)   M("Committed_AS",committed)
#undef M
}

static int8_t update_mem_data(void) {
    int8_t returnErr = 0;

    FILE *fileToRead = fopen(MEM_INFO_FILE, "r");

    if (fileToRead == NULL) {
        CHECK_ERR(true, "[MemCollector] Error when opening /proc/meminfo");
        returnErr = -1;
    } else if (!feof(fileToRead)) {
        char line[128];
        while (fgets(line, sizeof(line), fileToRead)) {
            char     key[64];
            uint64_t val;
            if (sscanf(line, "%63s %lu", key, &val) < 2) continue;
            size_t kl = strlen(key);
            if (kl > 0 && key[kl - 1] == ':') key[--kl] = '\0';
            apply_meminfo_field(key, val);
        }
        returnErr = fclose(fileToRead);
    } else {
        CHECK_ERR(true, "[MemCollector] /proc/meminfo is empty");
    }

    return returnErr;
}

static int8_t set_up_mq(void) {
    int8_t returnErr = 0;

    mq_unlink(MQ_LABEL);

    struct mq_attr attr;
    attr.mq_flags   = 0;
    attr.mq_maxmsg  = MQ_MAX_MESSAGES;
    attr.mq_msgsize = sizeof(MqMsgMemCollector);
    attr.mq_curmsgs = 0;

    memMq = mq_open(MQ_LABEL, MQ_FLAGS, MQ_MODE, &attr);

    if (memMq < 0) {
        CHECK_ERR(true, "[MemCollector] Fail to open the mqueue");
        returnErr = -1;
    }

    return returnErr;
}

static int8_t tear_down_mq(void) {
    int8_t returnErr = 0;

    returnErr = mq_unlink(MQ_LABEL);

    if (returnErr >= 0) {
        returnErr = mq_close(memMq);
        CHECK_ERR(returnErr < 0, "[MemCollector] Error when closing the mqueue");
    } else {
        CHECK_ERR(true, "[MemCollector] Error when unlinking the mqueue");
    }

    return returnErr;
}

static int8_t send_msg_mq(MqMsgMemCollector *msg) {
    int8_t returnErr = 0;
    returnErr = mq_send(memMq, (char *)msg, sizeof(MqMsgMemCollector), 0);
    CHECK_ERR(returnErr < 0, "[MemCollector] Error when sending a message");
    return returnErr;
}

static int8_t read_msg_mq(MqMsgMemCollector *dest) {
    int8_t returnErr = 0;
    returnErr = mq_receive(memMq, (char *)dest, sizeof(MqMsgMemCollector), NULL);
    CHECK_ERR(returnErr < 0, "[MemCollector] Error when receiving a message");
    return returnErr;
}

static void time_out(void) {
    TRACE("[MemCollector] time out\n");
    MqMsgMemCollector msg = { .event = E_TIME_OUT };
    send_msg_mq(&msg);
}

static void *run_mem_collector(void *_) {
    (void)_;
    while (currentState != S_DEATH) {
        MqMsgMemCollector msg;
        read_msg_mq(&msg);
        TransitionMemCollector transition = stateMachine[currentState][msg.event];
        if (transition.destinationState != S_NONE) {
            perform_action(transition.action);
            currentState = transition.destinationState;
        } else {
            TRACE("[MemCollector] FSM lost an event\n");
        }
    }
    return NULL;
}

static int8_t perform_action(ActionMemCollector action) {
    int8_t returnErr = 0;
    switch (action) {
        case A_NONE:    break;
        case A_COLLECT: returnErr = perform_collect(); break;
        case A_STOP:    returnErr = perform_stop();    break;
        default:        break;
    }
    CHECK_ERR(returnErr < 0, "[MemCollector] Error when performing action");
    return returnErr;
}

static int8_t perform_collect(void) {
    watchdog_disarm(wtdTimer);
    int8_t returnErr = 0;

    pthread_mutex_lock(&myMutex);
    CHECK_ERR(update_mem_data() < 0, "[MemCollector] Error updating memory data");
    pthread_mutex_unlock(&myMutex);

    sink_log_mem(&memStats);
    TRACE("[MemCollector] snapshot logged\n");

    watchdog_arm(wtdTimer);
    return returnErr;
}

static int8_t perform_stop(void) {
    int8_t returnErr = 0;
    watchdog_disarm(wtdTimer);

    MqMsgMemCollector msg = { .event = E_STOP };
    returnErr = send_msg_mq(&msg);

    if (returnErr < 0) {
        CHECK_ERR(true, "[MemCollector] Error when sending E_STOP in perform_stop");
    } else {
        returnErr = pthread_join(memThread, NULL);
        CHECK_ERR(returnErr < 0, "[MemCollector] Error when joining the worker thread");
    }

    CHECK_ERR(returnErr < 0, "[MemCollector] Error in perform_stop");
    return returnErr;
}
