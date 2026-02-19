
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>
#include <time.h>

static void clear_stats(struct ConnectionStats *yb_stats, const int yb_max_pools) {
    for (int i = YB_TXN_POOL_STATS_START_INDEX; i < yb_max_pools; i++) {
        yb_stats[i].active_clients = 0;
		yb_stats[i].queued_clients = 0;
		yb_stats[i].waiting_clients = 0;
		yb_stats[i].active_servers = 0;
		yb_stats[i].idle_servers = 0;
		yb_stats[i].sticky_connections = 0;
		yb_stats[i].query_rate = 0;
		yb_stats[i].transaction_rate = 0;
		yb_stats[i].avg_wait_time_ns = 0;
    }
}

void od_router_init(od_router_t *router, od_global_t *global)
{
	pthread_mutex_init(&router->lock, NULL);
	od_rules_init(&router->rules);
	od_list_init(&router->servers);
	od_route_pool_init(&router->route_pool);
	router->clients = 0;
	router->clients_routing = 0;
	router->servers_routing = 0;

	router->global = global;

	router->router_err_logger = od_err_logger_create_default();
}

static inline int od_router_immed_close_server_cb(od_server_t *server,
						  void **argv)
{
	od_route_t *route = server->route;
	/* remove server for server pool */
	od_pg_server_pool_set(&route->server_pool, server, OD_SERVER_UNDEF);

	server->route = NULL;
	od_backend_close_connection(server);
	od_backend_close(server);

	return 0;
}

static inline int od_router_immed_close_cb(od_route_t *route, void **argv)
{
	od_route_lock(route);
	od_server_pool_foreach(&route->server_pool, OD_SERVER_IDLE,
			       od_router_immed_close_server_cb, argv);
	od_route_unlock(route);
	return 0;
}

void od_router_free(od_router_t *router)
{
	od_list_t *i;
	od_list_t *n;
	od_list_foreach_safe(&router->servers, i, n)
	{
		od_system_server_t *server;
		server = od_container_of(i, od_system_server_t, link);
		od_system_server_free(server);
	}

	od_router_foreach(router, od_router_immed_close_cb, NULL);
	od_route_pool_free(&router->route_pool);
	od_rules_free(&router->rules);
	pthread_mutex_destroy(&router->lock);
	od_err_logger_free(router->router_err_logger);
}

inline int od_router_foreach(od_router_t *router, od_route_pool_cb_t callback,
			     void **argv)
{
	od_router_lock(router);
	int rc;
	rc = od_route_pool_foreach(&router->route_pool, callback, argv);
	od_router_unlock(router);
	return rc;
}

static inline int od_router_reload_cb(od_route_t *route, void **argv)
{
	(void)argv;

	if (!route->rule->obsolete) {
		return 0;
	}

	od_route_lock(route);
	od_route_reload_pool(route);
	od_route_unlock(route);
	return 1;
}

static inline int od_drop_obsolete_rule_connections_cb(od_route_t *route,
						       void **argv)
{
	od_list_t *i;
	od_rule_t *rule = route->rule;
	od_list_t *obsolete_rules = argv[0];
	od_list_foreach(obsolete_rules, i)
	{
		od_rule_key_t *obsolete_rule;
		obsolete_rule = od_container_of(i, od_rule_key_t, link);
		assert(rule);
		assert(obsolete_rule);
		if (strcmp(rule->user_name, obsolete_rule->usr_name) == 0 &&
		    strcmp(rule->db_name, obsolete_rule->db_name) == 0) {
			od_route_kill_client_pool(route);
			return 0;
		}
	}
	return 0;
}

int od_router_reconfigure(od_router_t *router, od_rules_t *rules)
{
	od_instance_t *instance = router->global->instance;
	od_router_lock(router);

	int updates;
	od_list_t added;
	od_list_t deleted;
	od_list_t to_drop;
	od_list_init(&added);
	od_list_init(&deleted);
	od_list_init(&to_drop);

	updates = od_rules_merge(&router->rules, rules, &added, &deleted,
				 &to_drop);

	if (updates > 0) {
		od_extention_t *extentions = router->global->extentions;
		od_list_t *i;
		od_list_t *j;
		od_module_t *modules = extentions->modules;

		od_list_foreach(&added, i)
		{
			od_rule_key_t *rk;
			rk = od_container_of(i, od_rule_key_t, link);
			od_log(&instance->logger, "reload config", NULL, NULL,
			       "added rule: %s %s", rk->usr_name, rk->db_name);
		}

		od_list_foreach(&deleted, i)
		{
			od_rule_key_t *rk;
			rk = od_container_of(i, od_rule_key_t, link);
			od_log(&instance->logger, "reload config", NULL, NULL,
			       "deleted rule: %s %s", rk->usr_name,
			       rk->db_name);
		}

		{
			void *argv[] = { &to_drop };
			od_route_pool_foreach(
				&router->route_pool,
				od_drop_obsolete_rule_connections_cb, argv);
		}

		/* reloadcallback */
		od_list_foreach(&modules->link, i)
		{
			od_module_t *module;
			module = od_container_of(i, od_module_t, link);
			if (module->od_config_reload_cb == NULL)
				continue;

			if (module->od_config_reload_cb(&added, &deleted) ==
			    OD_MODULE_CB_FAIL_RETCODE) {
				break;
			}
		}

		od_list_foreach_safe(&added, i, j)
		{
			od_rule_key_t *rk;
			rk = od_container_of(i, od_rule_key_t, link);
			od_rule_key_free(rk);
		}

		od_list_foreach_safe(&deleted, i, j)
		{
			od_rule_key_t *rk;
			rk = od_container_of(i, od_rule_key_t, link);
			od_rule_key_free(rk);
		}

		od_list_foreach_safe(&to_drop, i, j)
		{
			od_rule_key_t *rk;
			rk = od_container_of(i, od_rule_key_t, link);
			od_rule_key_free(rk);
		}

		od_route_pool_foreach(&router->route_pool, od_router_reload_cb,
				      NULL);
	}

	od_router_unlock(router);
	return updates;
}

