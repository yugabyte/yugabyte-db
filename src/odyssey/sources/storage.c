
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

od_storage_watchdog_t *od_storage_watchdog_allocate(od_global_t *global)
{
	od_storage_watchdog_t *watchdog;
	watchdog = malloc(sizeof(od_storage_watchdog_t));
	if (watchdog == NULL) {
		return NULL;
	}
	memset(watchdog, 0, sizeof(od_storage_watchdog_t));
	watchdog->check_retry = 10;
	watchdog->global = global;
	watchdog->online = 1;
	pthread_mutex_init(&watchdog->mu, NULL);

	return watchdog;
}

static inline int
od_storage_watchdog_online_status(od_storage_watchdog_t *watchdog)
{
	int ret;
	pthread_mutex_lock(&watchdog->mu);
	ret = watchdog->online;
	pthread_mutex_unlock(&watchdog->mu);
	return ret;
}

static inline int od_storage_watchdog_soft_exit(od_storage_watchdog_t *watchdog)
{
	pthread_mutex_lock(&watchdog->mu);
	watchdog->online = 0;
	pthread_mutex_unlock(&watchdog->mu);
	return OK_RESPONSE;
}

int od_storage_watchdog_free(od_storage_watchdog_t *watchdog)
{
	if (watchdog == NULL) {
		return NOT_OK_RESPONSE;
	}

	if (watchdog->query) {
		free(watchdog->query);
	}

	pthread_mutex_destroy(&watchdog->mu);

	free(watchdog);
	return OK_RESPONSE;
}

od_rule_storage_t *od_rules_storage_allocate(void)
{
	/* Allocate and force defaults */
	od_rule_storage_t *storage;
	storage = (od_rule_storage_t *)malloc(sizeof(*storage));
	if (storage == NULL)
		return NULL;
	memset(storage, 0, sizeof(*storage));
	storage->tls_opts = od_tls_opts_alloc();
	if (storage->tls_opts == NULL) {
		return NULL;
	}
	storage->target_session_attrs = OD_TARGET_SESSION_ATTRS_ANY;
	storage->rr_counter = 0;

	od_list_init(&storage->link);
	return storage;
}

void od_rules_storage_free(od_rule_storage_t *storage)
{
	if (storage->name)
		free(storage->name);
	if (storage->type)
		free(storage->type);
	if (storage->host)
		free(storage->host);

	if (storage->tls_opts) {
		od_tls_opts_free(storage->tls_opts);
	}

	if (storage->watchdog) {
		od_storage_watchdog_soft_exit(storage->watchdog);
	}

	if (storage->endpoints_count) {
		for (size_t i = 0; i < storage->endpoints_count; ++i) {
			free(storage->endpoints[i].host);
		}

		free(storage->endpoints);
	}

	od_list_unlink(&storage->link);
	free(storage);
}

od_rule_storage_t *od_rules_storage_copy(od_rule_storage_t *storage)
{
	od_rule_storage_t *copy;
	copy = od_rules_storage_allocate();
	if (copy == NULL)
		return NULL;
	copy->storage_type = storage->storage_type;
	copy->name = strdup(storage->name);
	copy->server_max_routing = storage->server_max_routing;
	if (copy->name == NULL)
		goto error;
	copy->type = strdup(storage->type);
	if (copy->type == NULL)
		goto error;
	if (storage->host) {
		copy->host = strdup(storage->host);
		if (copy->host == NULL)
			goto error;
	}
	copy->port = storage->port;
	copy->tls_opts->tls_mode = storage->tls_opts->tls_mode;
	if (storage->tls_opts->tls) {
		copy->tls_opts->tls = strdup(storage->tls_opts->tls);
		if (copy->tls_opts->tls == NULL)
			goto error;
	}
	if (storage->tls_opts->tls_ca_file) {
		copy->tls_opts->tls_ca_file =
			strdup(storage->tls_opts->tls_ca_file);
		if (copy->tls_opts->tls_ca_file == NULL)
			goto error;
	}
	if (storage->tls_opts->tls_key_file) {
		copy->tls_opts->tls_key_file =
			strdup(storage->tls_opts->tls_key_file);
		if (copy->tls_opts->tls_key_file == NULL)
			goto error;
	}
	if (storage->tls_opts->tls_cert_file) {
		copy->tls_opts->tls_cert_file =
			strdup(storage->tls_opts->tls_cert_file);
		if (copy->tls_opts->tls_cert_file == NULL)
			goto error;
	}
	if (storage->tls_opts->tls_protocols) {
		copy->tls_opts->tls_protocols =
			strdup(storage->tls_opts->tls_protocols);
		if (copy->tls_opts->tls_protocols == NULL)
			goto error;
	}

	if (storage->endpoints_count) {
		copy->endpoints_count = storage->endpoints_count;
		copy->endpoints = malloc(sizeof(od_storage_endpoint_t) *
					 copy->endpoints_count);
		if (copy->endpoints == NULL) {
			goto error;
		}

		for (size_t i = 0; i < copy->endpoints_count; ++i) {
			copy->endpoints[i].host =
				strdup(storage->endpoints[i].host);
			if (copy->endpoints[i].host == NULL) {
				goto error;
			}
			copy->endpoints[i].port = storage->endpoints[i].port;
		}
	}

	copy->target_session_attrs = storage->target_session_attrs;

	return copy;
error:
	od_rules_storage_free(copy);
	return NULL;
}

