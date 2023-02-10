

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

void od_config_init(od_config_t *config)
{
	config->daemonize = 0;
	config->priority = 0;
	config->log_debug = 0;
	config->log_to_stdout = 1;
	config->log_config = 0;
	config->log_session = 1;
	config->log_query = 0;
	config->log_file = NULL;
	config->log_stats = 1;
	config->log_general_stats_prom = 0;
	config->log_route_stats_prom = 0;
	config->stats_interval = 3;
	config->log_format = NULL;
	config->pid_file = NULL;
	config->unix_socket_dir = NULL;
	config->locks_dir = NULL;
	config->enable_online_restart_feature = 0;
	config->bindwith_reuseport = 0;
	config->graceful_die_on_errors = 0;
	config->unix_socket_mode = NULL;

	config->log_syslog = 0;
	config->log_syslog_ident = NULL;
	config->log_syslog_facility = NULL;

	config->readahead = 8192;
	config->nodelay = 1;

	config->keepalive = 15;
	config->keepalive_keep_interval = 5;
	config->keepalive_probes = 3;
	config->keepalive_usr_timeout = 0; // use sys default

	config->workers = 1;
	config->resolvers = 1;
	config->client_max_set = 0;
	config->client_max = 0;
	config->client_max_routing = 0;
	config->server_login_retry = 1;
	config->cache_coroutine = 0;
	config->cache_msg_gc_size = 0;
	config->coroutine_stack_size = 4;
	config->hba_file = NULL;
	od_list_init(&config->listen);
}

void od_config_reload(od_config_t *current_config, od_config_t *new_config)
{
	current_config->client_max_set = new_config->client_max_set;
	current_config->client_max = new_config->client_max;
	current_config->client_max_routing = new_config->client_max_routing;
	current_config->server_login_retry = new_config->server_login_retry;
}

static void od_config_listen_free(od_config_listen_t *);

void od_config_free(od_config_t *config)
{
	od_list_t *i, *n;
	od_list_foreach_safe(&config->listen, i, n)
	{
		od_config_listen_t *listen;
		listen = od_container_of(i, od_config_listen_t, link);
		od_config_listen_free(listen);
	}
	if (config->log_file)
		free(config->log_file);
	if (config->log_format)
		free(config->log_format);
	if (config->pid_file)
		free(config->pid_file);
	if (config->unix_socket_dir)
		free(config->unix_socket_dir);
	if (config->log_syslog_ident)
		free(config->log_syslog_ident);
	if (config->log_syslog_facility)
		free(config->log_syslog_facility);
	if (config->locks_dir) {
		free(config->locks_dir);
		if (config->hba_file)
			free(config->hba_file);
	}
}

od_config_listen_t *od_config_listen_add(od_config_t *config)
{
	od_config_listen_t *listen;

	listen = (od_config_listen_t *)malloc(sizeof(*listen));
	if (listen == NULL) {
		return NULL;
	}

	memset(listen, 0, sizeof(*listen));

	listen->tls_opts = od_tls_opts_alloc();
	if (listen->tls_opts == NULL) {
		return NULL;
	}

	listen->port = 6432;
	listen->backlog = 128;
	listen->client_login_timeout = 15000;

	od_list_init(&listen->link);
	od_list_append(&config->listen, &listen->link);

	return listen;
}

static void od_config_listen_free(od_config_listen_t *config)
{
	if (config->host)
		free(config->host);

	if (config->tls_opts) {
		od_tls_opts_free(config->tls_opts);
	}
	free(config);
}