static inline int od_router_expire_server_cb(od_server_t *server, void **argv)
{
	od_route_t *route = server->route;
	od_list_t *expire_list = argv[0];
	int *count = argv[1];

	/* remove server for server pool */
	od_pg_server_pool_set(&route->server_pool, server, OD_SERVER_UNDEF);

	od_list_append(expire_list, &server->link);
	(*count)++;

	return 0;
}

static inline int od_router_expire_server_tick_cb(od_server_t *server,
						  void **argv)
{
	od_route_t *route = server->route;
	od_list_t *expire_list = argv[0];
	int *count = argv[1];
	uint64_t *now_us = argv[2];
	int jitter = route->rule->yb_jitter_time == 0 ? 0 : rand() % route->rule->yb_jitter_time;

	uint64_t lifetime = route->rule->server_lifetime_us;
	uint64_t server_life = *now_us - server->init_time_us;

	/* Database or user is dropped */
	if (yb_is_route_invalid(route))
		goto expire_server;

	if (!server->offline) {
		/* Expire server if is marked for closing */
		if (server->marked_for_close)
			goto expire_server;

		/* advance idle time for 1 sec */
		if (server_life < lifetime &&
		    server->idle_time < route->rule->pool->ttl + jitter) {
			/* YB NOTE: the server is within the pool's ttl. */
			server->idle_time++;
			return 0;
		}

		/* YB NOTE: the server has crossed the pool's ttl. */
		server->idle_time++;

		/*
		 * Do not expire more servers than we are allowed to connect at one time
		 * This avoids need to re-launch lot of connections together
		 */
		if (*count > route->rule->storage->server_max_routing)
			return 0;

		/*
		 * Dont expire the server connection if the min size is reached and this
		 * server hasn't been idle for >= 3 * ttl.
		 */
		if ((od_server_pool_total(&route->server_pool) <=
		     route->rule->min_pool_size) &&
		    server_life < lifetime &&
		    server->idle_time < 3 * route->rule->pool->ttl)
			return 0;
	} // else remove server because we are forced to

expire_server:
	/* remove server for server pool */
	od_pg_server_pool_set(&route->server_pool, server, OD_SERVER_UNDEF);

	/* add to expire list */
	od_list_append(expire_list, &server->link);
	(*count)++;

	return 0;
}

static inline int od_router_expire_cb(od_route_t *route, void **argv)
{
	od_route_lock(route);

	/* expire by config obsoletion */
	if (route->rule->obsolete &&
	    !od_client_pool_total(&route->client_pool)) {
		od_server_pool_foreach(&route->server_pool, OD_SERVER_IDLE,
				       od_router_expire_server_cb, argv);

		od_route_unlock(route);
		return 0;
	}

	if (!route->rule->pool->ttl) {
		od_route_unlock(route);
		return 0;
	}

	od_server_pool_foreach(&route->server_pool, OD_SERVER_IDLE,
			       od_router_expire_server_tick_cb, argv);

	od_route_unlock(route);
	return 0;
}

int od_router_expire(od_router_t *router, od_list_t *expire_list)
{
	int count = 0;
	uint64_t now_us = machine_time_us();
	void *argv[] = { expire_list, &count, &now_us };
	od_router_foreach(router, od_router_expire_cb, argv);
	return count;
}

/* Mark the route as inactive for the provided db oid or user oid */
static inline int yb_mark_route_inactive(od_route_t *route, void **argv)
{
	int *db_oid = argv[0];
	int *user_oid = argv[1];

	od_route_lock(route);

	if (*db_oid != -1 && *db_oid == route->id.yb_db_oid) {
		route->status = YB_ROUTE_INACTIVE;
		od_route_unlock(route);
		return 0;
	}

	if (*user_oid != -1 && *user_oid == route->id.yb_user_oid) {
		route->status = YB_ROUTE_INACTIVE;
		od_route_unlock(route);
		return 0;
	}

	od_route_unlock(route);

	return 0;
}

void yb_mark_routes_inactive(od_router_t *router, int db_oid, int user_oid)
{
	void *argv[] = { &db_oid, &user_oid };
	od_router_foreach(router, yb_mark_route_inactive, argv);
}

static inline int od_router_gc_cb(od_route_t *route, void **argv)
{
	od_route_pool_t *pool = argv[0];
	od_instance_t *instance = argv[1];
	int index = route->id.yb_stats_index;
	od_route_lock(route);

	if (route->status == YB_ROUTE_INACTIVE)
		goto clean;

	if (od_server_pool_total(&route->server_pool) > 0 ||
	    od_client_pool_total(&route->client_pool) > 0)
		goto done;

	if (!od_route_is_dynamic(route) && !route->rule->obsolete)
		goto done;

clean:
	/* remove route from route pool */
	assert(pool->count > 0);
	pool->count--;
	od_list_unlink(&route->link);

	if (index >= YB_TXN_POOL_STATS_START_INDEX) {
		instance->yb_stats[index].database_oid = -1;
		instance->yb_stats[index].user_oid = -1;
	}

	/* unref route rule */
	od_rules_unref(route->rule);
	od_route_unlock(route);

	/* free route object */
	od_route_free(route);
	return 0;
done:
	od_route_unlock(route);
	return 0;
}

