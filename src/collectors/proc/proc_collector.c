/**
 * @file proc_collector.c
 *
 * @brief Process collector — enumerates /proc and reads /proc/[pid]/stat.
 *
 * @par State machine
 * @verbatim
 *   S_RUNNING --E_TIME_OUT--> S_RUNNING  (A_COLLECT)
 *   S_RUNNING --E_STOP------> S_DEATH    (A_STOP)
 * @endverbatim
 *
 * @par PID enumeration strategy
 * get_number_process() and update_pid_list() both open /proc and iterate
 * directory entries, keeping only those whose name is entirely numeric.
 * The two-step approach (count then fill) avoids realloc.
 *
 * @par /proc/[pid]/stat parsing strategy
 * A single sscanf call with %*u skips for the ~30 unused fields extracts
 * only the ten ProcStatEntry fields we need.
 *
 * @version 1.0
 */

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Include
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "proc_collector.h"

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
#include <ctype.h>
#include <dirent.h>

#include "../../watchdog/watchdog.h"
#include "../../core/types.h"
#include "../../core/debug.h"
#include "../../sink/sink.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Define
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** @brief POSIX mqueue label for the process collector. */
#define MQ_LABEL        "/MQ_ProcCollector"

/** @brief Path to the /proc virtual filesystem. */
#define PROC_DIR        "/proc"

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
 * @brief States of the process collector state machine.
 */
typedef enum {
    S_NONE    = 0, /**< Default state — should never be reached. */
    S_RUNNING,     /**< Normal operating state. */
    S_DEATH,       /**< Terminal state — worker thread exits. */
    S_NB_STATE     /**< Sentinel: total number of states. */
} StateProcCollector;

/**
 * @brief Events that drive the process collector state machine.
 */
typedef enum {
    E_STOP    = 0, /**< Shutdown requested by proc_collector_ask_stop(). */
    E_TIME_OUT,    /**< Watchdog tick — time to collect a new snapshot. */
    E_NB_EVENT     /**< Sentinel: total number of events. */
} EventProcCollector;

/**
 * @brief Actions executed during a state machine transition.
 */
typedef enum {
    A_NONE    = 0, /**< No action — do nothing. */
    A_STOP,        /**< Stop the collector. */
    A_COLLECT,     /**< Enumerate /proc and read per-PID stat files. */
    A_NB_ACTION    /**< Sentinel: total number of actions. */
} ActionProcCollector;

/**
 * @brief Message structure written to and read from the collector mqueue.
 */
typedef struct {
    EventProcCollector event; /**< Event associated with this message. */
} MqMsgProcCollector;

/**
 * @brief One state machine transition: destination state + action to execute.
 */
typedef struct {
    StateProcCollector  destinationState; /**< State to enter after this transition. */
    ActionProcCollector action;           /**< Action to execute during this transition. */
} TransitionProcCollector;

/** @brief Current FSM state, updated inside the worker thread. */
static StateProcCollector currentState;

/**
 * @brief Transition table indexed by [current state][event].
 */
static const TransitionProcCollector stateMachine[S_NB_STATE][E_NB_EVENT] = {
    [S_RUNNING][E_TIME_OUT] = { S_RUNNING, A_COLLECT },
    [S_RUNNING][E_STOP]     = { S_DEATH,   A_STOP    },
};

/** @brief Worker thread handle. */
static pthread_t procThread;

/** @brief Mutex protecting procData against concurrent access. */
static pthread_mutex_t myMutex = PTHREAD_MUTEX_INITIALIZER;

/** @brief POSIX message queue handle. */
static mqd_t procMq;

/** @brief Periodic watchdog timer used to trigger acquisitions. */
static Watchdog *wtdTimer;

/** @brief Runtime configuration loaded from monitor.yaml. */
static CollectorCfg procCfg;

/** @brief Heap-allocated array of process snapshots, reallocated on each tick. */
static ProcStatEntry *procData;

/** @brief Number of valid entries in procData after the last acquisition. */
static uint16_t nbProcess;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Function prototype
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Count the number of numeric (PID) directories in /proc.
 *
 * @return Number of PID directories found, or 0 on error.
 */
static uint16_t get_number_process(void);

/**
 * @brief Fill pidList with the numeric directory names found in /proc.
 *
 * @param pidList  Output array. Must have room for at least @p maxPids entries.
 * @param maxPids  Maximum number of PIDs to write into @p pidList.
 */
static void update_pid_list(uint16_t *pidList, uint16_t maxPids);

/**
 * @brief Enumerate all PIDs and read each /proc/[pid]/stat.
 *
 * Reallocates procData on every call. PIDs that have exited between
 * enumeration and open are silently skipped.
 *
 * @return 0 on success, -1 on error.
 */
static int8_t update_proc_data(void);

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
static int8_t send_msg_mq(MqMsgProcCollector *msg);

/**
 * @brief Blocking read of one message from the mqueue.
 *
 * @param dest  Buffer to receive the message. Must not be NULL.
 * @return      0 on success, -1 on error.
 */