int od_config_validate(od_config_t *config, od_logger_t *logger)
{
	/* workers */
	if (config->workers <= 0) {
		od_error(logger, "config", NULL, NULL, "bad workers number");
		return -1;
	}

	/* resolvers */
	if (config->resolvers <= 0) {
		od_error(logger, "config", NULL, NULL, "bad resolvers number");
		return -1;
	}

	/* coroutine_stack_size */
	if (config->coroutine_stack_size < 4) {
		od_error(logger, "config", NULL, NULL,
			 "bad coroutine_stack_size number");
		return -1;
	}

	/* log format */
	if (config->log_format == NULL) {
		od_error(logger, "config", NULL, NULL, "log is not defined");
		return -1;
	}

	/* unix_socket_mode */
	if (config->unix_socket_dir) {
		if (config->unix_socket_mode == NULL) {
			od_error(logger, "config", NULL, NULL,
				 "unix_socket_mode is not set");
			return -1;
		}
	}

	/* listen */
	if (od_list_empty(&config->listen)) {
		od_error(logger, "config", NULL, NULL,
			 "no listen servers defined");
		return -1;
	}

	od_list_t *i;
	od_list_foreach(&config->listen, i)
	{
		od_config_listen_t *listen;
		listen = od_container_of(i, od_config_listen_t, link);
		if (listen->host == NULL) {
			if (config->unix_socket_dir == NULL) {
				od_error(
					logger, "config", NULL, NULL,
					"listen host is not set and no unix_socket_dir is specified");
				return -1;
			}
		}

		/* tls options */
		if (listen->tls_opts->tls) {
			if (strcmp(listen->tls_opts->tls, "disable") == 0) {
				listen->tls_opts->tls_mode =
					OD_CONFIG_TLS_DISABLE;
			} else if (strcmp(listen->tls_opts->tls, "allow") ==
				   0) {
				listen->tls_opts->tls_mode =
					OD_CONFIG_TLS_ALLOW;
			} else if (strcmp(listen->tls_opts->tls, "require") ==
				   0) {
				listen->tls_opts->tls_mode =
					OD_CONFIG_TLS_REQUIRE;
			} else if (strcmp(listen->tls_opts->tls, "verify_ca") ==
				   0) {
				listen->tls_opts->tls_mode =
					OD_CONFIG_TLS_VERIFY_CA;
			} else if (strcmp(listen->tls_opts->tls,
					  "verify_full") == 0) {
				listen->tls_opts->tls_mode =
					OD_CONFIG_TLS_VERIFY_FULL;
			} else {
				od_error(logger, "config", NULL, NULL,
					 "unknown tls_opts->tls mode");
				return -1;
			}
		}
	}

	if (config->enable_online_restart_feature &&
	    !config->bindwith_reuseport) {
		od_dbg_printf_on_dvl_lvl(1, "validation error detected %s\n",
					 "");
		od_error(
			logger, "config", NULL, NULL,
			"online restart feature works only with SO_REUSEPORT. Disable "
			"online restart or/and enable bindwith_reuseport");
		return NOT_OK_RESPONSE;
	}

	return OK_RESPONSE;
}

static inline char *od_config_yes_no(int value)
{
	return value ? "yes" : "no";
}

