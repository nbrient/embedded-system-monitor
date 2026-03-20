/**
 * @file main.c
 * @brief embedded-monitor entry point.
 * @version 1.0
 */
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                              Include
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "core/config.h"
#include "core/debug.h"
#include "sink/sink.h"
#include "collectors/cpu/cpu_collector.h"
#include "collectors/mem/mem_collector.h"
#include "collectors/irq/irq_collector.h"
#include "collectors/proc/proc_collector.h"
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                              Define
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/** @brief Mutex protecting shutdownFlag against concurrent access. */
static pthread_mutex_t mainMutex = PTHREAD_MUTEX_INITIALIZER;
/** @brief Condition variable signalled when a shutdown is requested. */
static pthread_cond_t  mainCond  = PTHREAD_COND_INITIALIZER;
/** @brief Set to true by int_handler() or when 'q' is read from stdin. */
static volatile bool   shutdownFlag = false;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                              Function prototype
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/** @brief SIGINT handler — sets shutdownFlag and wakes the main thread. @param _ Signal number, not used. */
static void int_handler(int _);
/** @brief Register int_handler() as the SIGINT handler via sigaction. */
static void set_up_signals(void);
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                              Public functions
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int main(int argc, char *argv[]) {
    int8_t returnErr = 0;
    if (argc == 2) config_set_path(argv[1]); else config_use_default_path();
    MonitorCfg cfg = config_load();
    set_up_signals(); sink_init(cfg.logTarget);
    returnErr  = cpu_collector_new(cfg.cpu);
    returnErr |= mem_collector_new(cfg.mem);
    returnErr |= irq_collector_new(cfg.irq);
    returnErr |= proc_collector_new(cfg.proc);
    CHECK_ERR(returnErr < 0, "[Main] Error during collector init");
    if (returnErr < 0) return EXIT_FAILURE;
    cpu_collector_ask_start(); mem_collector_ask_start();
    irq_collector_ask_start(); proc_collector_ask_start();
    LOG("embedded-monitor running — Ctrl-C or 'q' to quit\n");
    pthread_mutex_lock(&mainMutex);
    while (shutdownFlag == false) {
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 1;
        pthread_cond_timedwait(&mainCond, &mainMutex, &ts);
        fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = { 0, 0 };
        if (select(1, &fds, NULL, NULL, &tv) > 0) {
            char ch = 0; if (scanf(" %c", &ch) == 1 && ch == 'q') shutdownFlag = true;
        }
    }
    pthread_mutex_unlock(&mainMutex);
    cpu_collector_ask_stop();  mem_collector_ask_stop();
    irq_collector_ask_stop();  proc_collector_ask_stop();
    cpu_collector_free();      mem_collector_free();
    irq_collector_free();      proc_collector_free();
    pthread_mutex_destroy(&mainMutex); pthread_cond_destroy(&mainCond);
    LOG("embedded-monitor stopped\n"); return EXIT_SUCCESS;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                              Static functions
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void int_handler(int _) {
    (void)_; pthread_mutex_lock(&mainMutex); shutdownFlag = true;
    pthread_cond_signal(&mainCond); pthread_mutex_unlock(&mainMutex);
}
static void set_up_signals(void) {
    struct sigaction sa = { .sa_handler = int_handler }; sigemptyset(&sa.sa_mask);
    int8_t returnErr = sigaction(SIGINT, &sa, NULL);
    CHECK_ERR(returnErr < 0, "[Main] Error when setting up SIGINT handler");
    if (returnErr < 0) exit(EXIT_FAILURE);
}