void od_router_gc(od_router_t *router)
{
	void *argv[] = { &router->route_pool, router->global->instance };
	od_router_foreach(router, od_router_gc_cb, argv);
}

void od_router_stat(od_router_t *router, uint64_t prev_time_us,
#ifdef PROM_FOUND
		    od_prom_metrics_t *metrics,
#endif
		    od_route_pool_stat_cb_t callback, void **argv)
{
	od_router_lock(router);
	od_instance_t *instance = argv[0];
	clear_stats(instance->yb_stats,  instance->config.yb_max_pools);
	/* 
	 * TOOD: Fix data race condition. Once all the stats are cleared, if read before updating
	 * will return stale (NULL) values.
	 */
	od_route_pool_stat(&router->route_pool, prev_time_us,
#ifdef PROM_FOUND
			   metrics,
#endif
			   callback, argv);
	od_router_unlock(router);
}

static inline int yb_update_name(od_route_t *route, void **argv)
{
	int *obj_type = argv[0];
	int *oid = argv[1];
	const char *name = argv[2];
	od_route_lock(route);
	if (*obj_type == YB_DATABASE && route->id.yb_db_oid == *oid) {
		strcpy(route->yb_database_name, name);
		route->yb_database_name_len = strlen(name);
	} else if (*obj_type == YB_USER && route->id.yb_user_oid == *oid) {
		strcpy(route->yb_user_name, name);
		route->yb_user_name_len = strlen(name);
	}
	od_route_unlock(route);
	return 0;
}

/*
* Update the name of the database or user in all the routes created using
* given database_oid or user_oid.
*/
void yb_route_pool_update_name(od_router_t *router, int obj_type, int oid,
				  char *name)
{
	void *argv[] = { &obj_type, &oid, name };
	/*
	 * Restrict from using od_router_foreach as it acquires lock on router object.
	 * This function is called from od_router_route which already acquires lock
	 * on router object.
	 */
	od_route_pool_foreach(&router->route_pool, yb_update_name, argv);
}

