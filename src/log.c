/*
 * log.c
 *
 *  Created on: 2013-11-4
 *      Author: brucewoo
 */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <locale.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#ifdef USE_SYSLOG
#include <libintl.h>
#include <syslog.h>
#include <sys/stat.h>

#define JSON_MSG_LENGTH      8192
#define SYSLOG_CEE_FORMAT    \
        "@cee: {\"msg\": \"%s\", \"code\": \"%u\", \"message\": \"%s\"}"
#define LOG_CONTROL_FILE     "/etc/jhttpserver/log.conf"
#endif /* USE_SYSLOG */

#include "log.h"

#ifdef LINUX_HOST_OS
#include <syslog.h>
#endif

#ifdef HAVE_BACKTRACE
#include <execinfo.h>
#endif

void _timestuff (timefmts *fmt, const char ***fmts, const char ***zeros)
{
        *fmt = timefmt_last;
        *fmts = __timefmts;
        *zeros = __zerotimes;
}

static inline void time_fmt (char *dst, size_t sz_dst, time_t utime, unsigned int fmt)
{
        static timefmts timefmt_last = (timefmts) -1;
        static const char **fmts;
        static const char **zeros;
        struct tm tm;

        if (timefmt_last == -1)
        {
        	_timestuff (&timefmt_last, &fmts, &zeros);
        }

        if (timefmt_last < fmt)
        {
        	fmt = timefmt_default;
        }
        if (utime && gmtime_r (&utime, &tm) != NULL)
        {
                strftime (dst, sz_dst, fmts[fmt], &tm);
        }
        else
        {
                strncpy (dst, "N/A", sz_dst);
        }
}

void log_logrotate(log_handle_t* log, int signum)
{
	log->logrotate = 1;
}

void log_enable_syslog(log_handle_t* log)
{
	log->log_syslog = 1;
}

void log_disable_syslog(log_handle_t* log)
{
	log->log_syslog = 0;
}

loglevel_t log_get_loglevel(log_handle_t* log)
{
	return log->loglevel;
}

void log_set_loglevel(log_handle_t* log, loglevel_t level) {
	log->loglevel = level;
}

void log_fini(log_handle_t* log)
{
	pthread_mutex_destroy(&log->logfile_mutex);
}

#ifdef USE_SYSLOG
/**
 * get_error_message -function to get error message for given error code
 * @error_code: error code defined by log book
 *
 * @return: success: string
 *          failure: NULL
 */
const char * get_error_message (int error_code) {
	return _get_message (error_code);
}

/**
 * openlog -function to open syslog specific to gluster based on
 *             existence of file /etc/glusterfs/logger.conf
 * @ident:    optional identification string similar to openlog()
 * @option:   optional value to option to openlog().  Passing -1 uses
 *            'LOG_PID | LOG_NDELAY' as default
 * @facility: optional facility code similar to openlog().  Passing -1
 *            uses LOG_DAEMON as default
 *
 * @return: void
 */
void openlog(const char *ident, int option, int facility)
{
	int _option = option;
	int _facility = facility;

	if (-1 == _option)
	{
		_option = LOG_PID | LOG_NDELAY;
	}
	if (-1 == _facility)
	{
		_facility = LOG_LOCAL1;
	}

	setlocale(LC_ALL, "");
	bindtextdomain("jhttpserver", "/usr/share/locale");
	textdomain("jhttpserver");

	openlog(ident, _option, _facility);
}

/**
 * _json_escape -function to convert string to json encoded string
 * @str: input string
 * @buf: buffer to store encoded string
 * @len: length of @buf
 *
 * @return: success: l
			level_strings[level], basename, line, function, domain);
	if (-1 == ret)
	{
		goto err;
	}

	ret = vasprintf(&str2, fmt, ap);
	if (-1 == ret)
	{
		goto err;
	}

	va_end(ap);

	len = strlen(str1);
	msg = (char*)malloc(sizeof(char) * (len + strlen(str2) + 1));

	strcpy(msg, str1);
	strcpy(msg + len, str2);
 * ast unprocessed character position by pointer in @str
 *          failure: NULL
 *
 * Internal function. Heavily inspired by _ul_str_escape() function in
 * libumberlog
 *
 * Sample output:
 * [1] str = "devel error"
 *     buf = "devel error"
 * [2] str = "devel        error"
 *     buf = "devel\terror"
 * [3] str = "I/O error on "/tmp/foo" file"
 *     buf = "I/O error on \"/tmp/foo\" file"
 * [4] str = "I/O erroron /tmp/bar file"
 *     buf = "I/O error\u001bon /tmp/bar file"
 *
 */
