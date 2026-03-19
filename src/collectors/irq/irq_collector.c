/**
 * @file irq_collector.c
 *
 * @brief IRQ collector — reads /proc/interrupts and tracks per-CPU deltas.
 *
 * @par State machine
 * @verbatim
 *   S_RUNNING --E_TIME_OUT--> S_RUNNING  (A_COLLECT)
 *   S_RUNNING --E_STOP------> S_DEATH    (A_STOP)
 * @endverbatim
 *
 * @par /proc/interrupts parsing strategy
 * The file is read line-by-line with fgets().  On the first acquisition
 * (firstAcq == true) the IRQ id and description fields are stored.
 * On subsequent acquisitions only the per-CPU raw counts are updated and
 * deltaPerCpu = raw - raw_previous is computed for each CPU.
 *
 * @version 1.0
 */

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Include
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "irq_collector.h"

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

#include "../../watchdog/watchdog.h"
#include "../../core/types.h"
#include "../../core/debug.h"
#include "../../sink/sink.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Define
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** @brief POSIX mqueue label for the IRQ collector. */
#define MQ_LABEL        "/MQ_IrqCollector"

/** @brief Path to the interrupt counters pseudo-file. */
#define IRQ_PROC_FILE   "/proc/interrupts"

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
 * @brief States of the IRQ collector state machine.
 */
typedef enum {
    S_NONE    = 0, /**< Default state — should never be reached. */
    S_RUNNING,     /**< Normal operating state. */
    S_DEATH,       /**< Terminal state — worker thread exits. */
    S_NB_STATE     /**< Sentinel: total number of states. */
} StateIrqCollector;

/**
 * @brief Events that drive the IRQ collector state machine.
 */
typedef enum {
    E_STOP    = 0, /**< Shutdown requested by irq_collector_ask_stop(). */
    E_TIME_OUT,    /**< Watchdog tick — time to collect a new snapshot. */
    E_NB_EVENT     /**< Sentinel: total number of events. */
} EventIrqCollector;

/**
 * @brief Actions executed during a state machine transition.
 */
typedef enum {
    A_NONE    = 0, /**< No action — do nothing. */
    A_STOP,        /**< Stop the collector. */
    A_COLLECT,     /**< Read /proc/interrupts and log the snapshot. */
    A_NB_ACTION    /**< Sentinel: total number of actions. */
} ActionIrqCollector;

/**
 * @brief Message structure written to and read from the collector mqueue.
 */
typedef struct {
    EventIrqCollector event; /**< Event associated with this message. */
} MqMsgIrqCollector;

/**
 * @brief One state machine transition: destination state + action to execute.
 */
typedef struct {
    StateIrqCollector  destinationState; /**< State to enter after this transition. */
    ActionIrqCollector action;           /**< Action to execute during this transition. */
} TransitionIrqCollector;

/** @brief Current FSM state, updated inside the worker thread. */
static StateIrqCollector currentState;

/**
 * @brief Transition table indexed by [current state][event].
 */
static const TransitionIrqCollector stateMachine[S_NB_STATE][E_NB_EVENT] = {
    [S_RUNNING][E_TIME_OUT] = { S_RUNNING, A_COLLECT },
    [S_RUNNING][E_STOP]     = { S_DEATH,   A_STOP    },
};

/** @brief Worker thread handle. */
static pthread_t irqThread;

/** @brief Mutex protecting irqData against concurrent access. */
static pthread_mutex_t myMutex = PTHREAD_MUTEX_INITIALIZER;

/** @brief POSIX message queue handle. */
static mqd_t irqMq;

/** @brief Periodic watchdog timer used to trigger acquisitions. */
static Watchdog *wtdTimer;

/** @brief Runtime configuration loaded from monitor.yaml. */
static CollectorCfg irqCfg;

/** @brief Array of IrqEntry structs, one per /proc/interrupts data line. */
static IrqEntry *irqData;