od_router_status_t od_router_route(od_router_t *router, od_client_t *client)
{
	kiwi_be_startup_t *startup = &client->startup;
	od_instance_t *instance = router->global->instance;
	bool is_auth_backend = client->yb_is_authenticating;

	/* match route */
	assert(startup->database.value_len);
	assert(startup->user.value_len);
	assert(client->yb_db_oid >= 0);
	assert(client->yb_user_oid >= 0);

	/*
	 * yb_db_oid and yb_user_oid for external clients can't be equal to 
	 * YB_CTRL_CONN_OID.
	 */
	if (client->type != OD_POOL_CLIENT_INTERNAL)
	{
		assert(client->yb_db_oid > YB_CTRL_CONN_OID);
		assert(client->yb_user_oid > YB_CTRL_CONN_OID);
	}

	od_router_lock(router);

	/* match latest version of route rule */
	od_rule_t *rule;
	rule = od_rules_forward(&router->rules, startup->database.value,
				startup->user.value);
	if (rule == NULL) {
		od_router_unlock(router);
		return OD_ROUTER_ERROR_NOT_FOUND;
	}
	od_debug(&instance->logger, "routing", NULL, NULL,
		 "matched rule: %s %s with %s routing type", rule->db_name,
		 rule->user_name, rule->pool->routing_type);
	if (!od_rule_matches_client(rule->pool, client->type)) {
		// emulate not found error
		od_router_unlock(router);
		return OD_ROUTER_ERROR_NOT_FOUND;
	}

	/* force settings required by route */
	od_route_id_t id = { .yb_db_oid = client->yb_db_oid,
			     .yb_user_oid = client->yb_user_oid,
			     .physical_rep = false,
			     .logical_rep = false,
			     .yb_stats_index = -1 };

	if (startup->replication.value_len != 0) {
		if (strcmp(startup->replication.value, "database") == 0)
			id.logical_rep = true;
		else if (!parse_bool(startup->replication.value,
				     &id.physical_rep)) {
			od_router_unlock(router);
			return OD_ROUTER_ERROR_REPLICATION;
		}
	}
#ifdef LDAP_FOUND
	if (rule->ldap_storage_credentials_attr) {
		od_ldap_server_t *ldap_server = NULL;
		ldap_server =
			od_ldap_server_pull(&instance->logger, rule, false);
		if (ldap_server == NULL) {
			od_debug(&instance->logger, "routing", client, NULL,
				 "failed to get ldap connection");
			od_router_unlock(router);
			return OD_ROUTER_ERROR_NOT_FOUND;
		}
		int ldap_rc = od_ldap_server_prepare(&instance->logger,
						     ldap_server, rule, client);
		switch (ldap_rc) {
		case OK_RESPONSE: {
			od_ldap_endpoint_lock(rule->ldap_endpoint);
			ldap_server->idle_timestamp = (int)time(NULL);
			od_ldap_server_pool_set(
				rule->ldap_endpoint->ldap_search_pool,
				ldap_server, OD_SERVER_IDLE);
			od_ldap_endpoint_unlock(rule->ldap_endpoint);

			/* invalid after switching to OID based pool design */
#ifndef YB_SUPPORT_FOUND
			/* YB: commenting out code to avoid compiler errors */
			// id.user = client->ldap_storage_username;
			// id.user_len = client->ldap_storage_username_len + 1;
			rule->storage_user = client->ldap_storage_username;
			rule->storage_user_len =
				client->ldap_storage_username_len;
			rule->storage_password = client->ldap_storage_password;
			rule->storage_password_len =
				client->ldap_storage_password_len;
			// od_debug(&instance->logger, "routing", client, NULL,
			//   "route->id.user changed to %s", id.user);
#endif
			break;
		}
		case LDAP_INSUFFICIENT_ACCESS: {
			od_ldap_endpoint_lock(rule->ldap_endpoint);
			ldap_server->idle_timestamp = (int)time(NULL);
			od_ldap_server_pool_set(
				rule->ldap_endpoint->ldap_search_pool,
				ldap_server, OD_SERVER_IDLE);
			od_ldap_endpoint_unlock(rule->ldap_endpoint);
			od_router_unlock(router);
			return OD_ROUTER_INSUFFICIENT_ACCESS;
		}
		default: {
			od_debug(&instance->logger, "routing", client, NULL,
				 "closing bad ldap connection, need relogin");
			od_ldap_endpoint_lock(rule->ldap_endpoint);
			od_ldap_server_pool_set(
				rule->ldap_endpoint->ldap_search_pool,
				ldap_server, OD_SERVER_UNDEF);
			od_ldap_endpoint_unlock(rule->ldap_endpoint);
			od_ldap_server_free(ldap_server);
			od_router_unlock(router);
			return OD_ROUTER_ERROR_NOT_FOUND;
		}
		}
	}
#endif
	/* match or create dynamic route */
	od_route_t *route;
	route = od_route_pool_match(&router->route_pool, &id, rule);
	if (route == NULL) {
		if (router->route_pool.count >= instance->config.yb_max_pools) {
			od_router_unlock(router);
			od_error(
				&instance->logger, "routing", client, NULL,
				"Limit reached to create maximum number of pools");
			return OD_ROUTER_ERROR;
		}
		route = od_route_pool_new(&router->route_pool, &id, rule);
		if (route == NULL) {
			od_router_unlock(router);
			return OD_ROUTER_ERROR;
		}
		od_log(&instance->logger, "routing", NULL, NULL,
		       "new route: %s %s", startup->database.value, startup->user.value);
		if (!is_auth_backend &&
			(id.yb_db_oid == YB_CTRL_CONN_OID || id.yb_user_oid == YB_CTRL_CONN_OID)) {

				// Set the user and database of control connection pool to "yugabyte"
				// and "yugabyte" respectively for purpose of creating backends for
				// auth pass through authentication in control connection pool.
				strcpy(route->yb_database_name, YB_CTRL_CONN_DB_NAME);
				route->yb_database_name_len = strlen(YB_CTRL_CONN_DB_NAME);

				strcpy(route->yb_user_name, YB_CTRL_CONN_USER_NAME);
				route->yb_user_name_len = strlen(YB_CTRL_CONN_USER_NAME);
		}
		else {
			strcpy(route->yb_database_name, startup->database.value);
			route->yb_database_name_len = strlen(startup->database.value);

			strcpy(route->yb_user_name, startup->user.value);
			route->yb_user_name_len = strlen(startup->user.value);
		}
	}
	else
	{
		if (route->id.yb_db_oid > YB_CTRL_CONN_OID &&
			strcmp(route->yb_database_name, startup->database.value) != 0) {
			/*
			 * The database has been renamed.
			 * We need to update the name in all the routes created using given
			 * database_oid.
			 */
			yb_route_pool_update_name(router, YB_DATABASE, client->yb_db_oid,
						  startup->database.value);
		}

		if (route->id.yb_user_oid > YB_CTRL_CONN_OID &&
			strcmp(route->yb_user_name, startup->user.value) != 0) {
			/*
			 * The user has been renamed.
			 * We need to update the name in all the routes created using given
			 * user_oid.
			 */
			yb_route_pool_update_name(router, YB_USER, client->yb_user_oid,
						  startup->user.value);
		}
	}

	od_rules_ref(rule);

	od_route_lock(route);

	/* increase counter of new tot tcp connections */
	++route->tcp_connections;

	/* ensure route client_max limit */
	if (rule->client_max_set &&
	    od_client_pool_total(&route->client_pool) >= rule->client_max) {
		od_rules_unref(rule);
		od_route_unlock(route);
		od_router_unlock(router);

		/*
		 * we assign client's rule to pass connection limit to the place where
		 * error is handled Client does not actually belong to the pool
		 */
		client->rule = rule;

		od_router_status_t ret = OD_ROUTER_ERROR_LIMIT_ROUTE;
		if (route->extra_logging_enabled) {
			od_error_logger_store_err(route->err_logger, ret);
		}
		return ret;
	}
	od_router_unlock(router);

	/* add client to route client pool */
	od_client_pool_set(&route->client_pool, client, OD_CLIENT_PENDING);
	client->rule = rule;
	client->route = route;

	od_route_unlock(route);
	return OD_ROUTER_OK;
}

