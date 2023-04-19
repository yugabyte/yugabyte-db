
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <channel_limit.h>
#include <odyssey.h>

typedef struct {
	char *name;
	int id;
} od_log_syslog_facility_t;

static od_log_syslog_facility_t od_log_syslog_facilities[] = {
	{ "daemon", LOG_DAEMON },
	{ "user", LOG_USER },
	{ "local0", LOG_LOCAL0 },
	{ "local1", LOG_LOCAL1 },
	{ "local2", LOG_LOCAL2 },
	{ "local3", LOG_LOCAL3 },
	{ "local4", LOG_LOCAL4 },
	{ "local5", LOG_LOCAL5 },
	{ "local6", LOG_LOCAL6 },
	{ "local7", LOG_LOCAL7 },
	{ NULL, 0 }
};

static int od_log_syslog_level[] = { LOG_INFO, LOG_ERR, LOG_DEBUG, LOG_CRIT };

static char *od_log_level[] = { "info", "error", "debug", "fatal" };

od_retcode_t od_logger_init(od_logger_t *logger, od_pid_t *pid)
{
	logger->pid = pid;
	logger->log_debug = 0;
	logger->log_stdout = 1;
	logger->log_syslog = 0;
	logger->format = NULL;
	logger->format_len = 0;
	logger->fd = -1;
	logger->loaded = 0;

	/* set temporary format */
	od_logger_set_format(logger, "%p %t %l (%c) %h %m\n");

	return OK_RESPONSE;
}

static inline void od_logger(void *arg);

#define OD_LOGGER_TASK_CHANNEL_LIMIT 1 << 8

od_retcode_t od_logger_load(od_logger_t *logger)
{
	// we should do this in separate function, after config read and machinauim initialization
	logger->task_channel = machine_channel_create();
	if (logger->task_channel == NULL) {
		return NOT_OK_RESPONSE;
	}

	machine_channel_assign_limit_policy(logger->task_channel,
					    OD_LOGGER_TASK_CHANNEL_LIMIT,
					    MM_CHANNEL_LIMIT_SOFT);

	char name[32];
	od_snprintf(name, sizeof(name), "logger");
	logger->machine = machine_create(name, od_logger, logger);

	if (logger->machine == -1) {
		machine_channel_free(logger->task_channel);
		return NOT_OK_RESPONSE;
	}

	logger->loaded = 1;
	return OK_RESPONSE;
}

int od_logger_open(od_logger_t *logger, char *path)
{
	logger->fd = open(path, O_RDWR | O_CREAT | O_APPEND, 0644);
	if (logger->fd == -1)
		return -1;
	return 0;
}

int od_logger_reopen(od_logger_t *logger, char *path)
{
	int old_fd = logger->fd;
	int rc = od_logger_open(logger, path);
	if (rc == -1)
		logger->fd = old_fd;
	else if (old_fd != -1)
		close(old_fd);
	return rc;
}

int od_logger_open_syslog(od_logger_t *logger, char *ident, char *facility)
{
	int facility_id = LOG_DAEMON;
	if (facility) {
		int i = 0;
		od_log_syslog_facility_t *facility_ptr;
		for (;;) {
			facility_ptr = &od_log_syslog_facilities[i];
			if (facility_ptr->name == NULL)
				break;
			if (strcasecmp(facility_ptr->name, facility) == 0) {
				facility_id = facility_ptr->id;
				break;
			}
			i++;
		}
	}
	logger->log_syslog = 1;
	if (ident == NULL)
		ident = "odyssey";
	openlog(ident, 0, facility_id);
	return 0;
}

void od_logger_close(od_logger_t *logger)
{
	if (logger->fd != -1)
		close(logger->fd);
	logger->fd = -1;
}

static char od_logger_escape_tab[256] = {
	['\0'] = '0', ['\t'] = 't',  ['\n'] = 'n',
	['\r'] = 'r', ['\\'] = '\\', ['='] = '='
};