static int8_t read_msg_mq(MqMsgProcCollector *dest);

/**
 * @brief Dispatch the given FSM action.
 *
 * @param action  Action to execute.
 * @return        0 on success, -1 on error.
 */
static int8_t perform_action(ActionProcCollector action);

/**
 * @brief Worker thread entry point — drives the FSM until S_DEATH.
 *
 * @param _  Unused thread argument.
 * @return   NULL.
 */
static void *run_proc_collector(void *_);

/**
 * @brief A_COLLECT action: enumerate PIDs, read stat files, log snapshot.
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

extern int8_t proc_collector_new(CollectorCfg cfg) {
    int8_t returnErr = 0;
    procCfg = cfg;

    if (procCfg.enabled == false) return returnErr;

    TRACE("[ProcCollector] new\n");

    wtdTimer  = watchdog_create(procCfg.intervalSec * 1000000U, time_out);
    returnErr = set_up_mq();
    CHECK_ERR(returnErr < 0, "[ProcCollector] Error during set_up_mq");

    if (returnErr >= 0) {
        returnErr = pthread_mutex_init(&myMutex, NULL);
        CHECK_ERR(returnErr < 0, "[ProcCollector] Error when initialising the mutex");
    }

    CHECK_ERR(returnErr < 0, "[ProcCollector] Error during initialisation");
    return returnErr;
}

extern int8_t proc_collector_free(void) {
    int8_t returnErr = 0;

    if (procCfg.enabled == false) return returnErr;

    TRACE("[ProcCollector] free\n");

    watchdog_destroy(wtdTimer);
    free(procData);
    returnErr = tear_down_mq();

    if (returnErr >= 0) {
        returnErr = pthread_mutex_destroy(&myMutex);
        CHECK_ERR(returnErr < 0, "[ProcCollector] Error when destroying the mutex");
    }

    CHECK_ERR(returnErr < 0, "[ProcCollector] Error during destruction");
    return returnErr;
}

extern int8_t proc_collector_ask_start(void) {
    int8_t returnErr = 0;

    if (procCfg.enabled == false) return returnErr;

    TRACE("[ProcCollector] ask start\n");

    returnErr = pthread_create(&procThread, NULL, &run_proc_collector, NULL);
    CHECK_ERR(returnErr < 0, "[ProcCollector] Error when creating the worker thread");

    currentState = S_RUNNING;
    watchdog_arm(wtdTimer);
    return returnErr;
}

extern int8_t proc_collector_ask_stop(void) {
    int8_t returnErr = 0;

    if (procCfg.enabled == false) return returnErr;

    TRACE("[ProcCollector] ask stop\n");

    MqMsgProcCollector msg = { .event = E_STOP };
    returnErr = send_msg_mq(&msg);
    CHECK_ERR(returnErr < 0, "[ProcCollector] Error when sending E_STOP");

    pthread_join(procThread, NULL);
    return returnErr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Static functions
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint16_t get_number_process(void) {
    uint16_t count = 0;
    struct dirent *dir;
    DIR *dp = opendir(PROC_DIR);

    if (dp == NULL) return 0;

    while ((dir = readdir(dp)) != NULL) {
        if (dir->d_type == DT_DIR) {
            bool isDigit = true;
            for (int i = 0; dir->d_name[i] != '\0'; i++) {
                if (!isdigit((unsigned char)dir->d_name[i])) {
                    isDigit = false;
                    break;
                }
            }
            if (isDigit == true) count++;
        }
    }

    closedir(dp);
    return count;
}

static void update_pid_list(uint16_t *pidList, uint16_t maxPids) {
    struct dirent *dir;
    DIR *dp = opendir(PROC_DIR);
    uint16_t idx = 0;

    if (dp == NULL) return;

    while ((dir = readdir(dp)) != NULL && idx < maxPids) {
        if (dir->d_type == DT_DIR) {
            bool isDigit = true;
            for (int i = 0; dir->d_name[i] != '\0'; i++) {
                if (!isdigit((unsigned char)dir->d_name[i])) {
                    isDigit = false;
                    break;
                }
            }
            if (isDigit == true) {
                pidList[idx] = (uint16_t)atoi(dir->d_name);
                idx++;
            }
        }
    }

    closedir(dp);
}

static int8_t update_proc_data(void) {
    int8_t returnErr = 0;

    pthread_mutex_lock(&myMutex);

    nbProcess = get_number_process();
    free(procData);
    procData = calloc(nbProcess, sizeof(ProcStatEntry));

    uint16_t *pidList = calloc(nbProcess, sizeof(uint16_t));
    update_pid_list(pidList, nbProcess);

    for (uint16_t i = 0; i < nbProcess; i++) {
        char pidPath[64];
        snprintf(pidPath, sizeof(pidPath), "/proc/%u/stat", pidList[i]);

        FILE *fileToRead = fopen(pidPath, "r");

        if (fileToRead == NULL) {
            /* process exited between enumeration and open — skip silently */
            continue;
        } else if (!feof(fileToRead)) {
            char line[512];
            if (fgets(line, sizeof(line), fileToRead)) {
                unsigned pid, ppid, ut, st, pri, nth, vsz, rss;
                char     comm[64];
                char     state;

                int matched = sscanf(line,
                    "%u (%63[^)]) %c %u %*u %*u %*u %*d %*u "
                    "%*u %*u %*u %*u %u %u "
                    "%*d %*d %u %*d %u "
                    "%*u %*u %u %u",
                    &pid, comm, &state, &ppid,
                    &ut, &st, &pri, &nth, &vsz, &rss);

                if (matched >= 10) {
                    procData[i].pid        = (uint16_t)pid;
                    procData[i].ppid       = (uint16_t)ppid;
                    procData[i].state      = state;
                    procData[i].utime      = ut;
                    procData[i].stime      = st;
                    procData[i].priority   = pri;
                    procData[i].numThreads = nth;
                    procData[i].vsize      = vsz;
                    procData[i].rss        = rss;
                    strncpy(procData[i].comm, comm, sizeof(procData[i].comm) - 1);
                }
            }
            fclose(fileToRead);
        } else {
            CHECK_ERR(true, "[ProcCollector] stat file is empty");
            fclose(fileToRead);
        }
    }

    free(pidList);
    pthread_mutex_unlock(&myMutex);
    return returnErr;
}