void od_router_unroute(od_router_t *router, od_client_t *client)
{
	(void)router;
	/* detach client from route */
	assert(client->route);
	assert(client->server == NULL);

	od_route_t *route = client->route;
	od_route_lock(route);
	od_client_pool_set(&route->client_pool, client, OD_CLIENT_UNDEF);
	client->route = NULL;
	od_route_unlock(route);
}

bool od_should_not_spun_connection_yet(int connections_in_pool, int pool_size,
				       int currently_routing, int max_routing)
{
	if (pool_size == 0)
		return currently_routing >= max_routing;
	/*
	 * This routine controls ramping of server connections.
	 * When we have a lot of server connections we try to avoid opening new
	 * in parallel. Meanwhile when we have no server connections we go at
	 * maximum configured parallelism.
	 *
	 * This equation means that we gradualy reduce parallelism until we reach
	 * half of possible connections in the pool.
	 */
	max_routing =
		max_routing * (pool_size - connections_in_pool * 2) / pool_size;
	if (max_routing <= 0)
		max_routing = 1;
	return currently_routing >= max_routing;
}

#define MAX_BUZYLOOP_RETRY 10

/*
 * Count the number of active routes i.e. routes with at least one in_use or
 * pending client.
 *
 * IMPORTANT: The caller must not hold any locks on any of the routes.
 */
static uint32_t yb_count_all_active_routes(od_router_t *router, od_route_t *current_route) {
	od_router_lock(router);

	od_route_pool_t *pool = &router->route_pool;
	od_list_t *i;
	uint32_t active_routes = 0;
	od_list_foreach(&pool->list, i)
	{
		od_route_t *route;
		route = od_container_of(i, od_route_t, link);

		if (yb_is_route_invalid(route) ||
			(route->id.logical_rep))
			continue;

		/*
		 * The current route must be counted as an active route since we have an
		 * active request on it.
		 */
		if (od_route_id_compare(&route->id, &current_route->id)) {
			active_routes++;
			continue;
		}

		od_route_lock(route);
		active_routes +=
			(od_server_pool_total(&route->server_pool) +
				 od_client_pool_total(&route->client_pool) >
			 0) ? 1 : 0;
		od_route_unlock(route);
	}

	od_router_unlock(router);
	return active_routes;
}

/*
 * Calculate the number of in_use backends (aka physical connection) across all
 * routes.
 *
 * IMPORTANT: The caller must not hold any locks on any of the routes.
 */
static uint32_t yb_calculate_all_in_use_backends(od_router_t *router) {
	od_router_lock(router);

	od_route_pool_t *pool = &router->route_pool;
	od_list_t *i;
	uint32_t total_in_use_backends = 0;
	od_list_foreach(&pool->list, i)
	{
		od_route_t *route;
		route = od_container_of(i, od_route_t, link);

		if (yb_is_route_invalid(route) ||
			(route->id.logical_rep))
			continue;

		od_route_lock(route);
		total_in_use_backends += od_server_pool_total(&route->server_pool);
		od_route_unlock(route);
	}

	od_router_unlock(router);
	return total_in_use_backends;
}

/*
 * Return an idle server to close from a route different from the current route.
 * The route must be exceeding the per_route_quota.
 * Returns NULL if no such route is found.
 *
 * IMPORTANT: Must not hold a lock on any of the route before calling this
 * function to avoid deadlocks.
 *
 * IMPORTANT: If the route is found, the caller must unlock the route after
 * closing the idle server.
 */
static od_server_t *yb_get_idle_server_to_close(od_router_t *router,
					 od_route_t *current_route,
					 uint32_t per_route_quota)
{
	od_server_t *idle_server = NULL;
	od_router_lock(router);

	od_route_pool_t *pool = &router->route_pool;
	od_list_t *i;
	od_list_foreach(&pool->list, i)
	{
		od_route_t *route;
		route = od_container_of(i, od_route_t, link);

		if (od_route_id_compare(&route->id, &current_route->id) ||
		    yb_is_route_invalid(route))
			continue;

		od_route_lock(route);
		uint32_t route_yb_in_use =
			od_server_pool_total(&route->server_pool);
		if (route_yb_in_use > per_route_quota) {
			idle_server = od_pg_server_pool_next(&route->server_pool,
				OD_SERVER_IDLE);

			/*
			 * Note the lack of od_route_unlock(route) in this case.
			 * The caller is responsible for unlocking the route once it is done
			 * shutting down this server.
			 */
			if (idle_server) {
				od_router_unlock(router);
				return idle_server;
			}
		}
		od_route_unlock(route);
	}

	od_router_unlock(router);
	return NULL;
}

/*
 * od_router_attach is a function used to attach a client object to a server object.
 * client_for_router represents the internal client used for
 * route matching and server attachment. On the other hand,
 * external_client represents the actual client connection
 * currently being processed by the worker thread.
 * Since it may take some time for the server (limited by pool size)
 * to become available, read events from external_client are unsubscribed
 * to prevent potential hangs. It's important to note that
 * during control connection creation, external_client and client_for_router
 * will be different, while in all other scenarios, they will be the same.
 */
