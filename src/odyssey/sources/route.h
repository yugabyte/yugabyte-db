#ifndef ODYSSEY_ROUTE_H
#define ODYSSEY_ROUTE_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

typedef struct od_route od_route_t;

struct od_route {
	od_rule_t *rule;
	od_route_id_t id;

	od_stat_t stats;
	od_stat_t stats_prev;
	bool stats_mark_db;

	od_server_pool_t server_pool;
	od_client_pool_t client_pool;

	kiwi_params_lock_t params;
	int64_t tcp_connections;
	int last_heartbeat;
	machine_channel_t *wait_bus;
	pthread_mutex_t lock;

	od_error_logger_t *err_logger;
	bool extra_logging_enabled;

	yb_db_entry_t* yb_database_entry;

	od_list_t link;
};

static inline void od_route_init(od_route_t *route, bool extra_route_logging)
{
	route->rule = NULL;
	route->tcp_connections = 0;
	route->last_heartbeat = 0;

	od_route_id_init(&route->id);
	od_server_pool_init(&route->server_pool);

	od_client_pool_init(&route->client_pool);

	/* stat init */
	route->stats_mark_db = false;
	route->extra_logging_enabled = extra_route_logging;
	if (extra_route_logging) {
		/* error logging */
		route->err_logger = od_err_logger_create_default();
	} else {
		route->err_logger = NULL;
	}

	od_stat_init(&route->stats);
	od_stat_init(&route->stats_prev);
	kiwi_params_lock_init(&route->params);
	od_list_init(&route->link);
	route->wait_bus = NULL;
	pthread_mutex_init(&route->lock, NULL);
}

static inline void od_route_free(od_route_t *route)
{
	od_route_id_free(&route->id);
	od_pg_server_pool_free(&route->server_pool);

	kiwi_params_lock_free(&route->params);
	if (route->wait_bus)
		machine_channel_free(route->wait_bus);
	if (route->stats.enable_quantiles) {
		od_stat_free(&route->stats);
	}

	if (route->extra_logging_enabled) {
		od_err_logger_free(route->err_logger);
		route->err_logger = NULL;
	}

	pthread_mutex_destroy(&route->lock);
	free(route);
}

static inline od_route_t *od_route_allocate()
{
	od_route_t *route = malloc(sizeof(*route));
	if (route == NULL)
		return NULL;
	od_route_init(route, true);
	route->wait_bus = machine_channel_create();
	if (route->wait_bus == NULL) {
		od_route_free(route);
		return NULL;
	}
	return route;
}

static inline void od_route_lock(od_route_t *route)
{
	pthread_mutex_lock(&route->lock);
}

static inline void od_route_unlock(od_route_t *route)
{
	pthread_mutex_unlock(&route->lock);
}

static inline int od_route_is_dynamic(od_route_t *route)
{
	return route->rule->db_is_default || route->rule->user_is_default;
}

static inline int od_route_match_compare_client_cb(od_client_t *client,
						   void **argv)
{
	return od_id_cmp(&client->id, argv[0]);
}

static inline od_client_t *od_route_match_client(od_route_t *route, od_id_t *id)
{
	void *argv[] = { id };
	od_client_t *match;
	match = od_client_pool_foreach(&route->client_pool, OD_CLIENT_ACTIVE,
				       od_route_match_compare_client_cb, argv);
	if (match)
		return match;
	match = od_client_pool_foreach(&route->client_pool, OD_CLIENT_QUEUE,
				       od_route_match_compare_client_cb, argv);
	if (match)
		return match;
	match = od_client_pool_foreach(&route->client_pool, OD_CLIENT_PENDING,
				       od_route_match_compare_client_cb, argv);
	if (match)
		return match;

	return NULL;
}

static inline void od_route_kill_client(od_route_t *route, od_id_t *id)
{
	od_client_t *client;
	client = od_route_match_client(route, id);
	if (client)
		od_client_kill(client);
}

static inline int od_route_kill_cb(od_client_t *client, void **argv)
{
	(void)argv;
	od_client_kill(client);
	return 0;
}

static inline int od_grac_shutdown_cb(od_server_t *server, void **argv)
{
	(void)argv;
	od_server_grac_shutdown(server);
	return 0;
}

static inline int od_route_reload_cb(od_server_t *server, void **argv)
{
	(void)argv;
	od_server_reload(server);
	return 0;
}

static inline void od_route_kill_client_pool(od_route_t *route)
{
	od_client_pool_foreach(&route->client_pool, OD_CLIENT_ACTIVE,
			       od_route_kill_cb, NULL);
	od_client_pool_foreach(&route->client_pool, OD_CLIENT_PENDING,
			       od_route_kill_cb, NULL);
	od_client_pool_foreach(&route->client_pool, OD_CLIENT_QUEUE,
			       od_route_kill_cb, NULL);
}

static inline void od_route_grac_shutdown_pool(od_route_t *route)
{
	od_server_pool_foreach(&route->server_pool, OD_SERVER_ACTIVE,
			       od_grac_shutdown_cb, NULL);
	od_server_pool_foreach(&route->server_pool, OD_SERVER_IDLE,
			       od_grac_shutdown_cb, NULL);
}

static inline void od_route_reload_pool(od_route_t *route)
{
	od_server_pool_foreach(&route->server_pool, OD_SERVER_ACTIVE,
			       od_route_reload_cb, NULL);
	od_server_pool_foreach(&route->server_pool, OD_SERVER_IDLE,
			       od_route_reload_cb, NULL);
}

static inline int od_route_wait(od_route_t *route, uint32_t time_ms)
{
	machine_msg_t *msg;
	msg = machine_channel_read(route->wait_bus, time_ms);
	if (msg) {
		machine_msg_free(msg);
		return 0;
	}
	return -1;
}

static inline int od_route_signal(od_route_t *route)
{
	machine_msg_t *msg;
	msg = machine_msg_create(0);
	if (msg == NULL) {
		return -1;
	}
	machine_channel_write(route->wait_bus, msg);
	return 0;
}

#endif /* ODYSSEY_ROUTE_H */
