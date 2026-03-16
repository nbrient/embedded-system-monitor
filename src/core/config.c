/**
 * @file config.c
 *
 * @brief YAML configuration loader implementation.
 *
 * @version 1.0
 */

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Include
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "config.h"
#include "debug.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Define
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Resolved path to the YAML config file.
 *
 * Set by config_set_path() or config_use_default_path() before config_load() is called.
 */
static char configFilePath[256];

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              CYAML schema
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** @brief String-to-enum mapping for the log_method YAML field. */
static const cyaml_strval_t logTargetStrings[] = {
    { "journalctl",            LOG_TARGET_JOURNALCTL },
    { "LOG_TARGET_JOURNALCTL", LOG_TARGET_JOURNALCTL },
    { "syslog",                LOG_TARGET_SYSLOG     },
    { "LOG_TARGET_SYSLOG",     LOG_TARGET_SYSLOG     },
};

/** @brief CYAML field schema for a CollectorCfg mapping. */
static const cyaml_schema_field_t collectorCfgSchema[] = {
    CYAML_FIELD_UINT("enable_acquisition", CYAML_FLAG_DEFAULT, CollectorCfg, enabled),
    CYAML_FIELD_UINT("timer",              CYAML_FLAG_DEFAULT, CollectorCfg, intervalSec),
    CYAML_FIELD_END
};

/** @brief CYAML field schema for the generic_config mapping. */
static const cyaml_schema_field_t genericCfgSchema[] = {
    CYAML_FIELD_ENUM("log_method", CYAML_FLAG_DEFAULT,
        MonitorCfg, logTarget, logTargetStrings, CYAML_ARRAY_LEN(logTargetStrings)),
    CYAML_FIELD_END
};

/** @brief CYAML field schema for the top-level MonitorCfg mapping. */
static const cyaml_schema_field_t monitorCfgSchema[] = {
    CYAML_FIELD_MAPPING("generic_config", CYAML_FLAG_DEFAULT, MonitorCfg, logTarget, genericCfgSchema),
    CYAML_FIELD_MAPPING("cpu_config",     CYAML_FLAG_DEFAULT, MonitorCfg, cpu,       collectorCfgSchema),
    CYAML_FIELD_END
};

/** @brief CYAML value schema for the root YAML document. */
static const cyaml_schema_value_t rootSchema = {
    CYAML_VALUE_MAPPING(CYAML_FLAG_POINTER, MonitorCfg, monitorCfgSchema),
};

/** @brief libcyaml runtime configuration (logging + memory). */
static const cyaml_config_t cyamlCfg = {
    .log_fn    = cyaml_log,
    .mem_fn    = cyaml_mem,
    .log_level = CYAML_LOG_WARNING,
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Static functions
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Populate @p cfg with safe built-in defaults.
 *
 * Called before YAML parsing so that missing fields keep their default values.
 *
 * @param cfg  Output MonitorCfg struct to initialise.
 */
static void apply_defaults(MonitorCfg *cfg) {
    cfg->logTarget       = LOG_TARGET_JOURNALCTL;
    cfg->cpu.enabled     = true;
    cfg->cpu.intervalSec = 5;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                                              Public functions
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern void config_set_path(const char *path) {
    size_t len = strlen(path);

    if (len > 5 && strcmp(path + len - 5, ".yaml") == 0) {
        snprintf(configFilePath, sizeof(configFilePath), "%s", path);
    } else {
        char dir[256];
        snprintf(dir, sizeof(dir), "%s", path);
        size_t dlen = strlen(dir);
        while (dlen > 0 && dir[dlen - 1] == '/') dir[--dlen] = '\0';
        snprintf(configFilePath, sizeof(configFilePath), "%s/%s", dir, CONFIG_FILENAME);
    }

    TRACE("config path: %s\n", configFilePath);
}

extern void config_use_default_path(void) {
    snprintf(configFilePath, sizeof(configFilePath), "%s%s",
             DEFAULT_CONFIG_DIR, CONFIG_FILENAME);
    TRACE("default config path: %s\n", configFilePath);
}

extern MonitorCfg config_load(void) {
    int8_t returnErr = 0;
    MonitorCfg result;
    MonitorCfg *parsed = NULL;

    apply_defaults(&result);

    returnErr = cyaml_load_file(configFilePath, &cyamlCfg,
                                &rootSchema, (void **)&parsed, NULL);
    if (returnErr != CYAML_OK) {
        fprintf(stderr, "[config] %s — using defaults\n", cyaml_strerror(returnErr));
        return result;
    }

    result = *parsed;
    cyaml_free(&cyamlCfg, &rootSchema, parsed, 0);
    return result;
}