od_router_status_t od_router_attach(od_router_t *router,
				    od_client_t *client_for_router,
				    bool wait_for_idle,
				    od_client_t *external_client)
{
	(void)router;
	od_route_t *route = client_for_router->route;
	assert(route != NULL);
	od_instance_t *instance = router->global->instance;

	struct timespec start_time, end_time;
	long long time_taken_to_attach_server_ns;

	clock_gettime(CLOCK_MONOTONIC, &start_time);

	od_route_lock(route);

	/* enqueue client (pending -> queue) */
	od_client_pool_set(&route->client_pool, client_for_router,
			   OD_CLIENT_QUEUE);

	/* get client server from route server pool */
	bool restart_read = false;
	od_server_t *server;
	int busyloop_sleep = 0;
	int busyloop_retry = 0;

	const char *is_warmup_needed_flag = getenv("YB_YSQL_CONN_MGR_DOWARMUP_ALL_POOLS_MODE");
	bool is_warmup_needed = false;
	bool random_allot = false;

	is_warmup_needed = is_warmup_needed_flag != NULL && strcmp(is_warmup_needed_flag, "none") != 0;
	random_allot = is_warmup_needed && strcmp(is_warmup_needed_flag, "random") == 0;

	od_debug(&instance->logger, "router-attach", client_for_router, NULL,
		 "client_for_router logical client version = %d",
		 client_for_router->logical_client_version);

	for (;;) {
		if (version_matching) {

			server = yb_od_server_pool_idle_version_matching(
				&route->server_pool,
				client_for_router->logical_client_version,
				version_matching_connect_higher_version);

			if (server)
				goto attach;
			else if (route->max_logical_client_version >
					 client_for_router->logical_client_version &&
				 !version_matching_connect_higher_version) {
				od_debug(
					&instance->logger, "router-attach",
					client_for_router, NULL,
					"old logical client, need to disconect, "
					"max_logical_client_version of pool = %d, and version of client = %d",
					route->max_logical_client_version,
					client_for_router->logical_client_version);
				od_route_unlock(route);
				return OD_ROUTER_ERROR;
			}
		}

		else if (is_warmup_needed) {
			if (random_allot)
				server = yb_od_server_pool_idle_random(&route->server_pool);
			else /* round_robin allotment */
				server = yb_od_server_pool_idle_last(&route->server_pool);

			if (server &&
			    (od_server_pool_total(&route->server_pool) >=
			     route->rule->min_pool_size))
				goto attach;
		}
		else {
			server = od_pg_server_pool_next(&route->server_pool,
						OD_SERVER_IDLE);
			if (server)
				goto attach;
		}

		if (wait_for_idle) {
			/* special case, when we are interested only in an idle connection
			 * and do not want to start a new one */
			if (route->server_pool.count_active == 0) {
				od_route_unlock(route);
				return OD_ROUTER_ERROR_TIMEDOUT;
			}
		} else {
			/* Maybe start new connection, if pool_size is zero */
			/* Maybe start new connection, if we still have capacity for it */
			int connections_in_pool =
				od_server_pool_total(&route->server_pool);
			int pool_size = route->rule->pool->size;
			uint32_t currently_routing =
				od_atomic_u32_of(&router->servers_routing);
			uint32_t max_routing = (uint32_t)route->rule->storage
						       ->server_max_routing;

			bool yb_is_slot_available = false;
			if (instance->config.yb_enable_multi_route_pool) {
				od_route_unlock(route);
				uint32_t yb_total_acquired_slots =
					yb_calculate_all_in_use_backends(router);
				od_route_lock(route);
				yb_is_slot_available =
					yb_total_acquired_slots < (uint32_t) instance->config.yb_ysql_max_connections;
			} else {
				yb_is_slot_available = pool_size == 0 ||
					connections_in_pool < pool_size;
			}

			// For replication connection, PG has a different pool from ysql connections.
			// Replication connections are not detached after txn is committed similar to sticky
			// connections.They are expired after logical replication connection is closed.
			// Therefore always allow it to spin up a new physical connection. And let
			// postgres take care of the limits on number of backend connections.
			if (route->id.logical_rep) {
				yb_is_slot_available = true;
			}

			if (yb_is_slot_available) {
				if (od_should_not_spun_connection_yet(
					    connections_in_pool,
						// TODO(#25284): The existing control of ramping up of connections utilizes
						// the pool_size which isn't applicable in the multi route algorithm.
						// Consider an alternative.
						(instance->config.yb_enable_multi_route_pool) ? 0 : pool_size,
					    (int)currently_routing,
					    (int)max_routing)) {
					// concurrent server connection in progress.
					od_route_unlock(route);
					machine_sleep(busyloop_sleep);
					busyloop_retry++;
					// TODO: support this opt in configure file
					if (busyloop_retry > MAX_BUZYLOOP_RETRY)
						busyloop_sleep = 1;
					od_route_lock(route);
					continue;
				} else {
					// We are allowed to spun new server connection
					break;
				}
			} else if (instance->config.yb_enable_multi_route_pool) {
				// We have reached ysql_max_connections limit. We need to close
				// a physical connection before we can start a new one.
				// Find a route which has more entries than per_route_quota and
				// get a machine from it if possible. If found, we close that
				// machine, otherwise, we add ourselves to the pending count
				// and wait.

				// We need to unlock the route before we can call
				// yb_count_all_active_routes and yb_get_idle_server_to_close
				// functions.
				od_route_unlock(route);
				uint32_t num_active_routes =
					yb_count_all_active_routes(router, route);
				uint32_t per_route_quota =
					(uint32_t)instance->config.yb_ysql_max_connections /
						num_active_routes;
				od_server_t *idle_server =
					yb_get_idle_server_to_close(router, route, per_route_quota);
				if (idle_server) {
					// Close the server and make space for ourselves.
					od_route_t *idle_route = idle_server->route;
					od_pg_server_pool_set(&idle_route->server_pool,
							      idle_server,
							      OD_SERVER_UNDEF);
					idle_server->route = NULL;
					od_backend_close_connection(idle_server);
					od_backend_close(idle_server);
					od_route_unlock(idle_route);

					// We are allowed to spun new server connection now that we
					// have made space for ourselves.
					od_route_lock(route);
					break;
				}

				// No available idle servers. We have to wait.
				od_route_lock(route);
			}
		}

		/*
		 * unsubscribe from pending client read events during the time we wait
		 * for an available server
		 */
		restart_read = restart_read ||
			       (bool)od_io_read_active(&external_client->io);
		od_route_unlock(route);

		int rc = od_io_read_stop(&external_client->io);
		if (rc == -1)
			return OD_ROUTER_ERROR;

		/*
		 * Wait wakeup condition for pool_timeout milliseconds.
		 *
		 * The condition triggered when a server connection
		 * put into idle state by DETACH events.
		 */
		uint32_t timeout = route->rule->pool->timeout;
		if (timeout == 0)
			timeout = UINT32_MAX;
		rc = od_route_wait(route, timeout);
		if (rc == -1)
			return OD_ROUTER_ERROR_TIMEDOUT;

		od_route_lock(route);
	}

	/* create new server object */
	bool created_atleast_one = false;
	while (is_warmup_needed &&
		  (od_server_pool_total(&route->server_pool) < route->rule->min_pool_size))
	{
		server = od_server_allocate(
		route->rule->pool->reserve_prepared_statement);
		if (server == NULL)
			return OD_ROUTER_ERROR;
		od_id_generate(&server->id, "s");
		server->global = client_for_router->global;
		server->route = route;
		server->client = NULL;
		od_pg_server_pool_set(&route->server_pool, server,
						OD_SERVER_IDLE);
		created_atleast_one = true;
	}

	/*
	 * If we created a server, then hold on to the lock so no other client can
	 * acquire this server from the server pool
	*/
	if (created_atleast_one)
		goto attach;

	/* YB: Unlock the route since we don't need the lock when allocating server */
	od_route_unlock(route);

	server = od_server_allocate(
		route->rule->pool->reserve_prepared_statement);
	if (server == NULL)
		return OD_ROUTER_ERROR;
	od_id_generate(&server->id, "s");
	server->global = client_for_router->global;
	server->route = route;

	od_route_lock(route);

attach:
	od_pg_server_pool_set(&route->server_pool, server, OD_SERVER_ACTIVE);
	od_client_pool_set(&route->client_pool, client_for_router,
			   OD_CLIENT_ACTIVE);

	client_for_router->server = server;
	server->client = client_for_router;
	server->idle_time = 0;
	server->key_client = client_for_router->key;

	if (route->id.logical_rep) {
		/*
		 * Replication connections are never detached after txn is committed
		 * similar to sticky connections. Attach phase is called once for a
		 * replication connection. So only once the sticky count is incremented
		 * to reflect on stats. Control backends are however NOT marked as
		 * sticky, as they are either automatically closed after athentication
		 * (Auth Backend) or reset and reused for further authentication
		 * (Auth Passthrough).
		 */

		server->yb_replication_connection = true;
		if(!(route->rule->pool->routing == OD_RULE_POOL_INTERVAL)) {
			route->server_pool.yb_count_sticky++;
		}
	}

	od_route_unlock(route);

	/* attach server io to clients machine context */
	if (server->io.io) {
		od_io_attach(&server->io);
	}

	/* maybe restore read events subscription */
	if (restart_read)
		od_io_read_start(&external_client->io);

    // Record the end time
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    // Calculate the time taken in nanoseconds
    time_taken_to_attach_server_ns =
	    (end_time.tv_sec - start_time.tv_sec) * 1000000000LL +
	    (end_time.tv_nsec - start_time.tv_nsec);
    od_stat_t *stats = &route->stats;
    od_atomic_u64_add(&stats->wait_time, time_taken_to_attach_server_ns);

	/*
	 * YB: Instead of number of transactions, count number of route/attach 
	 * attempts for conn mgr's control backends instead as these backends
	 * do not do any transactions. This allows populating avg_wait_time_ns.
	 */
	if (yb_od_streq(CONTROL_CONN_USER, sizeof(CONTROL_CONN_USER),
			client_for_router->startup.user.value,
			client_for_router->startup.user.value_len) &&
	    yb_od_streq(CONTROL_CONN_DB, sizeof(CONTROL_CONN_DB),
			client_for_router->startup.database.value,
			client_for_router->startup.database.value_len)) {
		od_atomic_u64_inc(&stats->count_tx);
	}

	return OD_ROUTER_OK;
}