__attribute__((hot)) static inline int od_logger_escape(char *dest, int size,
							char *fmt, va_list args)
{
	char prefmt[512];
	int prefmt_len;
	prefmt_len = od_vsnprintf(prefmt, sizeof(prefmt), fmt, args);

	char *dst_pos = dest;
	char *dst_end = dest + size;
	char *msg_pos = prefmt;
	char *msg_end = prefmt + prefmt_len;

	while (msg_pos < msg_end) {
		char escaped_char;
		escaped_char = od_logger_escape_tab[(int)*msg_pos];
		if (od_unlikely(escaped_char)) {
			if (od_unlikely((dst_end - dst_pos) < 2))
				break;
			dst_pos[0] = '\\';
			dst_pos[1] = escaped_char;
			dst_pos += 2;
		} else {
			if (od_unlikely((dst_end - dst_pos) < 1))
				break;
			dst_pos[0] = *msg_pos;
			dst_pos += 1;
		}
		msg_pos++;
	}
	return dst_pos - dest;
}

__attribute__((hot)) static inline int
od_logger_format(od_logger_t *logger, od_logger_level_t level, char *context,
		 od_client_t *client, od_server_t *server, char *fmt,
		 va_list args, char *output, int output_len)
{
	char *dst_pos = output;
	char *dst_end = output + output_len;
	char *format_pos = logger->format;
	char *format_end = logger->format + logger->format_len;
	char peer[128];

	int len;
	while (format_pos < format_end) {
		if (*format_pos == '\\') {
			format_pos++;
			if (od_unlikely(format_pos == format_end))
				break;
			if (od_unlikely((dst_end - dst_pos) < 1))
				break;
			switch (*format_pos) {
			case '\\':
				dst_pos[0] = '\\';
				dst_pos += 1;
				break;
			case 'n':
				dst_pos[0] = '\n';
				dst_pos += 1;
				break;
			case 't':
				dst_pos[0] = '\t';
				dst_pos += 1;
				break;
			case 'r':
				dst_pos[0] = '\r';
				dst_pos += 1;
				break;
			default:
				if (od_unlikely((dst_end - dst_pos) < 2))
					break;
				dst_pos[0] = '\\';
				dst_pos[1] = *format_pos;
				dst_pos += 2;
				break;
			}
		} else if (*format_pos == '%') {
			format_pos++;
			if (od_unlikely(format_pos == format_end))
				break;
			switch (*format_pos) {
			/* unixtime */
			case 'n': {
				time_t tm = time(NULL);
				len = od_snprintf(dst_pos, dst_end - dst_pos,
						  "%lu", tm);
				dst_pos += len;
				break;
			}
			/* timestamp */
			case 't': {
				struct timeval tv;
				gettimeofday(&tv, NULL);
				len = strftime(dst_pos, dst_end - dst_pos,
					       "%FT%TZ", gmtime(&tv.tv_sec));
				dst_pos += len;

				break;
			}
			/* millis */
			case 'e': {
				struct timeval tv;
				gettimeofday(&tv, NULL);
				len = od_snprintf(dst_pos, dst_end - dst_pos,
						  "%03d",
						  (signed)tv.tv_usec / 1000);
				dst_pos += len;
				break;
			}
			/* pid */
			case 'p':
				len = od_snprintf(dst_pos, dst_end - dst_pos,
						  "%s", logger->pid->pid_sz);
				dst_pos += len;
				break;
			/* client id */
			case 'i':
				if (client && client->id.id_prefix != NULL) {
					len = od_snprintf(
						dst_pos, dst_end - dst_pos,
						"%s%.*s", client->id.id_prefix,
						(signed)sizeof(client->id.id),
						client->id.id);
					dst_pos += len;
					break;
				}
				len = od_snprintf(dst_pos, dst_end - dst_pos,
						  "none");
				dst_pos += len;
				break;
			/* server id */
			case 's':
				if (server && server->id.id_prefix != NULL) {
					len = od_snprintf(
						dst_pos, dst_end - dst_pos,
						"%s%.*s", server->id.id_prefix,
						(signed)sizeof(server->id.id),
						server->id.id);
					dst_pos += len;
					break;
				}
				len = od_snprintf(dst_pos, dst_end - dst_pos,
						  "none");
				dst_pos += len;
				break;
			/* user name */
			case 'u':
				if (client && client->startup.user.value_len) {
					len = od_snprintf(
						dst_pos, dst_end - dst_pos,
						"%s",
						client->startup.user.value);
					dst_pos += len;
					break;
				}
				len = od_snprintf(dst_pos, dst_end - dst_pos,
						  "none");
				dst_pos += len;
				break;
			/* database name */
			case 'd':
				if (client &&
				    client->startup.database.value_len) {
					len = od_snprintf(
						dst_pos, dst_end - dst_pos,
						"%s",
						client->startup.database.value);
					dst_pos += len;
					break;
				}
				len = od_snprintf(dst_pos, dst_end - dst_pos,
						  "none");
				dst_pos += len;
				break;
			/* context */
			case 'c':
				len = od_snprintf(dst_pos, dst_end - dst_pos,
						  "%s", context);
				dst_pos += len;
				break;
			/* level */
			case 'l':
				len = od_snprintf(dst_pos, dst_end - dst_pos,
						  "%s", od_log_level[level]);
				dst_pos += len;
				break;
			/* message */
			case 'm':
				len = od_vsnprintf(dst_pos, dst_end - dst_pos,
						   fmt, args);
				dst_pos += len;
				break;
			/* message (escaped) */
			case 'M':
				len = od_logger_escape(
					dst_pos, dst_end - dst_pos, fmt, args);
				dst_pos += len;
				break;
			/* server host */
			case 'H':
				if (client && client->route) {
					od_route_t *route_ref = client->route;
					len = od_snprintf(
						dst_pos, dst_end - dst_pos,
						"%s",
						route_ref->rule->storage->host);
					dst_pos += len;
					break;
				}
				len = od_snprintf(dst_pos, dst_end - dst_pos,
						  "none");
				dst_pos += len;
				break;
			/* client host */
			case 'h':
				if (client && client->io.io) {
					od_getpeername(client->io.io, peer,
						       sizeof(peer), 1, 0);
					len = od_snprintf(dst_pos,
							  dst_end - dst_pos,
							  "%s", peer);
					dst_pos += len;
					break;
				}
				len = od_snprintf(dst_pos, dst_end - dst_pos,
						  "none");
				dst_pos += len;
				break;
			/* client port */
			case 'r':
				if (client && client->io.io) {
					od_getpeername(client->io.io, peer,
						       sizeof(peer), 0, 1);
					len = od_snprintf(dst_pos,
							  dst_end - dst_pos,
							  "%s", peer);
					dst_pos += len;
					break;
				}
				len = od_snprintf(dst_pos, dst_end - dst_pos,
						  "none");
				dst_pos += len;
				break;
			case '%':
				if (od_unlikely((dst_end - dst_pos) < 1))
					break;
				dst_pos[0] = '%';
				dst_pos += 1;
				break;
			default:
				if (od_unlikely((dst_end - dst_pos) < 2))
					break;
				dst_pos[0] = '%';
				dst_pos[1] = *format_pos;
				dst_pos += 2;
				break;
			}
		} else {
			if (od_unlikely((dst_end - dst_pos) < 1))
				break;
			dst_pos[0] = *format_pos;
			dst_pos += 1;
		}
		format_pos++;
	}
	return dst_pos - output;
}