static int8_t set_up_mq(void) {
    int8_t returnErr = 0;
    mq_unlink(MQ_LABEL);
    struct mq_attr attr;
    attr.mq_flags   = 0;
    attr.mq_maxmsg  = MQ_MAX_MESSAGES;
    attr.mq_msgsize = sizeof(MqMsgProcCollector);
    attr.mq_curmsgs = 0;
    procMq = mq_open(MQ_LABEL, MQ_FLAGS, MQ_MODE, &attr);
    if (procMq < 0) { CHECK_ERR(true, "[ProcCollector] Fail to open the mqueue"); returnErr = -1; }
    return returnErr;
}

static int8_t tear_down_mq(void) {
    int8_t returnErr = mq_unlink(MQ_LABEL);
    if (returnErr >= 0) { returnErr = mq_close(procMq); CHECK_ERR(returnErr < 0, "[ProcCollector] Error closing mqueue"); }
    else { CHECK_ERR(true, "[ProcCollector] Error unlinking mqueue"); }
    return returnErr;
}

static int8_t send_msg_mq(MqMsgProcCollector *msg) {
    int8_t returnErr = mq_send(procMq, (char *)msg, sizeof(MqMsgProcCollector), 0);
    CHECK_ERR(returnErr < 0, "[ProcCollector] Error when sending a message");
    return returnErr;
}

static int8_t read_msg_mq(MqMsgProcCollector *dest) {
    int8_t returnErr = mq_receive(procMq, (char *)dest, sizeof(MqMsgProcCollector), NULL);
    CHECK_ERR(returnErr < 0, "[ProcCollector] Error when receiving a message");
    return returnErr;
}

static void time_out(void) {
    TRACE("[ProcCollector] time out\n");
    MqMsgProcCollector msg = { .event = E_TIME_OUT };
    send_msg_mq(&msg);
}

static void *run_proc_collector(void *_) {
    (void)_;
    while (currentState != S_DEATH) {
        MqMsgProcCollector msg;
        read_msg_mq(&msg);
        TransitionProcCollector transition = stateMachine[currentState][msg.event];
        if (transition.destinationState != S_NONE) {
            perform_action(transition.action);
            currentState = transition.destinationState;
        } else {
            TRACE("[ProcCollector] FSM lost an event\n");
        }
    }
    return NULL;
}

static int8_t perform_action(ActionProcCollector action) {
    int8_t returnErr = 0;
    switch (action) {
        case A_NONE:    break;
        case A_COLLECT: returnErr = perform_collect(); break;
        case A_STOP:    returnErr = perform_stop();    break;
        default:        break;
    }
    CHECK_ERR(returnErr < 0, "[ProcCollector] Error when performing action");
    return returnErr;
}

static int8_t perform_collect(void) {
    watchdog_disarm(wtdTimer);
    int8_t returnErr = 0;

    CHECK_ERR(update_proc_data() < 0, "[ProcCollector] Error updating process data");
    sink_log_proc(procData, nbProcess);
    TRACE("[ProcCollector] %u processes logged\n", nbProcess);

    watchdog_arm(wtdTimer);
    return returnErr;
}

static int8_t perform_stop(void) {
    int8_t returnErr = 0;
    watchdog_disarm(wtdTimer);

    MqMsgProcCollector msg = { .event = E_STOP };
    returnErr = send_msg_mq(&msg);

    if (returnErr < 0) {
        CHECK_ERR(true, "[ProcCollector] Error sending E_STOP in perform_stop");
    } else {
        returnErr = pthread_join(procThread, NULL);
        CHECK_ERR(returnErr < 0, "[ProcCollector] Error when joining the worker thread");
    }

    CHECK_ERR(returnErr < 0, "[ProcCollector] Error in perform_stop");
    return returnErr;
}
