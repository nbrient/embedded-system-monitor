/**
 * @file config.h
 *
 * @brief YAML configuration loader for embedded-monitor.
 */

#ifndef CORE_CONFIG_H
#define CORE_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <cyaml/cyaml.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Define
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** @brief Default config directory when no CLI path is supplied. */
#define DEFAULT_CONFIG_DIR  "/etc/embedded-monitor/"

/** @brief Config filename expected inside the config directory. */
#define CONFIG_FILENAME     "monitor.yaml"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Structures
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Logging output target.
 */
typedef enum {
    LOG_TARGET_JOURNALCTL = 0, /**< Write to the systemd journal via sd_journal_send(). */
    LOG_TARGET_SYSLOG     = 1, /**< Write via traditional openlog() / syslog(). */
} LogTarget;

/**
 * @brief Per-collector configuration, loaded from monitor.yaml.
 */
typedef struct {
    uint16_t intervalSec; /**< Acquisition interval in seconds. */
    bool     enabled;     /**< Whether to run this collector. */
} CollectorCfg;

/**
 * @brief Top-level monitor configuration.
 *
 * A new CollectorCfg field is added here for each new collector module.
 */
typedef struct {
    LogTarget    logTarget; /**< Active logging backend. */
    CollectorCfg cpu;       /**< CPU collector configuration. */
    CollectorCfg mem;       /**< Memory collector configuration. */
} MonitorCfg;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Public functions
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Override the config file path.
 *
 * @param path  Path to the YAML file or its parent directory.
 */
extern void config_set_path(const char *path);

/** @brief Use the compiled-in default path. */
extern void config_use_default_path(void);

/**
 * @brief Parse the YAML config and return a populated MonitorCfg.
 *
 * Falls back to built-in defaults on any parse error.
 *
 * @return Populated MonitorCfg returned by value.
 */
extern MonitorCfg config_load(void);

#endif /* CORE_CONFIG_H */
