/**
 * @file debug.h
 *
 * @brief Debug and logging macros: LOG, CHECK_ERR, TRACE.
 *
 * All macros accept printf-style format strings.
 * TRACE is compiled out when NDEBUG is defined.
 */

#ifndef CORE_DEBUG_H
#define CORE_DEBUG_H

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Macros
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG

/**
 * @brief Print a formatted message to stderr.
 *
 * @param fmt  printf-style format string.
 * @param ...  Optional format arguments.
 */
#define LOG(fmt, ...)                                                          \
  do {                                                                         \
    fprintf(stderr, fmt, ##__VA_ARGS__);                                       \
    fflush(stderr);                                                            \
  } while (0)

/**
 * @brief Report an error when @p cond is true.
 *
 * If errno is non-zero, calls perror() and clears errno.
 * Otherwise prints @p msg directly to stderr.
 *
 * @param cond  Boolean condition — report fires when true.
 * @param msg   Human-readable error label (no trailing newline required).
 */
#define CHECK_ERR(cond, msg)                                                   \
  do {                                                                         \
    if (cond) {                                                                \
      if (errno != 0) {                                                        \
        perror(msg);                                                           \
        errno = 0;                                                             \
      } else {                                                                 \
        fprintf(stderr, "%s\n", msg);                                          \
      }                                                                        \
    }                                                                          \
  } while (0)

/**
 * @brief Verbose trace — prints file, line, function name and message.
 *
 * Compiled to a no-op when NDEBUG is defined.
 *
 * @param fmt  printf-style format string.
 * @param ...  Optional format arguments.
 */
#define TRACE(fmt, ...)                                                        \
  do {                                                                         \
    fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__,          \
            ##__VA_ARGS__);                                                    \
  } while (0)

#else
#define LOG(fmt, ...)                                                          \
  do {                                                                         \
  } while (0)
#define CHECK_ERR(c, msg)                                                      \
  do {                                                                         \
  } while (0)
#define TRACE(fmt, ...)                                                        \
  do {                                                                         \
  } while (0)
#endif

#endif /* CORE_DEBUG_H */