char* _json_escape(const char *str, char *buf, size_t len)
{
	static const unsigned char json_exceptions[UCHAR_MAX + 1] =
	{
		[0x01] = 1, [0x02] = 1, [0x03] = 1, [0x04] = 1,
		[0x05] = 1, [0x06] = 1, [0x07] = 1, [0x08] = 1,
		[0x09] = 1, [0x0a] = 1, [0x0b] = 1, [0x0c] = 1,
		[0x0d] = 1, [0x0e] = 1, [0x0f] = 1, [0x10] = 1,
		[0x11] = 1, [0x12] = 1, [0x13] = 1, [0x14] = 1,
		[0x15] = 1, [0x16] = 1, [0x17] = 1, [0x18] = 1,
		[0x19] = 1, [0x1a] = 1, [0x1b] = 1, [0x1c] = 1,
		[0x1d] = 1, [0x1e] = 1, [0x1f] = 1,
		['\\'] = 1, ['"'] = 1
	};
	static const char json_hex_chars[16] = "0123456789abcdef";
	unsigned char *p = NULL;
	size_t pos = 0;

	if (!str || !buf || len <= 0)
	{
		return NULL;
	}

	for (p=(unsigned char *)str; *p&&(pos+1)<len; p++)
	{
		if (json_exceptions[*p] == 0)
		{
			buf[pos++] = *p;
			continue;
		}

		if ((pos + 2) >= len)
		{
			break;
		}

		switch (*p)
		{
			case '\b':
			buf[pos++] = '\\';
			buf[pos++] = 'b';
			break;
			case '\n':
			buf[pos++] = '\\';
			buf[pos++] = 'n';
			break;
			case '\r':
			buf[pos++] = '\\';
			buf[pos++] = 'r';
			break;
			case '\t':
			buf[pos++] = '\\';
			buf[pos++] = 't';
			break;
			case '\\':
			buf[pos++] = '\\';
			buf[pos++] = '\\';
			break;
			case '"':
			buf[pos++] = '\\';
			buf[pos++] = '"';
			break;
			default:
			if ((pos + 6) >= len)
			{
				buf[pos] = '\0';
				return (char *)p;
			}
			buf[pos++] = '\\';
			buf[pos++] = 'u';
			buf[pos++] = '0';
			buf[pos++] = '0';
			buf[pos++] = json_hex_chars[(*p) >> 4];
			buf[pos++] = json_hex_chars[(*p) & 0xf];
			break;
		}
	}

	buf[pos] = '\0';
	return (char *)p;
}

/**
 * syslog -function to submit message to syslog specific to jhttpserver
 * @error_code:        error code defined by log book
 * @facility_priority: facility_priority of syslog()
 * @format:            optional format string to syslog()
 *
 * @return: void
 */
void syslog(int error_code, int facility_priority, char *format, ...)
{
	char *msg = NULL;
	char json_msg[JSON_MSG_LENGTH];
	char *p = NULL;
	const char *error_message = NULL;
	char json_error_message[JSON_MSG_LENGTH];
	va_list ap;

	error_message = get_error_message (error_code);

	va_start (ap, format);
	if (format)
	{
		vasprintf (&msg, format, ap);
		p = _json_escape (msg, json_msg, JSON_MSG_LENGTH);
		if (error_message)
		{
			p = _json_escape (error_message, json_error_message,
					JSON_MSG_LENGTH);
			syslog (facility_priority, SYSLOG_CEE_FORMAT,
					json_msg, error_code, json_error_message);
		}
		else
		{
			/* ignore the error code because no error message for it
			 and use normal syslog */
			syslog (facility_priority, "%s", msg);
		}
		free (msg);
	} else {
		if (error_message)
		{
			/* no user message: treat error_message as msg */
			syslog (facility_priority, SYSLOG_CEE_FORMAT,
					json_error_message, error_code,
					json_error_message);
		}
		else
		{
			/* cannot produce log as neither error_message nor
			 msg available */
		}
	}
	va_end (ap);
}
#endif /* USE_SYSLOG */

