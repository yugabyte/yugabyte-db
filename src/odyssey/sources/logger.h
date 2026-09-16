#ifndef ODYSSEY_LOGGER_H
#define ODYSSEY_LOGGER_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#define OD_LOGLINE_MAXLEN 1024

typedef struct od_logger od_logger_t;

/*
 * YB: YB_OD_QUERY and YB_OD_SESSION print at the same verbosity ("debug") as
 * OD_DEBUG, but are gated independently by log_query/log_session instead
 * of log_debug, so they never enable (or get enabled by) unrelated
 * od_debug() call sites elsewhere in the codebase.
 */
typedef enum { OD_LOG, OD_ERROR, OD_DEBUG, OD_FATAL, YB_OD_QUERY, YB_OD_SESSION } od_logger_level_t;

struct od_logger {
	od_pid_t *pid;
	/* YB: Use _Atomic type to support config reload */
	_Atomic int log_debug;
	int log_stdout;
	int log_syslog;
	char *format;
	int format_len;

	int fd;
	char *log_dir;
	int current_log_size;
	int max_log_size;
	int rotate_interval;
	time_t next_rotate_timestamp;

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

static inline void od_logger_set_dir(od_logger_t *logger, char *dir)
{
	logger->log_dir = dir;
}

static inline void od_logger_set_max_size(od_logger_t *logger, int max_size)
{
	logger->max_log_size = max_size;
}

static inline void od_logger_set_rotate_interval(od_logger_t *logger, int rotate_interval)
{
	logger->rotate_interval = rotate_interval;
}

extern int od_logger_open(od_logger_t *);
extern int od_logger_reopen(od_logger_t *);
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

/*
 * YB: Query logs are gated by log_query at the
 * call site (including the existing per-route rule fallback), so these
 * wrappers don't re-check any flag here.
 */
static inline void yb_od_query(od_logger_t *logger, char *context, void *client,
			    void *server, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	od_logger_write(logger, YB_OD_QUERY, context, client, server, fmt, args);
	va_end(args);
}

/*
 * YB: Session logs are gated by log_session at the
 * call site (including the existing per-route rule fallback), so these
 * wrappers don't re-check any flag here.
 */
static inline void yb_od_session(od_logger_t *logger, char *context, void *client,
			      void *server, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	od_logger_write(logger, YB_OD_SESSION, context, client, server, fmt, args);
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