void yb_signal_all_routes(od_router_t *router, od_route_t *detached_route,
    bool enable_multi_route_pool)
{
    if (!enable_multi_route_pool) {
        if (detached_route == NULL) {
            return;
        }
        od_route_lock(detached_route);
        int signal = detached_route->client_pool.count_queue > 0;
        if (signal) {
            od_route_signal(detached_route);
        }
		od_route_unlock(detached_route);
        return;
    }

    // TODO (#28097): It can be CPU intensive for large number of routes. As every time there is a
    // detach all other pool will try to attach if they are waiting for an server to attach.
    od_router_lock(router);

    od_route_pool_t *pool = &router->route_pool;
    od_list_t *i;
    od_list_foreach(&pool->list, i)
    {
        od_route_t *route = od_container_of(i, od_route_t, link);

        od_route_lock(route);
        int signal = route->client_pool.count_queue > 0;
        if (signal) {
            od_route_signal(route);
        }
        od_route_unlock(route);
    }

    od_router_unlock(router);
}

void od_router_detach(od_router_t *router, od_client_t *client)
{
	(void)router;
	od_route_t *route = client->route;
	od_instance_t *instance = router->global->instance;
	assert(route != NULL);

	/* detach from current machine event loop */
	od_server_t *server = client->server;

	/* YB: Check that we actually have a valid connection with the server 
	 * before detaching. We can arrive here even when we don't have a 
	 * valid connection. For eg. when the external client times out waiting
	 * in the queue. 
	 */
	if (server->io.io != NULL)
		od_io_detach(&server->io);

	od_route_lock(route);

	client->server = NULL;
	server->client = NULL;
	/*
	 * When the server detaches after completing a transaction,
	 * some queries are issued during the reset phase, which causes the
	 * unnamed prepared statement plan cache to be dropped.
     * As a result, it is necessary to remove the saved state
	 * on Connection Manager.
	 */
	memset(&server->yb_unnamed_prep_stmt_client_id, 0,
	       sizeof(server->yb_unnamed_prep_stmt_client_id));
	/*
	 * Drop the server connection if:
	 * 	a. Server gets OFFLINE.
	 * 	b. Client connection had used a query which required the connection to be STICKY.
	 * 	   As of D26669, these queries are:
	 * 			a. Creating TEMP TABLES.
	 * 			b. Use of WITH HOLD CURSORS.
	 *  c. Client connection is a logical or physical replication connection
	 *     (but NOT a control connection).
	 *  d. It took too long to reset state on the server.
	 */
	if (od_likely(!server->offline) &&
		!server->yb_sticky_connection &&
		!server->reset_timeout) {
		od_instance_t *instance = server->global->instance;
		if ((route->id.physical_rep || route->id.logical_rep) &&
		    (route->rule->pool->routing != OD_RULE_POOL_INTERVAL)) {
			od_debug(&instance->logger, "expire-replication", NULL,
				 server, "closing replication connection");
			server->route = NULL;
			od_backend_close_connection(server);
			od_pg_server_pool_set(&route->server_pool, server,
					      OD_SERVER_UNDEF);
			od_backend_close(server);
		} else {
			od_pg_server_pool_set(&route->server_pool, server,
					      OD_SERVER_IDLE);
		}
	} else {
		od_instance_t *instance = server->global->instance;
		od_debug(&instance->logger, "expire", NULL, server,
			 "closing obsolete server connection");
		server->route = NULL;
		od_backend_close_connection(server);
		od_pg_server_pool_set(&route->server_pool, server,
				      OD_SERVER_UNDEF);
		od_backend_close(server);
	}
	od_client_pool_set(&route->client_pool, client, OD_CLIENT_PENDING);

	od_route_unlock(route);

	/* notify waiters */
	yb_signal_all_routes(router, route, instance->config.yb_enable_multi_route_pool);
}