static inline int od_storage_watchdog_parse_lag_from_datarow(machine_msg_t *msg,
							     int *repl_lag)
{
	char *pos = (char *)machine_msg_data(msg) + 1;
	uint32_t pos_size = machine_msg_size(msg) - 1;

	/* size */
	uint32_t size;
	int rc;
	rc = kiwi_read32(&size, &pos, &pos_size);
	if (kiwi_unlikely(rc == -1))
		goto error;
	/* count */
	uint16_t count;
	rc = kiwi_read16(&count, &pos, &pos_size);

	if (kiwi_unlikely(rc == -1))
		goto error;

	if (count != 1)
		goto error;

	/* (not used) */
	uint32_t lag_len;
	rc = kiwi_read32(&lag_len, &pos, &pos_size);
	if (kiwi_unlikely(rc == -1)) {
		goto error;
	}

	*repl_lag = strtol(pos, NULL, 0);

	return OK_RESPONSE;
error:
	return NOT_OK_RESPONSE;
}

static inline int od_router_update_heartbeat_cb(od_route_t *route, void **argv)
{
	od_route_lock(route);
	route->last_heartbeat = *(int *)argv[0];
	od_route_unlock(route);
	return 0;
}

void od_storage_watchdog_watch(void *arg)
{
	od_storage_watchdog_t *watchdog = (od_storage_watchdog_t *)arg;
	od_global_t *global = watchdog->global;
	od_router_t *router = global->router;
	od_instance_t *instance = global->instance;

	od_debug(&instance->logger, "watchdog", NULL, NULL,
		 "start lag polling watchdog ");

	/* create internal auth client */
	od_client_t *watchdog_client;
	watchdog_client =
		od_client_allocate_internal(global, "storage-watchog");
	if (watchdog_client == NULL) {
		od_error(&instance->logger, "watchdog", NULL, NULL,
			 "route storage watchdog failed to allocate client");
		return;
	}

	watchdog_client->global = global;
	watchdog_client->type = OD_POOL_CLIENT_INTERNAL;
	od_id_generate(&watchdog_client->id, "a");

	/* set storage user and database */
	kiwi_var_set(&watchdog_client->startup.user, KIWI_VAR_UNDEF,
		     watchdog->route_usr, strlen(watchdog->route_usr) + 1);

	kiwi_var_set(&watchdog_client->startup.database, KIWI_VAR_UNDEF,
		     watchdog->route_db, strlen(watchdog->route_db) + 1);

	machine_msg_t *msg;

	int last_heartbeat = 0;
	int rc;
	/* route */
	od_router_status_t status;
	status = od_router_route(router, watchdog_client);
	od_debug(&instance->logger, "watchdog", watchdog_client, NULL,
		 "routing to internal wd route status: %s",
		 od_router_status_to_str(status));

	if (status != OD_ROUTER_OK) {
		od_error(&instance->logger, "watchdog", watchdog_client, NULL,
			 "route storage watchdog failed: %s",
			 od_router_status_to_str(status));
		return;
	}

	for (;;) {
		/* attach client to some route */
		status = od_router_attach(router, watchdog_client, false, watchdog_client);
		od_debug(&instance->logger, "watchdog", watchdog_client, NULL,
			 "attaching wd client to backend connection status: %s",
			 od_router_status_to_str(status));

		if (status != OD_ROUTER_OK) {
			/* 1 second soft interval */
			machine_sleep(1000);
			continue;
		}
		od_server_t *server;
		server = watchdog_client->server;
		od_debug(&instance->logger, "watchdog", watchdog_client, server,
			 "attached to server %s%.*s", server->id.id_prefix,
			 (int)sizeof(server->id.id), server->id.id);

		/* connect to server, if necessary */
		if (server->io.io == NULL) {
			rc = od_backend_connect(server, "watchdog", NULL,
						watchdog_client);
			if (rc == NOT_OK_RESPONSE) {
				od_debug(
					&instance->logger, "watchdog",
					watchdog_client, server,
					"backend connect failed, retry after 1 sec");
				od_router_close(router, watchdog_client);
				/* 1 second soft interval */
				machine_sleep(1000);
				continue;
			}
		}

		for (int retry = 0; retry < watchdog->check_retry; ++retry) {
			char *qry = watchdog->query;

			msg = od_query_do(server, "watchdog", qry, NULL);
			if (msg != NULL) {
				rc = od_storage_watchdog_parse_lag_from_datarow(
					msg, &last_heartbeat);
				machine_msg_free(msg);
				od_router_close(router, watchdog_client);
			} else {
				od_debug(
					&instance->logger, "watchdog",
					watchdog_client, server,
					"receive msg failed, closing backend connection");
				rc = NOT_OK_RESPONSE;
				od_router_close(router, watchdog_client);
				break;
			}

			if (rc == OK_RESPONSE) {
				od_debug(
					&instance->logger, "watchdog",
					watchdog_client, server,
					"send heartbeat arenda update to routes with value %d",
					last_heartbeat);
				void *argv[] = { &last_heartbeat };
				od_router_foreach(router,
						  od_router_update_heartbeat_cb,
						  argv);
				break;
			}
			// retry
		}

		/* detach and unroute */
		if (watchdog_client->server) {
			od_router_detach(router, watchdog_client);
		}

		if (!od_storage_watchdog_online_status(watchdog)) {
			od_debug(&instance->logger, "watchdog", watchdog_client,
				 NULL,
				 "deallocating obsolete storage watchdog");
			od_client_free(watchdog_client);
			od_storage_watchdog_free(watchdog);
			return;
		}

		/* 1 second soft interval */
		machine_sleep(1000);
	}
}