typedef struct {
	od_logger_level_t lvl;
	char msg[FLEXIBLE_ARRAY_MEMBER];
} _od_log_entry;

static inline size_t od_log_entry_req_size(size_t msg_len)
{
	return sizeof(od_logger_level_t) +
	       sizeof(char) * (msg_len + /* NULL */ 1);
}

static inline void _od_logger_write(od_logger_t *l, char *data, int len,
				    od_logger_level_t lvl)
{
	int rc;
	if (l->fd != -1) {
		rc = write(l->fd, data, len);
	}
	if (l->log_stdout) {
		rc = write(STDOUT_FILENO, data, len);
	}
	if (l->log_syslog) {
		syslog(od_log_syslog_level[lvl], "%.*s", len, data);
	}
	(void)rc;
}

static inline void od_logger(void *arg)
{
	od_logger_t *logger = arg;

	for (;;) {
		machine_msg_t *msg;
		msg = machine_channel_read(logger->task_channel, UINT32_MAX);
		if (msg == NULL)
			break;

		od_msg_t msg_type;
		msg_type = machine_msg_type(msg);
		switch (msg_type) {
		case OD_MSG_LOG: {
			_od_log_entry *le = machine_msg_data(msg);
			int len = strlen(le->msg);

			_od_logger_write(logger, le->msg, len, le->lvl);
		} break;
		default: {
			assert(0);
		} break;
		}

		machine_msg_free(msg);
	}
}

