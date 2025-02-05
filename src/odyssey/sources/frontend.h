#ifndef ODYSSEY_FRONTEND_H
#define ODYSSEY_FRONTEND_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#define MAX_STARTUP_ATTEMPTS 7

extern bool version_matching;
extern bool version_matching_connect_higher_version;
extern int yb_max_query_size;
extern int yb_wait_timeout;

static inline machine_msg_t *od_frontend_error_msg(od_client_t *client,
						   machine_msg_t *stream,
						   char *code, char *fmt,
						   va_list args)
{
	char *msg = malloc(yb_max_query_size + 1);
	msg[yb_max_query_size] = '\0';
	int msg_len;
	msg_len = od_snprintf(msg, yb_max_query_size,
			      "odyssey: %s%.*s: ", client->id.id_prefix,
			      (signed)sizeof(client->id.id), client->id.id);
	msg_len +=
		od_vsnprintf(msg + msg_len, yb_max_query_size - msg_len, fmt, args);
	machine_msg_t *ret = kiwi_be_write_error(stream, code, msg, msg_len);
	free(msg);
	return ret;
}

static inline machine_msg_t *od_frontend_fatal_msg(od_client_t *client,
						   machine_msg_t *stream,
						   char *code, char *fmt,
						   va_list args)
{
	char *msg = malloc(yb_max_query_size + 1);
	msg[yb_max_query_size] = '\0';
	int msg_len;
	msg_len = od_snprintf(msg, yb_max_query_size,
			      "odyssey: %s%.*s: ", client->id.id_prefix,
			      (signed)sizeof(client->id.id), client->id.id);
	msg_len +=
		od_vsnprintf(msg + msg_len, yb_max_query_size - msg_len, fmt, args);
	machine_msg_t *ret = kiwi_be_write_error_fatal(stream, code, msg, msg_len);
	free(msg);
	return ret;
}

static inline machine_msg_t *od_frontend_fatal_msg_forward(od_client_t *client,
						   machine_msg_t *stream,
						   char *code, char *fmt,
						   va_list args)
{
	char *msg = malloc(yb_max_query_size + 1);
	msg[yb_max_query_size] = '\0';
	int msg_len;

	msg_len = od_snprintf(msg, yb_max_query_size,
			      "");
	msg_len +=
		od_vsnprintf(msg + msg_len, yb_max_query_size - msg_len, fmt, args);
	machine_msg_t *ret = kiwi_be_write_error_fatal(stream, code, msg, msg_len);
	free(msg);
	return ret;
}

static inline machine_msg_t *od_frontend_errorf(od_client_t *client,
						machine_msg_t *stream,
						char *code, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	machine_msg_t *msg;
	msg = od_frontend_error_msg(client, stream, code, fmt, args);
	va_end(args);
	return msg;
}

static inline machine_msg_t *od_frontend_info_msg(od_client_t *client,
						  machine_msg_t *stream,
						  char *fmt, va_list args)
{
	char *msg = malloc(yb_max_query_size + 1);
	msg[yb_max_query_size] = '\0';
	int msg_len;
	msg_len = od_snprintf(msg, yb_max_query_size,
			      "odyssey: %s%.*s: ", client->id.id_prefix,
			      (signed)sizeof(client->id.id), client->id.id);
	msg_len +=
		od_vsnprintf(msg + msg_len, yb_max_query_size - msg_len, fmt, args);
	machine_msg_t *ret = kiwi_be_write_notice_info(stream, msg, msg_len);
	free(msg);
	return ret;
}

static inline machine_msg_t *
od_frontend_infof(od_client_t *client, machine_msg_t *stream, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	machine_msg_t *msg;
	msg = od_frontend_info_msg(client, stream, fmt, args);
	va_end(args);
	return msg;
}

int od_frontend_info(od_client_t *, char *, ...);
int od_frontend_error(od_client_t *, char *, char *, ...);
int od_frontend_fatal(od_client_t *, char *, char *, ...);
int od_frontend_fatal_forward(od_client_t *, char *, char *, ...);
void od_frontend(void *);
extern int yb_execute_on_control_connection(od_client_t *,
					    int (*)(od_client_t *,
						    od_server_t *));
extern int yb_auth_via_auth_backend(od_client_t *client);

#endif /* ODYSSEY_FRONTEND_H */
