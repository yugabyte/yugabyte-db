#ifndef ODYSSEY_LOGGER_H
#define ODYSSEY_LOGGER_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#define OD_LOGLINE_MAXLEN 1024

typedef struct od_logger od_logger_t;

typedef enum { OD_LOG, OD_ERROR, OD_DEBUG, OD_FATAL } od_logger_level_t;

struct od_logger {
	od_pid_t *pid;
	int log_debug;
	int log_stdout;
	int log_syslog;
	char *format;
	int format_len;

	int fd;

	int loaded;
	int64_t machine;
	/* makes sence only with use_asynclog option on */
	machine_channel_t *task_channel;
};

extern od_retcode_t od_logger_init(od_logger_t *, od_pid_t *);
extern od_retcode_t od_logger_load(od_logger_t *logger);

static inline void od_logger_set_debug(od_logger_t *logger, int enable)
{
	logger->log_debug = enable;
}

static inline void od_logger_set_stdout(od_logger_t *logger, int enable)
{
	logger->log_stdout = enable;
}

static inline void od_logger_set_format(od_logger_t *logger, char *format)
{
	logger->format = format;
	logger->format_len = strlen(format);
}

extern int od_logger_open(od_logger_t *, char *);
extern int od_logger_reopen(od_logger_t *, char *);
extern int od_logger_open_syslog(od_logger_t *, char *, char *);
extern void od_logger_close(od_logger_t *);
extern void od_logger_write(od_logger_t *, od_logger_level_t, char *, void *,
			    void *, char *, va_list);
extern void od_logger_write_plain(od_logger_t *, od_logger_level_t, char *,
				  void *, void *, char *);

static inline void od_log(od_logger_t *logger, char *context, void *client,
			  void *server, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	od_logger_write(logger, OD_LOG, context, client, server, fmt, args);
	va_end(args);
}

static inline void od_debug(od_logger_t *logger, char *context, void *client,
			    void *server, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	od_logger_write(logger, OD_DEBUG, context, client, server, fmt, args);
	va_end(args);
}

static inline void od_error(od_logger_t *logger, char *context, void *client,
			    void *server, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	od_logger_write(logger, OD_ERROR, context, client, server, fmt, args);
	va_end(args);
}

static inline void od_fatal(od_logger_t *logger, char *context, void *client,
			    void *server, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	od_logger_write(logger, OD_FATAL, context, client, server, fmt, args);
	va_end(args);
	exit(1);
}

#endif /* ODYSSEY_LOGGER_H */
