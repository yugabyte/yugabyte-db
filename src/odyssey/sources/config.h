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

	/* YB: Use uint32 as multiple APIs expect timeout value to be uint32_t*/
	uint32_t client_login_timeout;
	int compression;

	od_list_t link;
};

enum yb_od_alter_guc_adoption {
	YB_GUC_ADOPTION_FLUCTUATING,
	YB_GUC_ADOPTION_GRADUAL,
	YB_GUC_ADOPTION_CONNECTION_STATIC,
};

struct od_config {
	int daemonize;
	int priority;
	/* logging */
	int log_to_stdout;
	/* YB: Use _Atomic types to support config reload */
	_Atomic int log_debug;
	_Atomic int log_config;
	_Atomic int log_session;
	_Atomic int log_query;
	char *log_dir;
	int log_max_size;
	int log_rotate_interval;
	char *log_format;
	/* YB: Use _Atomic type to support config reload */
	_Atomic int log_stats;
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

	/* YB */
	int yb_ysql_max_connections;
	int yb_use_auth_backend;
	int yb_optimized_extended_query_protocol;
	int yb_enable_multi_route_pool;
	int yb_optimized_session_parameters;
	int yb_max_pools;
	int yb_enable_prep_stmt_close;
	int TEST_yb_auth_delay_ms;
	enum yb_od_alter_guc_adoption yb_alter_guc_adoption_strategy;
	int yb_alter_guc_stale_backend_ttl_ms;
	_Atomic int yb_max_prepared_statements;
	_Atomic int yb_tcmalloc_gc_interval;
	_Atomic int yb_enable_parse_queue_tracking;
};

void od_config_init(od_config_t *);
void od_config_free(od_config_t *);
void od_config_reload(od_config_t *, od_config_t *);
int od_config_validate(od_config_t *, od_logger_t *);
void od_config_print(od_config_t *, od_logger_t *);

od_config_listen_t *od_config_listen_add(od_config_t *);

#endif /* ODYSSEY_CONFIG_H */