void log_globals_init(log_handle_t* log) {
	pthread_mutex_init(&log->logfile_mutex, NULL);

	log->loglevel = LOG_INFO;
	log->log_syslog = 1;
	log->sys_log_level = LOG_CRITICAL;

#ifndef USE_SYSLOG
#ifdef LINUX_HOST_OS
	/* For the 'syslog' output. one can grep 'GlusterFS' in syslog
	 for serious logs */
	openlog ("JHttpServer", LOG_PID, LOG_DAEMON);
#endif
#endif
}

int log_init(log_handle_t* log, const char *file, const char *ident)
{
	int fd = -1;

#if defined(USE_SYSLOG)
	{
		/* use default ident and option */
		/* TODO: make FACILITY configurable than LOG_DAEMON */
		struct stat buf;

		if (stat (LOG_CONTROL_FILE, &buf) == 0)
		{
			/* use syslog logging */
			log->log_control_file_found = 1;
			if (ident)
			{
				/* we need to keep this value as */
				/* syslog uses it on every logging */
				log->ident = strdup (ident);
				openlog (log->ident, -1, LOG_DAEMON);
			}
			else
			{
				openlog (NULL, -1, LOG_DAEMON);
			}
		}
		else
		{
			/* use old style logging */
			log->log_control_file_found = 0;
		}
	}
#endif

	if (!file) {
		fprintf(stderr, "ERROR: no filename specified\n");
		return -1;
	}

	if (strcmp(file, "-") == 0)
	{
		log->log_logfile = stderr;
		log->logfile = stderr;
		return 0;
	}

	log->filename = strdup(file);
	if (!log->filename)
	{
		fprintf(stderr, "ERROR: updating log-filename failed: %s\n",
				strerror(errno));
		return -1;
	}

	fd = open(file, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
	if (fd < 0)
	{
		fprintf(stderr, "ERROR: failed to create logfile \"%s\" (%s)\n", file,
				strerror(errno));
		return -1;
	}
	close(fd);

	log->logfile = fopen(file, "a");
	if (!log->logfile)
	{
		fprintf(stderr, "ERROR: failed to open logfile \"%s\" (%s)\n", file,
				strerror(errno));
		return -1;
	}

	log->log_logfile = log->logfile;
	return 0;
}

void set_sys_log_level(log_handle_t* log, loglevel_t level) {
	log->sys_log_level = level;
}

int _log_nomem(log_handle_t* log, const char *domain, const char *file,
		const char *function, int line, loglevel_t level, size_t size) {
	const char *basename = NULL;
	struct timeval tv = { 0, };
	int ret = 0;
	char msg[8092] = { 0, };
	char timestr[256] = { 0, };
	char callstr[4096] = { 0, };

	if (level > log->loglevel)
	{
		goto out;
	}

	static char *level_strings[] = { "", /* NONE */
									"M", /* EMERGENCY */
									"A", /* ALERT */
									"C", /* CRITICAL */
									"E", /* ERROR */
									"W", /* WARNING */
									"N", /* NOTICE */
									"I", /* INFO */
									"D", /* DEBUG */
									"T", /* TRACE */
									"" };

	if (!domain || !file || !function)
	{
		fprintf(stderr, "logging: %s:%s():%d: invalid argument\n", __FILE__,
				__PRETTY_FUNCTION__, __LINE__);
		return -1;
	}

	basename = strrchr(file, '/');
	if (basename)
	{
		basename++;
	}
	else
	{
		basename = file;
	}

#if HAVE_BACKTRACE
	/* Print 'calling function' */
	do {
		void *array[5];
		char **callingfn = NULL;
		size_t bt_size = 0;

		bt_size = backtrace (array, 5);
		if (bt_size)
		callingfn = backtrace_symbols (&array[2], bt_size-2);
		if (!callingfn)
		{
			break;
		}

		if (bt_size == 5)
		{
			snprintf (callstr, 4096, "(-->%s (-->%s (-->%s)))",
					callingfn[2], callingfn[1], callingfn[0]);
		}

		if (bt_size == 4)
		{
			snprintf (callstr, 4096, "(-->%s (-->%s))",
					callingfn[1], callingfn[0]);
		}

		if (bt_size == 3)
		{
			snprintf (callstr, 4096, "(-->%s)", callingfn[0]);
		}

		free (callingfn);
	}while (0);
#endif /* HAVE_BACKTRACE */

#if defined(USE_SYSLOG)
	if (log->log_control_file_found)
	{
		int priority;
		/* treat LOG_TRACE and LOG_NONE as LOG_DEBUG and
		 other level as is */
		if (LOG_TRACE == level || LOG_NONE == level)
		{
			priority = LOG_DEBUG;
		}
		else
		{
			priority = level - 1;
		}
		syslog (ERR_DEV, priority,
				"[%s:%d:%s] %s %s: no memory "
				"available for size (%"PRI_SIZET")",
				basename, line, function, callstr, domain,
				size);
		goto out;
	}
#endif /* USE_SYSLOG */
	ret = gettimeofday(&tv, NULL);
	if (-1 == ret)
	{
		goto out;
	}
	time_fmt(timestr, sizeof timestr, tv.tv_sec, timefmt_FT);
	snprintf(timestr + strlen(timestr), sizeof timestr - strlen(timestr),
			".%"PRI_SUSECONDS, tv.tv_usec);

	ret = sprintf(msg, "[%s] %s [%s:%d:%s] %s %s: no memory "
			"available for size (%"PRI_SIZET")", timestr, level_strings[level],
			basename, line, function, callstr, domain, size);
	if (-1 == ret)
	{
		goto out;
	}

	pthread_mutex_lock(&log->logfile_mutex);
	{
		if (log->logfile)
		{
			fprintf(log->logfile, "%s\n", msg);
		}
		else
		{
			fprintf(stderr, "%s\n", msg);
		}

#ifdef LINUX_HOST_OS
		/* We want only serious log in 'syslog', not our debug
		 and trace logs */
		if (log->log_syslog && level &&
				(level <= log->sys_log_level))
		{
			syslog ((level-1), "%s\n", msg);
		}
#endif
	}

	pthread_mutex_unlock(&log->logfile_mutex);
out:
	return ret;
}

int _log_callingfn(log_handle_t* log, const char *domain, const char *file,
		const char *function, int line, loglevel_t level, const char *fmt, ...) {
	const char *basename = NULL;
	char *str1 = NULL;
	char *str2 = NULL;
	char *msg = NULL;
	char timestr[256] = { 0, };
	char callstr[4096] = { 0, };
	struct timeval tv = { 0, };
	size_t len = 0;
	int ret = 0;
	va_list ap;

	if (level > log->loglevel)
	{
		goto out;
	}

	static char *level_strings[] = { "", /* NONE */
									"M", /* EMERGENCY */
									"A", /* ALERT */
									"C", /* CRITICAL */
									"E", /* ERROR */
									"W", /* WARNING */
									"N", /* NOTICE */
									"I", /* INFO */
									"D", /* DEBUG */
									"T", /* TRACE */
									"" };

	if (!domain || !file || !function || !fmt)
	{
		fprintf(stderr, "logging: %s:%s():%d: invalid argument\n", __FILE__,
				__PRETTY_FUNCTION__, __LINE__);
		return -1;
	}

	basename = strrchr(file, '/');
	if (basename)
	{
		basename++;
	}
	else
	{
		basename = file;
	}

#if HAVE_BACKTRACE
	/* Print 'calling function' */
	do {
		void *array[5];
		char **callingfn = NULL;
		size_t size = 0;

		size = backtrace (array, 5);
		if (size)
		callingfn = backtrace_symbols (&array[2], size-2);
		if (!callingfn)
		{
			break;
		}

		if (size == 5)
		{
			snprintf (callstr, 4096, "(-->%s (-->%s (-->%s)))",
				callingfn[2], callingfn[1], callingfn[0]);
		}
		if (size == 4)
		{
			snprintf (callstr, 4096, "(-->%s (-->%s))",
				callingfn[1], callingfn[0]);
		}
		if (size == 3)
		{
			snprintf (callstr, 4096, "(-->%s)", callingfn[0]);
		}

		free (callingfn);
	}while (0);
#endif /* HAVE_BACKTRACEstr2 */

#if defined(USE_SYSLOG)
	if (log->log_control_file_found)
	{
		int priority;
		/* treat LOG_TRACE and LOG_NONE as LOG_DEBUG and
		 other level as is */
		if (LOG_TRACE == level || LOG_NONE == level)
		{
			priority = LOG_DEBUG;
		}
		else
		{
			priority = level - 1;
		}

		va_start (ap, fmt);
		vasprintf (&str2, fmt, ap);
		va_end (ap);

		syslog (ERR_DEV, priority,
				"[%s:%d:%s] %s %d-%s: %s",
				basename, line, function,
				callstr,
				((this->graph) ? this->graph->id:0), domain,
				str2);
		goto out;
	}
#endif /* USE_SYSLOG */
	ret = gettimeofday(&tv, NULL);
	if (-1 == ret)
	{
		goto out;
	}
	va_start(ap, fmt);
	time_fmt(timestr, sizeof timestr, tv.tv_sec, timefmt_FT);
	snprintf(timestr + strlen(timestr), sizeof timestr - strlen(timestr),
			".%"PRI_SUSECONDS, tv.tv_usec);

	ret = j_asprintf(&str1, "[%s] %s [%s:%d:%s] %s %s: ", timestr,
			level_strings[level], basename, line, function, callstr, domain);
	if (-1 == ret)
	{
		goto out;
	}

	ret = vasprintf(&str2, fmt, ap);
	if (-1 == ret)
	{
		goto out;
	}

	va_end(ap);

	len = strlen(str1);
	msg = (char*)malloc(sizeof(char) * (len + strlen(str2) + 1));

	strcpy(msg, str1);
	strcpy(msg + len, str2);

	pthread_mutex_lock(&log->logfile_mutex);
	{
		if (log->logfile)
		{
			fprintf(log->logfile, "%s\n", msg);
		}
		else
		{
			fprintf(stderr, "%s\n", msg);
		}

#ifdef LINUX_HOST_OS
		/* We want only serious log in 'syslog', not our debug
		 and trace logs */
		if (log->log_syslog && level &&
				(level <= log->sys_log_level))
		syslog ((level-1), "%s\n", msg);
#endif
	}
	pthread_mutex_unlock(&log->logfile_mutex);
out:
	free(msg);
	free(str1);
	free(str2);

	return ret;
}

int _log(log_handle_t* log, const char *domain, const char *file,
		const char *function, int line, loglevel_t level, const char *fmt, ...) {
	const char *basename = NULL;
	FILE *new_logfile = NULL;
	va_list ap;
	char timestr[256] = { 0, };
	struct timeval tv = { 0, };
	char *str1 = NULL;
	char *str2 = NULL;
	char *msg = NULL;
	size_t len = 0;
	int ret = 0;
	int fd = -1;

	if (level > log->loglevel)
	{
		goto out;
	}

	static char *level_strings[] = { "", /* NONE */
									"M", /* EMERGENCY */
									"A", /* ALERT */
									"C", /* CRITICAL */
									"E", /* ERROR */
									"W", /* WARNING */
									"N", /* NOTICE */
									"I", /* INFO */
									"D", /* DEBUG */
									"T", /* TRACE */
									"" };

	if (!domain || !file || !function || !fmt)
	{
		fprintf(stderr, "logging: %s:%s():%d: invalid argument\n", __FILE__,
				__PRETTY_FUNCTION__, __LINE__);
		return -1;
	}

	basename = strrchr(file, '/');
	if (basename)
	{
		basename++;
	}
	else
	{
		basename = file;
	}

#if defined(USE_SYSLOG)
	if (log->log_control_file_found)
	{
		int priority;
		/* treat LOG_TRACE and LOG_NONE as LOG_DEBUG and
		 other level as is */
		if (LOG_TRACE == level || LOG_NONE == level)
		{
			priority = LOG_DEBUG;
		}
		else
		{
			priority = level - 1;
		}

		va_start (ap, fmt);
		vasprintf (&str2, fmt, ap);
		va_end (ap);

		syslog (ERR_DEV, priority,
				"[%s:%d:%s] %d-%s: %s",
				basename, line, function,
				((this->graph) ? this->graph->id:0), domain, str2);
		goto err;
	}
#endif /* USE_SYSLOG */

	if (log->logrotate)
	{
		log->logrotate = 0;
		fd = open(log->filename, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
		if (fd < 0)
		{
			free(msg);
			free(str1);
			free(str2);
			log(log, "logrotate", LOG_ERROR, "%s", strerror(errno));
			return -1;
		}
		close(fd);

		new_logfile = fopen(log->filename, "a");
		if (!new_logfile)
		{
			log(log, "logrotate", LOG_CRITICAL,
					"failed to open logfile %s (%s)", log->filename,
					strerror(errno));
			goto log;
		}

		pthread_mutex_lock(&log->logfile_mutex);
		{
			if (log->logfile)
			{
				fclose(log->logfile);
			}

			log->log_logfile = log->logfile = new_logfile;
		}
		pthread_mutex_unlock(&log->logfile_mutex);

	}

log:
	ret = gettimeofday(&tv, NULL);
	if (-1 == ret)
	{
		goto out;
	}
	va_start(ap, fmt);
	time_fmt(timestr, sizeof timestr, tv.tv_sec, timefmt_FT);
	snprintf(timestr + strlen(timestr), sizeof timestr - strlen(timestr),
			".%"PRI_SUSECONDS, tv.tv_usec);

	ret = j_asprintf(&str1, "[%s] %s [%s:%d:%s] %s: ", timestr,
			level_strings[level], basename, line, function, domain);
	if (-1 == ret)
	{
		goto err;
	}

	ret = vasprintf(&str2, fmt, ap);
	if (-1 == ret)
	{
		goto err;
	}

	va_end(ap);

	len = strlen(str1);
	msg = (char*)malloc(sizeof(char) * (len + strlen(str2) + 1));

	strcpy(msg, str1);
	strcpy(msg + len, str2);

	pthread_mutex_lock(&log->logfile_mutex);
	{
		if (log->logfile)
		{
			fprintf(log->logfile, "%s\n", msg);
			fflush(log->logfile);
		}
		else
		{
			fprintf(stderr, "%s\n", msg);
			fflush(stderr);
		}

#ifdef LINUX_HOST_OS
		/* We want only serious log in 'syslog', not our debug
		 and trace logs */
		if (log->log_syslog && level &&
				(level <= log->sys_log_level))
		syslog ((level-1), "%s\n", msg);
#endif
	}

	pthread_mutex_unlock(&log->logfile_mutex);

err:
	free(msg);
	free(str1);
	free(str2);

out:
	return (0);
}

int _log_eh(log_handle_t* log, const char *function, const char *fmt, ...) {
	int ret = -1;
	va_list ap;
	char *str1 = NULL;
	char *str2 = NULL;
	char *msg = NULL;


	ret = j_asprintf(&str1, "%s: ", function);
	if (-1 == ret) {
		goto out;
	}

	va_start(ap, fmt);

	ret = vasprintf(&str2, fmt, ap);
	if (-1 == ret) {
		goto out;
	}

	va_end(ap);

	msg = (char*)malloc(sizeof(char) * (strlen(str1)  + strlen(str2) + 1));
	if (!msg) {
		ret = -1;
		goto out;
	}

	strcpy(msg, str1);
	strcat(msg, str2);

out:
	free(str1);
	/* Use FREE instead of free since str2 was allocated by vasprintf */
	if (str2)
	{
		free(str2);
	}

	return ret;
}

int cmd_log_init(log_handle_t* log, const char *filename) {
	int fd = -1;

	if (!filename)
	{
		log(log, "jhttpserver", LOG_CRITICAL, "cmd_log_init: no "
				"filename specified\n");
		return -1;
	}

	log->cmd_log_filename = strdup(filename);
	if (!log->cmd_log_filename)
	{
		log(log, "jhttpserver", LOG_CRITICAL, "cmd_log_init: strdup error\n");
		return -1;
	}
	/* close and reopen cmdlogfile for log rotate*/
	if (log->cmdlogfile)
	{
		fclose(log->cmdlogfile);
		log->cmdlogfile = NULL;
	}

	fd = open(log->cmd_log_filename, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
	if (fd < 0)
	{
		log(log, "jhttpserver", LOG_CRITICAL, "%s", strerror(errno));
		return -1;
	}
	close(fd);

	log->cmdlogfile = fopen(log->cmd_log_filename, "a");
	if (!log->cmdlogfile)
	{
		log(log, "jhttpserver", LOG_CRITICAL,
				"cmd_log_init: failed to open logfile \"%s\" "
						"(%s)\n", log->cmd_log_filename, strerror(errno));
		return -1;
	}
	return 0;
}

int cmd_log(log_handle_t* log, const char *domain, const char *fmt, ...)
{
	va_list ap;
	char timestr[64];
	struct timeval tv = { 0, };
	char *str1 = NULL;
	char *str2 = NULL;
	char *msg = NULL;
	size_t len = 0;
	int ret = 0;

	if (!log->cmdlogfile)
	{
		return -1;
	}

	if (!domain || !fmt)
	{
		log(log, "jhttpserverd", LOG_TRACE, "logging: invalid argument\n");
		return -1;
	}

	ret = gettimeofday(&tv, NULL);
	if (ret == -1)
	{
		goto out;
	}
	va_start(ap, fmt);
	time_fmt(timestr, sizeof timestr, tv.tv_sec, timefmt_FT);
	snprintf(timestr + strlen(timestr), 256 - strlen(timestr),
			".%"PRI_SUSECONDS, tv.tv_usec);

	ret = j_asprintf(&str1, "[%s] %s : ", timestr, domain);
	if (ret == -1)
	{
		goto out;
	}

	ret = vasprintf(&str2, fmt, ap);
	if (ret == -1)
	{
		goto out;
	}

	va_end(ap);

	len = strlen(str1);
	msg = (char*)malloc(sizeof(char) * (len + strlen(str2) + 1));

	strcpy(msg, str1);
	strcpy(msg + len, str2);

	fprintf(log->cmdlogfile, "%s\n", msg);
	fflush(log->cmdlogfile);

out:
	free(msg);
	free(str1);
	free(str2);

	return (0);
}

int j_asprintf(char **string_ptr, const char *format, ...)
{
	va_list arg;
	int rv = 0;

	va_start(arg, format);
	rv = j_vasprintf(string_ptr, format, arg);
	va_end(arg);

	return rv;
}

int j_vasprintf(char **string_ptr, const char *format, va_list arg)
{
	va_list arg_save;
	char *str = NULL;
	int size = 0;
	int rv = 0;

	if (!string_ptr || !format)
	{
		return -1;
	}

	va_copy(arg_save, arg);

	size = vsnprintf(NULL, 0, format, arg);
	size++;
	str = (char*)malloc(size * sizeof(char));
	if (str == NULL)
	{
		/* log is done in malloc itself */
		return -1;
	}
	rv = vsnprintf(str, size, format, arg_save);
	*string_ptr = str;
	return (rv);
}
