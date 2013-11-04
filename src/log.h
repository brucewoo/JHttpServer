/*
 * log.h
 *
 *  Created on: 2013-11-4
 *      Author: brucewoo
 */

#ifndef LOG_H_
#define LOG_H_

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <inttypes.h>

#define PRI_FSBLK       PRIu64
#define PRI_DEV         PRIu64
#define PRI_NLINK       PRIu32
#define PRI_SUSECONDS   "06ld"

#define PRI_BLKSIZE     PRId32
#define PRI_SIZET       "zu"


#if 0
/* Syslog definitions :-) */
#define LOG_EMERG   0           /* system is unusable */
#define LOG_ALERT   1           /* action must be taken immediately */
#define LOG_CRIT    2           /* critical conditions */
#define LOG_ERR     3           /* error conditions */
#define LOG_WARNING 4           /* warning conditions */
#define LOG_NOTICE  5           /* normal but significant condition */
#define LOG_INFO    6           /* informational */
#define LOG_DEBUG   7           /* debug-level messages */
#endif

typedef enum {
        LOG_NONE = 0,
        LOG_EMERG,
        LOG_ALERT,
        LOG_CRITICAL,   /* fatal errors */
        LOG_ERROR,      /* major failures (not necessarily fatal) */
        LOG_WARNING,    /* info about normal operation */
        LOG_NOTICE,
        LOG_INFO,       /* Normal information */
        LOG_DEBUG,      /* internal errors */
        LOG_TRACE,      /* full trace of operation */
} loglevel_t;

#define DEFAULT_LOG_FILE_DIRECTORY            DATADIR "/log/jhttpserver"
#define DEFAULT_LOG_LEVEL                     LOG_INFO

typedef struct log_handle_ {
        pthread_mutex_t  logfile_mutex;
        uint8_t          logrotate;
        loglevel_t    	 loglevel;
        int              log_syslog;
        loglevel_t    	 sys_log_level;
        char             log_xl_log_set;
        char            *filename;
        FILE            *logfile;
        FILE            *log_logfile;
        char            *cmd_log_filename;
        FILE            *cmdlogfile;
#ifdef USE_SYSLOG
        int              log_control_file_found;
        char            *ident;
#endif /* USE_SYSLOG */

} log_handle_t;

typedef enum {
        timefmt_default = 0,
        timefmt_FT = 0,  /* YYYY-MM-DD hh:mm:ss */
        timefmt_Ymd_T,   /* YYYY/MM-DD-hh:mm:ss */
        timefmt_bdT,     /* ddd DD hh:mm:ss */
        timefmt_F_HMS,   /* YYYY-MM-DD hhmmss */
        timefmt_last
} timefmts;

static const char *__timefmts[] = {
        "%F %T",
        "%Y/%m/%d-%T",
        "%b %d %T",
        "%F %H%M%S"
};

static const char *__zerotimes[] = {
        "0000-00-00 00:00:00",
        "0000/00/00-00:00:00",
        "xxx 00 00:00:00",
        "0000-00-00 000000"
};

void log_globals_init(log_handle_t* log);

int log_init(log_handle_t* log, const char *filename, const char *ident);

void log_logrotate(log_handle_t* log, int signum);

//void log_cleanup(log_handle_t log);

int _log(log_handle_t* log, const char *domain, const char *file,
             const char *function, int32_t line, loglevel_t level,
             const char *fmt, ...);

int _log_callingfn(log_handle_t* log, const char *domain, const char *file,
                       const char *function, int32_t line, loglevel_t level,
                       const char *fmt, ...);

int _log_nomem (log_handle_t* log, const char *domain, const char *file,
                   const char *function, int line, loglevel_t level,
                   size_t size);

int _log_eh(log_handle_t* log, const char *function, const char *fmt, ...);



#define FMT_WARN(fmt...) do { if (0) printf (fmt); } while (0)

#define log(logger, dom, levl, fmt...) do {                                  \
                FMT_WARN (fmt);                                         \
                _log (logger, dom, __FILE__, __FUNCTION__, __LINE__,         \
                         levl, ##fmt);                                  \
        } while (0)

#define log_eh(logger, fmt...) do {                                          \
                FMT_WARN (fmt);                                         \
                _log_eh (logger, __FUNCTION__, ##fmt);                        \
        } while (0)

#define log_callingfn(logger, dom, levl, fmt...) do {                        \
                FMT_WARN (fmt);                                         \
                _log_callingfn (logger, dom, __FILE__, __FUNCTION__, __LINE__, \
                                   levl, ##fmt);                        \
        } while (0)


/* No malloc or calloc should be called in this function */
#define log_nomem(logger, dom, levl, size) do {                              \
                _log_nomem (logger, dom, __FILE__, __FUNCTION__, __LINE__,   \
                               levl, size);                             \
        } while (0)


/* Log once in UNIVERSAL_ANSWER times */
#define LOG_OCCASIONALLY(log, var, args...) if (!(var++%UNIVERSAL_ANSWER)) { \
                log(log, args);                                          \
        }

void log_disable_syslog (log_handle_t* logger);

void log_enable_syslog (log_handle_t* logger);

loglevel_t log_get_loglevel (log_handle_t* logger);

void log_set_loglevel (log_handle_t* logger, loglevel_t level);

int cmd_log(log_handle_t* logger, const char *domain, const char *fmt, ...);

int cmd_log_init(log_handle_t* logger, const char *filename);

void set_sys_log_level (log_handle_t* logger, loglevel_t level);

int j_asprintf(char **string_ptr, const char *format, ...);

int j_vasprintf(char **string_ptr, const char *format, va_list arg);

#define DEBUG(logger, name, format, args...)                           \
        log(logger, name, LOG_DEBUG, format, ##args)
#define INFO(logger, name, format, args...)                            \
        log(logger, name, LOG_INFO, format, ##args)
#define WARNING(logger, name, format, args...)                         \
        log(logger, name, LOG_WARNING, format, ##args)
#define ERROR(logger, name, format, args...)                           \
        log(logger, name, LOG_ERROR, format, ##args)

#endif /* LOG_H_ */