void od_router_close(od_router_t *router, od_client_t *client)
{
	(void)router;
	od_route_t *route = client->route;
	assert(route != NULL);

	od_server_t *server = client->server;
	od_backend_close_connection(server);

	od_route_lock(route);

	od_client_pool_set(&route->client_pool, client, OD_CLIENT_PENDING);
	od_pg_server_pool_set(&route->server_pool, server, OD_SERVER_UNDEF);
	client->server = NULL;
	server->client = NULL;
	server->route = NULL;

	od_route_unlock(route);

	assert(server->io.io == NULL);
	od_server_free(server);
}

static inline int od_router_cancel_cmp(od_server_t *server, void **argv)
{
	/* check that server is attached and has corresponding cancellation key */
	return (server->client != NULL) &&
	       kiwi_key_cmp(&server->key_client, argv[0]);
}

static inline int od_router_cancel_cb(od_route_t *route, void **argv)
{
	od_route_lock(route);

	od_server_t *server;
	server = od_server_pool_foreach(&route->server_pool, OD_SERVER_ACTIVE,
					od_router_cancel_cmp, argv);
	if (server) {
		od_router_cancel_t *cancel = argv[1];
		cancel->id = server->id;
		cancel->key = server->key;
		cancel->storage = od_rules_storage_copy(route->rule->storage);
		od_route_unlock(route);
		if (cancel->storage == NULL)
			return -1;
		return 1;
	}

	od_route_unlock(route);
	return 0;
}

od_router_status_t od_router_cancel(od_router_t *router, kiwi_key_t *key,
				    od_router_cancel_t *cancel)
{
	/* match server by client forged key */
	void *argv[] = { key, cancel };
	int rc;
	rc = od_router_foreach(router, od_router_cancel_cb, argv);
	if (rc <= 0)
		return OD_ROUTER_ERROR_NOT_FOUND;
	return OD_ROUTER_OK;
}

static inline int od_router_kill_cb(od_route_t *route, void **argv)
{
	od_route_lock(route);
	od_route_kill_client(route, argv[0]);
	od_route_unlock(route);
	return 0;
}

void od_router_kill(od_router_t *router, od_id_t *id)
{
	void *argv[] = { id };
	od_router_foreach(router, od_router_kill_cb, argv);
}