void od_config_print(od_config_t *config, od_logger_t *logger)
{
	od_log(logger, "config", NULL, NULL, "daemonize               %s",
	       od_config_yes_no(config->daemonize));
	od_log(logger, "config", NULL, NULL, "priority                %d",
	       config->priority);
	if (config->pid_file)
		od_log(logger, "config", NULL, NULL,
		       "pid_file                %s", config->pid_file);
	if (config->unix_socket_dir) {
		od_log(logger, "config", NULL, NULL,
		       "unix_socket_dir         %s", config->unix_socket_dir);
		od_log(logger, "config", NULL, NULL,
		       "unix_socket_mode        %s", config->unix_socket_mode);
	}
	if (config->log_format)
		od_log(logger, "config", NULL, NULL,
		       "log_format              %s", config->log_format);
	if (config->log_file)
		od_log(logger, "config", NULL, NULL,
		       "log_file                %s", config->log_file);
	od_log(logger, "config", NULL, NULL, "log_to_stdout           %s",
	       od_config_yes_no(config->log_to_stdout));
	od_log(logger, "config", NULL, NULL, "log_syslog              %s",
	       od_config_yes_no(config->log_syslog));
	if (config->log_syslog_ident)
		od_log(logger, "config", NULL, NULL,
		       "log_syslog_ident        %s", config->log_syslog_ident);
	if (config->log_syslog_facility)
		od_log(logger, "config", NULL, NULL,
		       "log_syslog_facility     %s",
		       config->log_syslog_facility);
	od_log(logger, "config", NULL, NULL, "log_debug               %s",
	       od_config_yes_no(config->log_debug));
	od_log(logger, "config", NULL, NULL, "log_config              %s",
	       od_config_yes_no(config->log_config));
	od_log(logger, "config", NULL, NULL, "log_session             %s",
	       od_config_yes_no(config->log_session));
	od_log(logger, "config", NULL, NULL, "log_query               %s",
	       od_config_yes_no(config->log_query));
	od_log(logger, "config", NULL, NULL, "log_stats               %s",
	       od_config_yes_no(config->log_stats));
	od_log(logger, "config", NULL, NULL, "stats_interval          %d",
	       config->stats_interval);
	od_log(logger, "config", NULL, NULL, "readahead               %d",
	       config->readahead);
	od_log(logger, "config", NULL, NULL, "nodelay                 %s",
	       od_config_yes_no(config->nodelay));
	od_log(logger, "config", NULL, NULL, "keepalive               %d",
	       config->keepalive);
	if (config->client_max_set)
		od_log(logger, "config", NULL, NULL,
		       "client_max              %d", config->client_max);
	od_log(logger, "config", NULL, NULL, "client_max_routing      %d",
	       config->client_max_routing);
	od_log(logger, "config", NULL, NULL, "server_login_retry      %d",
	       config->server_login_retry);
	od_log(logger, "config", NULL, NULL, "cache_msg_gc_size       %d",
	       config->cache_msg_gc_size);
	od_log(logger, "config", NULL, NULL, "cache_coroutine         %d",
	       config->cache_coroutine);
	od_log(logger, "config", NULL, NULL, "coroutine_stack_size    %d",
	       config->coroutine_stack_size);
	od_log(logger, "config", NULL, NULL, "workers                 %d",
	       config->workers);
	od_log(logger, "config", NULL, NULL, "resolvers               %d",
	       config->resolvers);

	if (config->enable_online_restart_feature) {
		od_log(logger, "config", NULL, NULL,
		       "online restart enabled: OK");
	}
	if (config->graceful_die_on_errors) {
		od_log(logger, "config", NULL, NULL,
		       "graceful die enabled:   OK");
	}
	if (config->bindwith_reuseport) {
		od_log(logger, "config", NULL, NULL,
		       "socket bind with:       SO_REUSEPORT");
	}
#ifdef USE_SCRAM
	od_log(logger, "config", NULL, NULL, "SCRAM auth metod:       OK");
#endif

	od_log(logger, "config", NULL, NULL, "");
	od_list_t *i;
	od_list_foreach(&config->listen, i)
	{
		od_config_listen_t *listen;
		listen = od_container_of(i, od_config_listen_t, link);
		od_log(logger, "config", NULL, NULL, "listen");
		od_log(logger, "config", NULL, NULL, "  host          %s",
		       listen->host ? listen->host : "<unix socket>");
		od_log(logger, "config", NULL, NULL, "  port          %d",
		       listen->port);
		od_log(logger, "config", NULL, NULL, "  backlog       %d",
		       listen->backlog);
		if (listen->tls_opts->tls)
			od_log(logger, "config", NULL, NULL,
			       "  tls           %s", listen->tls_opts->tls);
		if (listen->tls_opts->tls_ca_file)
			od_log(logger, "config", NULL, NULL,
			       "  tls_ca_file   %s",
			       listen->tls_opts->tls_ca_file);
		if (listen->tls_opts->tls_key_file)
			od_log(logger, "config", NULL, NULL,
			       "  tls_key_file  %s",
			       listen->tls_opts->tls_key_file);
		if (listen->tls_opts->tls_cert_file)
			od_log(logger, "config", NULL, NULL,
			       "  tls_cert_file %s",
			       listen->tls_opts->tls_cert_file);
		if (listen->tls_opts->tls_protocols)
			od_log(logger, "config", NULL, NULL,
			       "  tls_protocols %s",
			       listen->tls_opts->tls_protocols);
		od_log(logger, "config", NULL, NULL, "");
	}
}
