#ifndef ODYSSEY_CONFIG_H
#define ODYSSEY_CONFIG_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

typedef struct od_config_listen od_config_listen_t;
typedef struct od_config od_config_t;

struct od_config_listen {
	od_tls_opts_t *tls_opts;

	char *host;
	int port;

	int backlog;

	int client_login_timeout;
	int compression;

	od_list_t link;
};

struct od_config {
	int daemonize;
	int priority;
	/* logging */
	int log_to_stdout;
	int log_debug;
	int log_config;
	int log_session;
	int log_query;
	char *log_file;
	char *log_format;
	int log_stats;
	int log_general_stats_prom;
	int log_route_stats_prom;
	int log_syslog;
	char *log_syslog_ident;
	char *log_syslog_facility;
	/*         */
	int stats_interval;
	/* system related settings */
	char *pid_file;
	char *unix_socket_dir;
	char *unix_socket_mode;
	char *locks_dir;
	/* sigusr2 etc */
	int graceful_die_on_errors;
	int enable_online_restart_feature;
	int bindwith_reuseport;
	/*                         */
	int readahead;
	int nodelay;

	/* TCP KEEPALIVE related settings */
	int keepalive;
	int keepalive_keep_interval;
	int keepalive_probes;
	int keepalive_usr_timeout;
	/*                                */
	int workers;
	int resolvers;
	/*         client                 */
	int client_max_set;
	int client_max;
	int client_max_routing;
	int server_login_retry;
	int reserve_session_server_connection;
	/*  */
	int cache_coroutine;
	int cache_msg_gc_size;
	int coroutine_stack_size;
	char *hba_file;
	od_list_t listen;
};

void od_config_init(od_config_t *);
void od_config_free(od_config_t *);
void od_config_reload(od_config_t *, od_config_t *);
int od_config_validate(od_config_t *, od_logger_t *);
void od_config_print(od_config_t *, od_logger_t *);

od_config_listen_t *od_config_listen_add(od_config_t *);

#endif /* ODYSSEY_CONFIG_H */