void od_logger_write(od_logger_t *logger, od_logger_level_t level,
		     char *context, void *client, void *server, char *fmt,
		     va_list args)
{
	if (logger->fd == -1 && !logger->log_stdout && !logger->log_syslog)
		return;

	if (level == OD_DEBUG) {
		int is_debug = logger->log_debug;
		if (!is_debug) {
			od_client_t *client_ref = client;
			od_server_t *server_ref = server;
			if (client_ref && client_ref->rule) {
				is_debug = client_ref->rule->log_debug;
			} else if (server_ref && server_ref->route) {
				od_route_t *route = server_ref->route;
				is_debug = route->rule->log_debug;
			}
		}
		if (!is_debug)
			return;
	}

	char output[OD_LOGLINE_MAXLEN];
	int len;
	len = od_logger_format(logger, level, context, client, server, fmt,
			       args, output, sizeof(output));
	if (logger->loaded && false) {
		/* create new log event and pass it to logger pool */
		machine_msg_t *msg;
		msg = machine_msg_create(od_log_entry_req_size(len));

		machine_msg_set_type(msg, OD_MSG_LOG);
		_od_log_entry *le = machine_msg_data(msg);
		strncpy(le->msg, output, len);
		le->msg[len] = '\0';

		machine_channel_write(logger->task_channel, msg);
	} else {
		_od_logger_write(logger, output, len, level);
	}
}

extern void od_logger_write_plain(od_logger_t *logger, od_logger_level_t level,
				  char *context, void *client, void *server,
				  char *string)
{
	if (logger->fd == -1 && !logger->log_stdout && !logger->log_syslog)
		return;

	if (level == OD_DEBUG) {
		int is_debug = logger->log_debug;
		if (!is_debug) {
			od_client_t *client_ref = client;
			od_server_t *server_ref = server;
			if (client_ref && client_ref->rule) {
				is_debug = client_ref->rule->log_debug;
			} else if (server_ref && server_ref->route) {
				od_route_t *route = server_ref->route;
				is_debug = route->rule->log_debug;
			}
		}
		if (!is_debug)
			return;
	}

	int len = strlen(string);
	char output[len + OD_LOGLINE_MAXLEN];
	va_list empty_va_list;
	len = od_logger_format(logger, level, context, client, server, string,
			       empty_va_list, output, len + 100);

	if (logger->loaded) {
		/* create new log event and pass it to logger pool */
		machine_msg_t *msg;
		msg = machine_msg_create(od_log_entry_req_size(len));

		machine_msg_set_type(msg, OD_MSG_LOG);
		_od_log_entry *le = machine_msg_data(msg);
		strncpy(le->msg, output, len);
		le->msg[len] = '\0';

		machine_channel_write(logger->task_channel, msg);
	} else {
		_od_logger_write(logger, output, len, level);
	}
}