/** @brief Number of IRQ data lines (rows in /proc/interrupts minus the header). */
static uint8_t nbLines;

/** @brief Number of online CPUs, set once at init. */
static uint8_t nbCpu;

/**
 * @brief True until the first acquisition is complete.
 *
 * On the first acquisition, id and description fields are populated.
 * On subsequent acquisitions only the counter deltas are updated.
 */
static bool firstAcq;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Function prototype
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Count the number of IRQ data lines in /proc/interrupts.
 *
 * Counts newlines via fgetc, then subtracts 1 for the header line.
 *
 * @return Number of data lines, or 0 on error.
 */
static uint8_t count_irq_lines(void);

/**
 * @brief Allocate the irqData array and all per-CPU counter sub-arrays.
 *
 * Uses calloc for zero-initialisation of all counters.
 */
static void alloc_irq_data(void);

/**
 * @brief Free the irqData array and all per-CPU counter sub-arrays.
 */
static void free_irq_data(void);

/**
 * @brief Read /proc/interrupts and update irqData.
 *
 * On firstAcq, stores id and description.
 * On every call, updates rawPerCpu and computes deltaPerCpu.
 *
 * @return 0 on success, -1 on I/O error.
 */
static int8_t update_irq_data(void);

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
static int8_t send_msg_mq(MqMsgIrqCollector *msg);

/**
 * @brief Blocking read of one message from the mqueue.
 *
 * @param dest  Buffer to receive the message. Must not be NULL.
 * @return      0 on success, -1 on error.
 */
static int8_t read_msg_mq(MqMsgIrqCollector *dest);

/**
 * @brief Dispatch the given FSM action.
 *
 * @param action  Action to execute.
 * @return        0 on success, -1 on error.
 */
static int8_t perform_action(ActionIrqCollector action);

/**
 * @brief Worker thread entry point — drives the FSM until S_DEATH.
 *
 * @param _  Unused thread argument.
 * @return   NULL.
 */
static void *run_irq_collector(void *_);

/**
 * @brief A_COLLECT action: disarm watchdog, read /proc/interrupts, log, re-arm.
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

extern int8_t irq_collector_new(CollectorCfg cfg) {
    int8_t returnErr = 0;
    irqCfg = cfg;

    if (irqCfg.enabled == false) return returnErr;

    TRACE("[IrqCollector] new\n");

    nbLines  = count_irq_lines();
    nbCpu    = (uint8_t)sysconf(_SC_NPROCESSORS_ONLN);
    firstAcq = true;
    alloc_irq_data();

    wtdTimer  = watchdog_create(irqCfg.intervalSec * 1000000U, time_out);
    returnErr = set_up_mq();
    CHECK_ERR(returnErr < 0, "[IrqCollector] Error during set_up_mq");

    if (returnErr >= 0) {
        returnErr = pthread_mutex_init(&myMutex, NULL);
        CHECK_ERR(returnErr < 0, "[IrqCollector] Error when initialising the mutex");
    }

    CHECK_ERR(returnErr < 0, "[IrqCollector] Error during initialisation");
    return returnErr;
}

extern int8_t irq_collector_free(void) {
    int8_t returnErr = 0;

    if (irqCfg.enabled == false) return returnErr;

    TRACE("[IrqCollector] free\n");

    watchdog_destroy(wtdTimer);
    free_irq_data();
    returnErr = tear_down_mq();

    if (returnErr >= 0) {
        returnErr = pthread_mutex_destroy(&myMutex);
        CHECK_ERR(returnErr < 0, "[IrqCollector] Error when destroying the mutex");
    }

    CHECK_ERR(returnErr < 0, "[IrqCollector] Error during destruction");
    return returnErr;
}

extern int8_t irq_collector_ask_start(void) {
    int8_t returnErr = 0;

    if (irqCfg.enabled == false) return returnErr;

    TRACE("[IrqCollector] ask start\n");

    returnErr = pthread_create(&irqThread, NULL, &run_irq_collector, NULL);
    CHECK_ERR(returnErr < 0, "[IrqCollector] Error when creating the worker thread");

    currentState = S_RUNNING;
    watchdog_arm(wtdTimer);
    return returnErr;
}

extern int8_t irq_collector_ask_stop(void) {
    int8_t returnErr = 0;

    if (irqCfg.enabled == false) return returnErr;

    TRACE("[IrqCollector] ask stop\n");

    MqMsgIrqCollector msg = { .event = E_STOP };
    returnErr = send_msg_mq(&msg);
    CHECK_ERR(returnErr < 0, "[IrqCollector] Error when sending E_STOP");

    pthread_join(irqThread, NULL);
    return returnErr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Static functions
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint8_t count_irq_lines(void) {
    FILE *fileToRead = fopen(IRQ_PROC_FILE, "r");
    if (fileToRead == NULL) return 0;
    uint8_t lines = 0;
    int ch;
    while ((ch = fgetc(fileToRead)) != EOF) {
        if (ch == '\n') lines++;
    }
    fclose(fileToRead);
    return (lines > 0) ? lines - 1 : 0;
}

static void alloc_irq_data(void) {
    irqData = calloc(nbLines, sizeof(IrqEntry));
    for (uint8_t i = 0; i < nbLines; i++) {
        irqData[i].deltaPerCpu = calloc(nbCpu, sizeof(uint32_t));
        irqData[i].rawPerCpu   = calloc(nbCpu, sizeof(uint32_t));
    }
}

static void free_irq_data(void) {
    if (irqData == NULL) return;
    for (uint8_t i = 0; i < nbLines; i++) {
        free(irqData[i].deltaPerCpu);
        free(irqData[i].rawPerCpu);
    }
    free(irqData);
    irqData = NULL;
}

static int8_t update_irq_data(void) {
    int8_t returnErr = 0;

    FILE *fileToRead = fopen(IRQ_PROC_FILE, "r");
    if (fileToRead == NULL) {
        CHECK_ERR(true, "[IrqCollector] Error when opening /proc/interrupts");
        return -1;
    }

    char line[512];
    CHECK_ERR(fgets(line, sizeof(line), fileToRead) == NULL,
              "[IrqCollector] Error reading header line");

    uint8_t row = 0;
    while (fgets(line, sizeof(line), fileToRead) != NULL && row < nbLines) {
        char *ptr = line;

        while (*ptr == ' ') ptr++;
        char *idStart = ptr;
        while (*ptr && *ptr != ':' && *ptr != ' ') ptr++;
        size_t idLen = (size_t)(ptr - idStart);
        if (idLen >= IRQ_ID_LEN) idLen = IRQ_ID_LEN - 1;

        if (firstAcq == true) {
            strncpy(irqData[row].id, idStart, idLen);
            irqData[row].id[idLen] = '\0';
        }

        while (*ptr && *ptr != ':') ptr++;
        if (*ptr == ':') ptr++;

        for (uint8_t cpu = 0; cpu < nbCpu; cpu++) {
            while (*ptr == ' ') ptr++;
            uint32_t raw = 0;
            while (isdigit((unsigned char)*ptr)) {
                raw = raw * 10 + (uint32_t)(*ptr - '0');
                ptr++;
            }
            irqData[row].deltaPerCpu[cpu] = raw - irqData[row].rawPerCpu[cpu];
            irqData[row].rawPerCpu[cpu]   = raw;
        }

        if (firstAcq == true) {
            while (*ptr == ' ') ptr++;
            size_t dlen = strlen(ptr);
            if (dlen > 0 && ptr[dlen - 1] == '\n') ptr[dlen - 1] = '\0';
            strncpy(irqData[row].description, ptr, IRQ_DESC_LEN - 1);
        }

        row++;
    }

    fclose(fileToRead);
    return returnErr;
}

static int8_t set_up_mq(void) {
    int8_t returnErr = 0;
    mq_unlink(MQ_LABEL);
    struct mq_attr attr;
    attr.mq_flags   = 0;
    attr.mq_maxmsg  = MQ_MAX_MESSAGES;
    attr.mq_msgsize = sizeof(MqMsgIrqCollector);
    attr.mq_curmsgs = 0;
    irqMq = mq_open(MQ_LABEL, MQ_FLAGS, MQ_MODE, &attr);
    if (irqMq < 0) { CHECK_ERR(true, "[IrqCollector] Fail to open the mqueue"); returnErr = -1; }
    return returnErr;
}

static int8_t tear_down_mq(void) {
    int8_t returnErr = mq_unlink(MQ_LABEL);
    if (returnErr >= 0) { returnErr = mq_close(irqMq); CHECK_ERR(returnErr < 0, "[IrqCollector] Error closing mqueue"); }
    else { CHECK_ERR(true, "[IrqCollector] Error unlinking mqueue"); }
    return returnErr;
}

static int8_t send_msg_mq(MqMsgIrqCollector *msg) {
    int8_t returnErr = mq_send(irqMq, (char *)msg, sizeof(MqMsgIrqCollector), 0);
    CHECK_ERR(returnErr < 0, "[IrqCollector] Error when sending a message");
    return returnErr;
}

static int8_t read_msg_mq(MqMsgIrqCollector *dest) {
    int8_t returnErr = mq_receive(irqMq, (char *)dest, sizeof(MqMsgIrqCollector), NULL);
    CHECK_ERR(returnErr < 0, "[IrqCollector] Error when receiving a message");
    return returnErr;
}

static void time_out(void) {
    TRACE("[IrqCollector] time out\n");
    MqMsgIrqCollector msg = { .event = E_TIME_OUT };
    send_msg_mq(&msg);
}

static void *run_irq_collector(void *_) {
    (void)_;
    while (currentState != S_DEATH) {
        MqMsgIrqCollector msg;
        read_msg_mq(&msg);
        TransitionIrqCollector transition = stateMachine[currentState][msg.event];
        if (transition.destinationState != S_NONE) {
            perform_action(transition.action);
            currentState = transition.destinationState;
        } else {
            TRACE("[IrqCollector] FSM lost an event\n");
        }
    }
    return NULL;
}

static int8_t perform_action(ActionIrqCollector action) {
    int8_t returnErr = 0;
    switch (action) {
        case A_NONE:    break;
        case A_COLLECT: returnErr = perform_collect(); break;
        case A_STOP:    returnErr = perform_stop();    break;
        default:        break;
    }
    CHECK_ERR(returnErr < 0, "[IrqCollector] Error when performing action");
    return returnErr;
}

static int8_t perform_collect(void) {
    watchdog_disarm(wtdTimer);
    int8_t returnErr = 0;

    pthread_mutex_lock(&myMutex);
    CHECK_ERR(update_irq_data() < 0, "[IrqCollector] Error updating irq data");
    pthread_mutex_unlock(&myMutex);

    sink_log_irq(irqData, nbLines, nbCpu, firstAcq);
    firstAcq = false;
    TRACE("[IrqCollector] snapshot logged\n");

    watchdog_arm(wtdTimer);
    return returnErr;
}

static int8_t perform_stop(void) {
    int8_t returnErr = 0;
    watchdog_disarm(wtdTimer);

    MqMsgIrqCollector msg = { .event = E_STOP };
    returnErr = send_msg_mq(&msg);

    if (returnErr < 0) {
        CHECK_ERR(true, "[IrqCollector] Error sending E_STOP in perform_stop");
    } else {
        returnErr = pthread_join(irqThread, NULL);
        CHECK_ERR(returnErr < 0, "[IrqCollector] Error when joining the worker thread");
    }

    CHECK_ERR(returnErr < 0, "[IrqCollector] Error in perform_stop");
    return returnErr;
}
