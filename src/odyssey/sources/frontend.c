
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

bool version_matching = false;
bool version_matching_connect_higher_version = false;
/*
 * YB TODO(mkumar): GH#24724 Implement a solution to process the quries
 * in batches rather than only increasing the size of query array.
 */
int yb_max_query_size = OD_QRY_MAX_SZ;
int yb_wait_timeout = YB_DEFAULT_WAIT_TIMEOUT;

static inline void od_frontend_close(od_client_t *client)
{
	assert(client->route == NULL);
	assert(client->server == NULL);

	od_router_t *router = client->global->router;
	od_atomic_u32_dec(&router->clients);

	od_client_free_extended(client);
}

int od_frontend_info(od_client_t *client, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	machine_msg_t *msg;
	msg = od_frontend_info_msg(client, NULL, fmt, args);
	va_end(args);
	if (msg == NULL) {
		return -1;
	}
	return od_write(&client->io, &msg);
}

int od_frontend_error(od_client_t *client, char *code, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	machine_msg_t *msg;
	msg = od_frontend_error_msg(client, NULL, code, fmt, args);
	va_end(args);
	if (msg == NULL) {
		return -1;
	}
	return od_write(&client->io, &msg);
}

int od_frontend_fatal(od_client_t *client, char *code, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	machine_msg_t *msg;
	msg = od_frontend_fatal_msg(client, NULL, code, fmt, args);
	va_end(args);
	if (msg == NULL)
		return -1;
	return od_write(&client->io, &msg);
}

int od_frontend_fatal_forward(od_client_t *client, char *code, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	machine_msg_t *msg;
	msg = od_frontend_fatal_msg_forward(client, NULL, code, fmt, args);
	va_end(args);
	if (msg == NULL)
		return -1;
	return od_write(&client->io, &msg);
}

static inline int od_frontend_error_fwd(od_client_t *client)
{
	od_server_t *server = client->server;
	assert(server != NULL);
	assert(server->error_connect != NULL);
	kiwi_fe_error_t error;
	int rc;
	rc = kiwi_fe_read_error(machine_msg_data(server->error_connect),
				machine_msg_size(server->error_connect),
				&error);
	if (rc == -1)
		return -1;
	char text[512];
	int text_len;
	text_len =
		od_snprintf(text, sizeof(text), "odyssey: %s%.*s: %s",
			    client->id.id_prefix, (signed)sizeof(client->id.id),
			    client->id.id, error.message);
	int detail_len = error.detail ? strlen(error.detail) : 0;
	int hint_len = error.hint ? strlen(error.hint) : 0;

	machine_msg_t *msg;
	msg = kiwi_be_write_error_as(NULL, error.severity, error.code,
				     error.detail, detail_len, error.hint,
				     hint_len, text, text_len);
	if (msg == NULL)
		return -1;
	return od_write(&client->io, &msg);
}

static inline bool
od_frontend_error_is_too_many_connections(od_client_t *client)
{
	od_server_t *server = client->server;
	assert(server != NULL);
	if (server->error_connect == NULL)
		return false;
	kiwi_fe_error_t error;
	int rc;
	rc = kiwi_fe_read_error(machine_msg_data(server->error_connect),
				machine_msg_size(server->error_connect),
				&error);
	if (rc == -1)
		return false;
	return strcmp(error.code, KIWI_TOO_MANY_CONNECTIONS) == 0;
}

static inline bool
yb_frontend_error_is_db_does_not_exist(od_client_t *client)
{
	od_instance_t *instance = client->global->instance;
	od_server_t *server = client->server;
	assert(server != NULL);
	if (server->error_connect == NULL)
		return false;
	kiwi_fe_error_t error;

	int rc;
	rc = kiwi_fe_read_error(machine_msg_data(server->error_connect),
				machine_msg_size(server->error_connect),
				&error);
	if (rc == -1)
		return false;

	return strcmp(error.code, KIWI_UNDEFINED_DATABASE) == 0;
}

static inline bool
yb_frontend_error_is_role_does_not_exist(od_client_t *client)
{
	od_instance_t *instance = client->global->instance;
	od_server_t *server = client->server;
	assert(server != NULL);

	if (server->error_connect == NULL)
		return false;

	kiwi_fe_error_t error;
	int rc;

	rc = kiwi_fe_read_error(machine_msg_data(server->error_connect),
				machine_msg_size(server->error_connect),
				&error);
	if (rc == -1)
		return false;

	return strcmp(error.code, KIWI_INVALID_AUTHORIZATION_SPECIFICATION) == 0;
}

static int od_frontend_startup(od_client_t *client)
{
	od_instance_t *instance = client->global->instance;
	machine_msg_t *msg;

	for (uint32_t startup_attempt = 0;
	     startup_attempt < MAX_STARTUP_ATTEMPTS; startup_attempt++) {
		msg = od_read_startup(
			&client->io,
			client->config_listen->client_login_timeout);
		if (msg == NULL)
			goto error;

		int rc = kiwi_be_read_startup(
			machine_msg_data(msg), machine_msg_size(msg),
			&client->startup, &client->yb_startup_settings, true);
		machine_msg_free(msg);
		if (rc == -1)
			goto error;

		if (!client->startup.unsupported_request)
			break;
		/* not supported 'N' */
		msg = machine_msg_create(sizeof(uint8_t));
		if (msg == NULL)
			return -1;
		uint8_t *type = machine_msg_data(msg);
		*type = 'N';
		rc = od_write(&client->io, &msg);
		if (rc == -1) {
			od_error(&instance->logger,
				 "unsupported protocol (gssapi)", client, NULL,
				 "write error: %s", od_io_error(&client->io));
			return -1;
		}
		od_debug(&instance->logger, "unsupported protocol (gssapi)",
			 client, NULL, "ignoring");
	}

	/* client ssl request */
	int rc = od_tls_frontend_accept(client, &instance->logger,
					client->config_listen, client->tls);
	if (rc == -1)
		goto error;

	if (!client->startup.is_ssl_request) {
		rc = od_compression_frontend_setup(
			client, client->config_listen, &instance->logger);
		if (rc == -1)
			return -1;
		return 0;
	}

	/* read startup-cancel message followed after ssl
	 * negotiation */
	assert(client->startup.is_ssl_request);
	msg = od_read_startup(&client->io,
			      client->config_listen->client_login_timeout);
	if (msg == NULL)
		return -1;
	rc = kiwi_be_read_startup(machine_msg_data(msg), machine_msg_size(msg),
				  &client->startup,
				  &client->yb_startup_settings, true);
	machine_msg_free(msg);
	if (rc == -1)
		goto error;

	rc = od_compression_frontend_setup(client, client->config_listen,
					   &instance->logger);
	if (rc == -1) {
		return -1;
	}

	return 0;

error:
	od_debug(&instance->logger, "startup", client, NULL,
		 "startup packet read error");
	od_cron_t *cron = client->global->cron;
	od_atomic_u64_inc(&cron->startup_errors);
	return -1;
}

static inline od_frontend_status_t
od_frontend_attach(od_client_t *client, char *context,
		   kiwi_params_t *route_params)
{
	od_instance_t *instance = client->global->instance;
	od_router_t *router = client->global->router;
	od_route_t *route = client->route;

	bool wait_for_idle = false;
	for (;;) {
		if (yb_is_route_invalid(route))
			return OD_EATTACH;

		od_router_status_t status;
		status = od_router_attach(router, client, wait_for_idle, client);
		if (status != OD_ROUTER_OK) {
			if (status == OD_ROUTER_ERROR_TIMEDOUT) {
				od_error(&instance->logger, "router", client,
					 NULL,
					 "server pool wait timed out, closing");
				return OD_EATTACH_TOO_MANY_CONNECTIONS;
			}
			return OD_EATTACH;
		}

		od_server_t *server = client->server;
		if (server->io.io && !machine_connected(server->io.io)) {
			od_log(&instance->logger, context, client, server,
			       "server disconnected, close connection and retry attach");
			od_router_close(router, client);
			continue;
		}
		od_debug(&instance->logger, context, client, server,
			 "client %s%.*s attached to %s%.*s",
			 client->id.id_prefix,
			 (int)sizeof(client->id.id_prefix), client->id.id,
			 server->id.id_prefix,
			 (int)sizeof(server->id.id_prefix), server->id.id);

		/* connect to server, if necessary */
		if (server->io.io) {
			return OD_OK;
		}

		int rc;
		od_atomic_u32_inc(&router->servers_routing);
		rc = od_backend_connect(server, context, route_params, client);
		od_atomic_u32_dec(&router->servers_routing);
		if (rc == -1) {
			/* In case of 'too many connections' error, retry attach attempt by
			 * waiting for a idle server connection for pool_timeout ms
			 */
			wait_for_idle =
				route->rule->pool->timeout > 0 &&
				od_frontend_error_is_too_many_connections(
					client);
			if (wait_for_idle) {
				od_router_close(router, client);
				if (instance->config.server_login_retry) {
					machine_sleep(
						instance->config
							.server_login_retry);
				}
				continue;
			}

			/* YB: check auth failure status codes to update OID status */
			if (yb_frontend_error_is_db_does_not_exist(client))
				yb_mark_routes_inactive(router, ((od_route_t *)server->route)->id.yb_db_oid, -1);
			else if (yb_frontend_error_is_role_does_not_exist(client))
				yb_mark_routes_inactive(router, -1, ((od_route_t *)server->route)->id.yb_user_oid);

			return OD_ESERVER_CONNECT;
		}

		/* In case we create a new server but the logical client version of
		 * transactional backend is greater than the client's version then disconnect
		 * the client. This can happen for existing logical connections that are
		 * authenticated but during issuing the query, some ALTER ROLE SET or
		 * ALTER DATABASE set command has been executed that bumped up the
		 * current_version in pg_yb_logical_client_version.
		*/
		if (version_matching &&
			!version_matching_connect_higher_version &&
			 client->logical_client_version < server->logical_client_version) {
			od_log(&instance->logger, context, client, server,
			       "no matching server version found for client as client's logical version = %d "
				    "and server's logical version = %d",
			       client->logical_client_version,
			       server->logical_client_version);
			return OD_ESERVER_CONNECT;
		}

		/* In case we create a new server but the logical client version of
		 * transactional backend is greater than the client's version then disconnect
		 * the client. This can happen for existing logical connections that are
		 * authenticated but during issuing the query, some ALTER ROLE SET or
		 * ALTER DATABASE set command has been executed that bumped up the
		 * current_version in pg_yb_logical_client_version.
		*/
		if (version_matching &&
			!version_matching_connect_higher_version &&
			 client->logical_client_version < server->logical_client_version) {
			od_log(&instance->logger, context, client, server,
			       "no matching server version found for client as client's logical version = %d "
				    "and server's logical version = %d",
			       client->logical_client_version,
			       server->logical_client_version);
			return OD_ESERVER_CONNECT;
		}

		return OD_OK;
	}
}

static inline od_frontend_status_t
od_frontend_attach_and_deploy(od_client_t *client, char *context)
{
	/* attach and maybe connect server */
	od_frontend_status_t status;
	status = od_frontend_attach(client, context, NULL);
	if (status != OD_OK)
		return status;
	od_server_t *server = client->server;

	/* configure server using client parameters */
	int rc;
	rc = od_deploy(client, context);
	if (rc == -1)
		return OD_ESERVER_WRITE;

	if (client->deploy_err)
		return YB_OD_DEPLOY_ERR;

	/* set number of replies to discard */
	client->server->deploy_sync = rc;

	od_server_sync_request(server, server->deploy_sync);
	return OD_OK;
}

static inline od_frontend_status_t od_frontend_setup(od_client_t *client)
{
	od_instance_t *instance = client->global->instance;
	od_route_t *route = client->route;

	if (route->rule->pool->reserve_prepared_statement) {
		if (od_client_init_hm(client) != OK_RESPONSE) {
			od_log(&instance->logger, "setup", client, NULL,
			       "failed to initialize hash map for prepared statements");
			return OD_EOOM;
		}
	}

	/* write key data message */
	machine_msg_t *stream;
	machine_msg_t *msg;
	msg = kiwi_be_write_backend_key_data(NULL, client->key.key_pid,
					     client->key.key);
	if (msg == NULL)
		return OD_EOOM;
	stream = msg;

	/* write ready message */
	msg = kiwi_be_write_ready(stream, 'I');
	if (msg == NULL) {
		machine_msg_free(stream);
		return OD_EOOM;
	}

	int rc;
	rc = od_write(&client->io, &stream);
	if (rc == -1)
		return OD_ECLIENT_WRITE;

	if (instance->config.log_session) {
		client->time_setup = machine_time_us();
		od_log(&instance->logger, "setup", client, NULL,
		       "login time: %d microseconds",
		       (client->time_setup - client->time_accept));
		od_log(&instance->logger, "setup", client, NULL,
		       "client connection from %s to route %s.%s accepted",
		       client->peer, route->rule->db_name,
		       route->rule->user_name);
	}

	return OD_OK;
}

static inline od_frontend_status_t od_frontend_local_setup(od_client_t *client)
{
	machine_msg_t *stream;
	stream = machine_msg_create(0);

	if (stream == NULL)
		goto error;
	/* client parameters */
	machine_msg_t *msg;
	msg = kiwi_be_write_parameter_status(stream, "server_version", 15,
					     "9.6.0", 6);
	if (msg == NULL)
		goto error;
	msg = kiwi_be_write_parameter_status(stream, "server_encoding", 16,
					     "UTF-8", 6);
	if (msg == NULL)
		goto error;
	msg = kiwi_be_write_parameter_status(stream, "client_encoding", 16,
					     "UTF-8", 6);
	if (msg == NULL)
		goto error;
	msg = kiwi_be_write_parameter_status(stream, "DateStyle", 10, "ISO", 4);
	if (msg == NULL)
		goto error;
	msg = kiwi_be_write_parameter_status(stream, "TimeZone", 9, "GMT", 4);
	if (msg == NULL)
		goto error;
	/* ready message */
	msg = kiwi_be_write_ready(stream, 'I');
	if (msg == NULL)
		goto error;
	int rc;
	rc = od_write(&client->io, &stream);
	if (rc == -1)
		return OD_ECLIENT_WRITE;
	return OD_OK;
error:
	if (stream)
		machine_msg_free(stream);
	return OD_EOOM;
}

static inline bool od_eject_conn_with_rate(od_client_t *client,
					   od_server_t *server,
					   od_instance_t *instance)
{
	if (server == NULL) {
		/* server is null - client was never attached to any server so its ok to eject this conn  */
		return true;
	}
	od_thread_global **gl = od_thread_global_get();
	if (gl == NULL) {
		od_log(&instance->logger, "shutdown", client, server,
		       "drop client connection on restart, unable to throttle (wid %d)",
		       (*gl)->wid);
		/* this is clearly something bad, TODO: handle properly */
		return true;
	}

	od_conn_eject_info *info = (*gl)->info;

	struct timeval tv;
	gettimeofday(&tv, NULL);
	bool res = false;

	pthread_mutex_lock(&info->mu);
	{
		if (info->last_conn_drop_ts + /* 1 sec */ 1 > tv.tv_sec) {
			od_log(&instance->logger, "shutdown", client, server,
			       "delay drop client connection on restart, last drop was too recent (wid %d, last drop %d, curr time %d)",
			       (*gl)->wid, info->last_conn_drop_ts, tv.tv_sec);
		} else {
			info->last_conn_drop_ts = tv.tv_sec;
			res = true;

			od_log(&instance->logger, "shutdown", client, server,
			       "drop client connection on restart (wid %d, last eject %d, curr time %d)",
			       (*gl)->wid, info->last_conn_drop_ts, tv.tv_sec);
		}
	}
	pthread_mutex_unlock(&info->mu);

	return res;
}

static inline bool od_eject_conn_with_timeout(od_client_t *client,
					      __attribute__((unused))
					      od_server_t *server,
					      uint64_t timeout)
{
	assert(server != NULL);
	od_dbg_printf_on_dvl_lvl(1, "current time %lld, drop horizon %lld\n",
				 machine_time_us(),
				 client->time_last_active + timeout);

	if (client->time_last_active + timeout < machine_time_us()) {
		return true;
	}

	return false;
}

static inline bool od_should_drop_connection(od_client_t *client,
					     od_server_t *server)
{
	od_instance_t *instance = client->global->instance;

	switch (client->rule->pool->pool) {
	case OD_RULE_POOL_SESSION: {
		if (od_unlikely(client->rule->pool->client_idle_timeout)) {
			// as we do not unroute client in session pooling after transaction block etc
			// we should consider this case separately
			// general logic is: if client do nothing long enough we can assume this is just a stale connection
			// but we need to ensure this connection was initialized etc
			if (od_unlikely(
				    server != NULL && server->is_allocated &&
				    !server->is_transaction &&
				    /* case when we are out of any transactional block ut perform some stmt */
				    od_server_synchronized(server))) {
				if (od_eject_conn_with_timeout(
					    client, server,
					    client->rule->pool
						    ->client_idle_timeout)) {
					od_log(&instance->logger, "shutdown",
					       client, server,
					       "drop idle client connection on due timeout %d sec",
					       client->rule->pool
						       ->client_idle_timeout);

					return true;
				}
			}
		}
		if (od_unlikely(
			    client->rule->pool->idle_in_transaction_timeout)) {
			// the save as above but we are going to drop client inside transaction block
			if (server != NULL && server->is_allocated &&
			    server->is_transaction &&
			    /*server is sync - that means client executed some stmts and got get result, and now just... do nothing */
			    od_server_synchronized(server)) {
				if (od_eject_conn_with_timeout(
					    client, server,
					    client->rule->pool
						    ->idle_in_transaction_timeout)) {
					od_log(&instance->logger, "shutdown",
					       client, server,
					       "drop idle in transaction connection on due timeout %d sec",
					       client->rule->pool
						       ->idle_in_transaction_timeout);

					return true;
				}
			}
		}
	}
		/* fall through */
		yb_od_attribute_fallthrough;
	case OD_RULE_POOL_STATEMENT:
	case OD_RULE_POOL_TRANSACTION: {
		//TODO:: drop no more than X connection per sec/min/whatever
		if (od_likely(instance->shutdown_worker_id ==
			      INVALID_COROUTINE_ID)) {
			// try to optimize likely path
			return false;
		}

		if (od_unlikely(client->rule->storage->storage_type ==
				OD_RULE_STORAGE_LOCAL)) {
			/* local server is not very important (db like console, pgbouncer used for stats)*/
			return true;
		}

		if (od_unlikely(server == NULL)) {
			return od_eject_conn_with_rate(client, server,
						       instance);
		}
		if (!server->is_allocated) {
			return true;
		}
		if (server->state ==
			    OD_SERVER_ACTIVE /* we can drop client that are just connected and do not perform any queries */
		    && !od_server_synchronized(server)) {
			/* most probably we are not in transcation, but still executing some stmt */
			return false;
		}
		if (od_unlikely(!server->is_transaction)) {
			return od_eject_conn_with_rate(client, server,
						       instance);
		}
		return false;
	} break;
	default:
		return false;
	}
}
static od_frontend_status_t od_frontend_ctl(od_client_t *client)
{
	uint32_t op = od_client_ctl_of(client);
	if (op & OD_CLIENT_OP_KILL) {
		od_client_ctl_unset(client, OD_CLIENT_OP_KILL);
		od_client_notify_read(client);
		return OD_STOP;
	}
	return OD_OK;
}

static od_frontend_status_t od_frontend_local(od_client_t *client)
{
	od_instance_t *instance = client->global->instance;

	for (;;) {
		machine_msg_t *msg = NULL;
		for (;;) {
			/* local server is alwys null */
			if (od_should_drop_connection(client, NULL)) {
				/* Odyssey is in a state of completion, we done
                         * the last client's request and now we can drop the connection  */

				/* a sort of EAGAIN */
				return OD_ECLIENT_READ;
			}
			/* one minute */
			msg = od_read(&client->io, 60000);

			if (machine_timedout()) {
				/* retry wait to recheck exit condition */
				assert(msg == NULL);
				continue;
			}

			if (msg == NULL) {
				return OD_ECLIENT_READ;
			} else {
				break;
			}
		}

		/* client operations */
		od_frontend_status_t status;
		status = od_frontend_ctl(client);

		if (status != OD_OK)
			break;

		kiwi_fe_type_t type;
		type = *(char *)machine_msg_data(msg);

		od_debug(&instance->logger, "local", client, NULL, "%s",
			 kiwi_fe_type_to_string(type));

		if (type == KIWI_FE_TERMINATE) {
			machine_msg_free(msg);
			break;
		}

		machine_msg_t *stream = machine_msg_create(0);
		if (stream == NULL) {
			machine_msg_free(msg);
			return OD_EOOM;
		}

		int rc;
		if (type == KIWI_FE_QUERY) {
			rc = od_console_query(client, stream,
					      machine_msg_data(msg),
					      machine_msg_size(msg));
			machine_msg_free(msg);
			if (rc == -1) {
				machine_msg_free(stream);
				return OD_EOOM;
			}
		} else {
			/* unsupported */
			machine_msg_free(msg);

			od_error(&instance->logger, "local", client, NULL,
				 "unsupported request '%s'",
				 kiwi_fe_type_to_string(type));

			msg = od_frontend_errorf(client, stream,
						 KIWI_FEATURE_NOT_SUPPORTED,
						 "unsupported request '%s'",
						 kiwi_fe_type_to_string(type));
			if (msg == NULL) {
				machine_msg_free(stream);
				return OD_EOOM;
			}
		}

		/* ready */
		msg = kiwi_be_write_ready(stream, 'I');
		if (msg == NULL) {
			machine_msg_free(stream);
			return OD_EOOM;
		}

		rc = od_write(&client->io, &stream);
		if (rc == -1) {
			return OD_ECLIENT_WRITE;
		}
	}

	return OD_OK;
}

static od_frontend_status_t od_frontend_remote_server(od_relay_t *relay,
						      char *data, int size)
{
	od_client_t *client = relay->on_packet_arg;
	od_server_t *server = client->server;
	od_route_t *route = client->route;
	od_instance_t *instance = client->global->instance;

	kiwi_be_type_t type = *data;
	if (instance->config.log_debug)
		od_debug(&instance->logger, "main", client, server, "%s",
			 kiwi_be_type_to_string(type));

	int is_deploy = od_server_in_deploy(server);
	int is_ready_for_query = 0;

	int rc;
	bool skip_forward_to_client = false;
	switch (type) {
	case YB_BE_PARSE_PREPARE_ERROR_RESPONSE:
		// YB: Custom packet not required to be forwarded to client.
		od_backend_evict_server_hashmap(server, "parse prepare error", data, size);
		skip_forward_to_client = true;
		break;
	case KIWI_BE_ERROR_RESPONSE:
		od_backend_error(server, "main", data, size);
		break;
	case KIWI_BE_PARAMETER_STATUS:
		od_error(
			&instance->logger, "main", client, server,
			"Refusing to parse unexpected 'S' ParameterStatus message from Postgres");
		break;
	case YB_CONN_MGR_PARAMETER_STATUS:
		rc = od_backend_update_parameter(server, "main", data, size, 0);
		if (rc == -1)
			return relay->error_read;
		break;
	case KIWI_BE_COPY_IN_RESPONSE:
	case KIWI_BE_COPY_OUT_RESPONSE:
		server->in_out_response_received++;
		break;
	case KIWI_BE_COPY_DONE:
		/* should go after copy out
		* states that backend copy ended
		*/
		server->done_fail_response_received++;
		break;
	case KIWI_BE_COPY_FAIL:
		/*
		* states that backend copy failed
		*/
		return relay->error_write;
	case KIWI_BE_READY_FOR_QUERY: {
		is_ready_for_query = 1;
		od_backend_ready(server, data, size);

		if (is_deploy)
			server->deploy_sync--;

		if (!server->synced_settings) {
			server->synced_settings = true;
			break;
		}
		/* update server stats */
		int64_t query_time = 0;
		od_stat_query_end(&route->stats, &server->stats_state,
				  server->is_transaction, &query_time);
		if (instance->config.log_debug && query_time > 0) {
			od_debug(&instance->logger, "main", server->client,
				 server, "query time: %" PRIi64 " microseconds",
				 query_time);
		}

		break;
	}
#ifndef YB_SUPPORT_FOUND
	case KIWI_BE_PARSE_COMPLETE:
		if (route->rule->pool->reserve_prepared_statement) {
			// skip msg
			is_deploy = 1;
		}
#endif
	default:
		break;
	}

	/*
	 * If route is invalid, no need to wait for READYFORQUERY packet
	 * from the backend. Physical connection will be closed.
	 */
	if (yb_is_route_invalid(server->route))
		return OD_ESERVER_READ;

	/* error was caught during the deploy phase, return and forward to client */
	if (client->deploy_err)
		return YB_OD_DEPLOY_ERR;

	/* discard replies during configuration deploy */
	if (is_deploy || skip_forward_to_client)
		return OD_SKIP;

	if (route->id.physical_rep || route->id.logical_rep) {
		// do not detach server connection on replication
		// the exceptional case in offine: ew are going to shut down here
		if (server->offline) {
			return OD_DETACH;
		}
	} else {
		if (is_ready_for_query && od_server_synchronized(server)) {
			switch (route->rule->pool->pool) {
			case OD_RULE_POOL_STATEMENT:
				return OD_DETACH;
			case OD_RULE_POOL_TRANSACTION:
				if (!server->is_transaction) {
					/* Check for stickiness */
					if (server->yb_sticky_connection) {
						od_debug(&instance->logger, "sticky connection", client,
							server, "sticky connection established");
					} else
						return OD_DETACH;
				}
				break;
			case OD_RULE_POOL_SESSION:
				if (server->offline &&
				    !server->is_transaction) {
					return OD_DETACH;
				}
				break;
			}
		}
	}

	return OD_OK;
}

static inline od_retcode_t od_frontend_log_query(od_instance_t *instance,
						 od_client_t *client,
						 char *data, int size)
{
	uint32_t query_len;
	char *query;
	int rc;
	rc = kiwi_be_read_query(data, size, &query, &query_len);
	if (rc == -1)
		return NOT_OK_RESPONSE;

	od_log(&instance->logger, "query", client, NULL, "%.*s", query_len,
	       query);
	return OK_RESPONSE;
}

static inline od_retcode_t od_frontend_log_describe(od_instance_t *instance,
						    od_client_t *client,
						    char *data, int size)
{
	uint32_t name_len;
	char *name;
	int rc;
	kiwi_fe_describe_type_t t;
	rc = kiwi_be_read_describe(data, size, &name, &name_len, &t);
	if (rc == -1)
		return NOT_OK_RESPONSE;

	od_log(&instance->logger, "describe", client, client->server,
	       "(%s) name: %.*s",
	       t == KIWI_FE_DESCRIBE_PORTAL ? "portal" : "statement", name_len,
	       name);
	return OK_RESPONSE;
}

static inline od_retcode_t od_frontend_log_execute(od_instance_t *instance,
						   od_client_t *client,
						   char *data, int size)
{
	uint32_t name_len;
	char *name;
	int rc;
	rc = kiwi_be_read_execute(data, size, &name, &name_len);
	if (rc == -1)
		return NOT_OK_RESPONSE;

	od_log(&instance->logger, "execute", client, client->server,
	       "name: %.*s", name_len, name);
	return OK_RESPONSE;
}

static inline od_retcode_t od_frontend_parse_close(char *data, int size,
						   char **name,
						   uint32_t *name_len,
						   kiwi_fe_close_type_t *type)
{
	int rc;
	rc = kiwi_be_read_close(data, size, name, name_len, type);
	if (rc == -1)
		return NOT_OK_RESPONSE;
	return OK_RESPONSE;
}

static inline od_retcode_t od_frontend_log_close(od_instance_t *instance,
						 od_client_t *client,
						 char *name, uint32_t name_len,
						 kiwi_fe_close_type_t type)
{
	switch (type) {
	case KIWI_FE_CLOSE_PORTAL:
		od_log(&instance->logger, "close", client, client->server,
		       "portal, name: %.*s", name_len, name);
		return OK_RESPONSE;
	case KIWI_FE_CLOSE_PREPARED_STATEMENT:
		od_log(&instance->logger, "close", client, client->server,
		       "prepared statement, name: %.*s", name_len, name);
		return OK_RESPONSE;
	default:
		od_log(&instance->logger, "close", client, client->server,
		       "unknown close type, name: %.*s", name_len, name);
		return NOT_OK_RESPONSE;
	}
}

static inline od_retcode_t od_frontend_log_parse(od_instance_t *instance,
						 od_client_t *client,
						 char *context, char *data,
						 int size)
{
	uint32_t query_len;
	char *query;
	uint32_t name_len;
	char *name;
	int rc;
	rc = kiwi_be_read_parse(data, size, &name, &name_len, &query,
				&query_len);
	if (rc == -1)
		return NOT_OK_RESPONSE;

	od_log(&instance->logger, context, client, client->server, "%.*s %.*s",
	       name_len, name, query_len, query);
	return OK_RESPONSE;
}

static inline od_retcode_t od_frontend_log_bind(od_instance_t *instance,
						od_client_t *client, char *ctx,
						char *data, int size)
{
	uint32_t name_len;
	char *name;
	int rc;
	rc = kiwi_be_read_bind_stmt_name(data, size, &name, &name_len);
	if (rc == -1)
		return NOT_OK_RESPONSE;

	od_log(&instance->logger, ctx, client, client->server, "bind %.*s",
	       name_len, name);
	return OK_RESPONSE;
}

// 8 hex
#define OD_HASH_LEN 9

static inline machine_msg_t *od_frontend_rewrite_msg(char *data, int size,
						     int opname_start_offset,
						     int operator_name_len,
						     od_hash_t body_hash)
{
	machine_msg_t *msg =
		machine_msg_create(size - operator_name_len + OD_HASH_LEN);
	char *rewrite_data = machine_msg_data(msg);

	// packet header
	memcpy(rewrite_data, data, opname_start_offset);
	// prefix for opname
	od_snprintf(rewrite_data + opname_start_offset, OD_HASH_LEN, "%08x",
		    body_hash);
	// rest of msg
	memcpy(rewrite_data + opname_start_offset + OD_HASH_LEN,
	       data + opname_start_offset + operator_name_len,
	       size - opname_start_offset - operator_name_len);
	// set proper size to package
	kiwi_header_set_size((kiwi_header_t *)rewrite_data,
			     size - operator_name_len + OD_HASH_LEN);

	return msg;
}

/*
 * YB: Prepare the key using which we identify the index of the server hashmap to search.
 * For optimized mode, the client_id_len parameter is set to 0.
 */
static char *yb_prepare_server_key(char *stmt_name, int stmt_name_len, char *query_string,
								 int query_string_len, char *client_id, int client_id_len,
								 int *server_key_len)
{
	*server_key_len = (stmt_name_len - 1) + query_string_len + client_id_len;
	char *server_key = (char *)malloc(*server_key_len + 1);
	if (server_key == NULL) {
		return NULL;
	}

	/*
	 * Prune \0 at the end of only stmt_name. It is possible that
	 * the query_string contains information about parameters being used, so do
	 * not prune it. Client ID length is sent via strlen(), so no need for
	 * pruning it.
	 */
	memcpy(server_key, stmt_name, stmt_name_len - 1);
	memcpy(server_key + stmt_name_len - 1 , query_string, query_string_len);
	memcpy(server_key + stmt_name_len + query_string_len - 1, client_id, client_id_len);
	server_key[*server_key_len] = '\0';

	return server_key;
}

static od_frontend_status_t od_frontend_remote_client(od_relay_t *relay,
						      char *data, int size)
{
	/*
	 * YB: This is called during relay processing, so any message to be forwarded
	 * needs to be added to the relay->iov buffer and not written using od_write.
	 * The latter can cause a TCP deadlock in case the TCP write buffer is full
	 */
	od_client_t *client = relay->on_packet_arg;
	od_instance_t *instance = client->global->instance;
	(void)size;
	od_route_t *route = client->route;
	assert(route != NULL);

	kiwi_fe_type_t type = *data;
	if (type == KIWI_FE_TERMINATE)
		return OD_STOP;

	/* get server connection from the route pool and write
	   configuration */
	od_server_t *server = client->server;
	assert(server != NULL);

	if (instance->config.log_debug)
		od_debug(&instance->logger, "remote client", client, server,
			 "%s", kiwi_fe_type_to_string(type));

	od_frontend_status_t retstatus = OD_OK;
	switch (type) {
	case KIWI_FE_COPY_DONE:
	case KIWI_FE_COPY_FAIL:
		/* client finished copy */
		server->done_fail_response_received++;
		break;
	case KIWI_FE_QUERY:
		if (instance->config.log_query || route->rule->log_query)
			od_frontend_log_query(instance, client, data, size);
		/* update server sync state */
		od_server_sync_request(server, 1);
		break;
	case KIWI_FE_FUNCTION_CALL:
	case KIWI_FE_SYNC:
		/* update server sync state */
		od_server_sync_request(server, 1);
		break;
	case KIWI_FE_DESCRIBE:
		if (instance->config.log_query || route->rule->log_query)
			od_frontend_log_describe(instance, client, data, size);

		if (route->rule->pool->reserve_prepared_statement) {
			uint32_t operator_name_len;
			char *operator_name;
			int rc;
			kiwi_fe_describe_type_t type;
			rc = kiwi_be_read_describe(data, size, &operator_name,
						   &operator_name_len, &type);
			if (rc == -1) {
				return OD_ECLIENT_READ;
			}
			if (type == KIWI_FE_DESCRIBE_PORTAL) {
				break; // skip this, we obly need to rewrite statement
			}

			/* YB: unnamed prepared statement, check if re-parse needed */
			if (operator_name[0] == '\0') {
				assert(client->yb_unnamed_prep_stmt.description);

				if (od_id_cmp(&client->id, &server->yb_unnamed_prep_stmt_client_id))
					break;

				machine_msg_t *msg_new = NULL;
				msg_new = kiwi_fe_write_parse_description(
					NULL, client->yb_unnamed_prep_stmt.operator_name,
					client->yb_unnamed_prep_stmt.operator_name_len,
					client->yb_unnamed_prep_stmt.description,
					client->yb_unnamed_prep_stmt.description_len,
					YB_KIWI_FE_PARSE_NO_PARSE_COMPLETE);

				if (instance->config.log_query ||
					route->rule->log_query) {
					od_frontend_log_parse(
						instance, client,
						"rewrite parse",
						machine_msg_data(msg_new),
						machine_msg_size(msg_new));
				}

				od_stat_parse(&route->stats);

				rc = machine_iov_add(relay->iov, msg_new);
				if (rc == -1) {
					od_error(&instance->logger,
						 "rewrite parse", NULL, server,
						 "out of memory");
					return OD_EOOM;
				}
				break;
			}

			assert(client->prep_stmt_ids);
			int opname_start_offset =
				kiwi_be_describe_opname_offset(data, size);
			if (opname_start_offset < 0) {
				return OD_ECLIENT_READ;
			}

			od_hashmap_elt_t key;
			key.len = operator_name_len;
			key.data = operator_name;

			od_hash_t keyhash = od_murmur_hash(key.data, key.len);
			od_hashmap_elt_t *desc = od_hashmap_find(
				client->prep_stmt_ids, keyhash, &key);

			if (desc == NULL) {
				od_debug(
					&instance->logger, "remote client",
					client, server,
					"%.*s (len %d) (%u) operator was not prepared by this client",
					operator_name_len, operator_name,
					operator_name_len, keyhash);
				return OD_ESERVER_WRITE;
			}

			od_hash_t body_hash =
				od_murmur_hash(desc->data, desc->len);

			od_hash_t client_hash = od_murmur_hash(
				client->id.id, strlen(client->id.id));

			int server_key_len = 0;
			char *server_key = yb_prepare_server_key(operator_name, operator_name_len,
						desc->data, desc->len,
						client->id.id,
						instance->config.yb_optimized_extended_query_protocol ? 0 : strlen(client->id.id),
						&server_key_len);

			if (!server_key) {
				od_error(&instance->logger, "describe", client,
					 server, "failed to allocate memory");
				return OD_EOOM;
			}

			od_hashmap_elt_t server_key_desc = {server_key, server_key_len};

			od_hash_t yb_stmt_hash = od_murmur_hash(
				server_key_desc.data, server_key_desc.len);

			od_debug(&instance->logger, "rewrite describe", client,
				 server, "statement: %.*s, hash: %08x",
				 desc->len, desc->data, yb_stmt_hash);

			char opname[OD_HASH_LEN];
			od_snprintf(opname, OD_HASH_LEN, "%08x", yb_stmt_hash);

			int refcnt = 0;
			od_hashmap_elt_t value;
			value.data = &refcnt;
			value.len = sizeof(int);
			od_hashmap_elt_t *value_ptr = &value;

			// send parse msg if needed
			if (od_hashmap_insert(server->prep_stmts, yb_stmt_hash,
					      &server_key_desc, &value_ptr) == 0) {
				od_debug(
					&instance->logger,
					"rewrite parse before describe", client,
					server,
					"deploy %.*s operator hash %u to server",
					server_key_desc.len, server_key_desc.data, keyhash);
				free(server_key_desc.data);
				// rewrite msg
				// allocate prepered statement under name equal to yb_stmt_hash

				machine_msg_t *msg;
				msg = kiwi_fe_write_parse_description(
					NULL, opname, OD_HASH_LEN, desc->data,
					desc->len,
					YB_KIWI_FE_PARSE_NO_PARSE_COMPLETE);
				if (msg == NULL) {
					return OD_ESERVER_WRITE;
				}

				if (instance->config.log_query ||
				    route->rule->log_query) {
					od_frontend_log_parse(
						instance, client,
						"rewrite parse",
						machine_msg_data(msg),
						machine_msg_size(msg));
				}

				od_stat_parse(&route->stats);
				rc = machine_iov_add(relay->iov, msg);
				retstatus = OD_SKIP;
				if (rc == -1) {
					od_error(&instance->logger, "describe",
						 NULL, server, "out of memory");
					return OD_EOOM;
				}
			} else {
				int *refcnt;
				refcnt = value_ptr->data;
				*refcnt = 1 + *refcnt;
				free(server_key_desc.data);
			}

			machine_msg_t *msg;
			msg = kiwi_fe_write_describe(NULL, 'S', opname,
						     OD_HASH_LEN);

			if (msg == NULL) {
				return OD_ESERVER_WRITE;
			}

			if (instance->config.log_query ||
			    route->rule->log_query) {
				od_frontend_log_describe(instance, client,
							 machine_msg_data(msg),
							 machine_msg_size(msg));
			}

			// msg if deallocated automaictly
			rc = machine_iov_add(relay->iov, msg);
			retstatus = OD_SKIP;
			if (rc == -1) {
				od_error(&instance->logger, "describe", NULL,
					 server, "out of memory");
				return OD_EOOM;
			}
		}
		break;
	case KIWI_FE_PARSE:
		if (instance->config.log_query || route->rule->log_query)
			od_frontend_log_parse(instance, client, "parse", data,
					      size);

		if (route->rule->pool->reserve_prepared_statement) {
			/* skip client parse msg */
			kiwi_prepared_statement_t desc;
			int rc;
			rc = kiwi_be_read_parse_dest(data, size, &desc);
			if (rc) {
				return OD_ECLIENT_READ;
			}

			/* YB: unnamed prepared statement, separately track */
			if (desc.operator_name[0] == '\0') {
				yb_prepared_statement_free(&client->yb_unnamed_prep_stmt);

				rc = yb_prepared_statement_alloc(&client->yb_unnamed_prep_stmt,
								desc.operator_name, desc.operator_name_len,
								desc.description, desc.description_len);
				if (rc == -1) {
					return OD_EOOM;
				}

				server->yb_unnamed_prep_stmt_client_id = client->id;

				break;
			}

			od_hash_t keyhash = od_murmur_hash(
				desc.operator_name, desc.operator_name_len);
			od_debug(&instance->logger, "parse", client, server,
				 "saving %.*s operator hash %u",
				 desc.operator_name_len, desc.operator_name,
				 keyhash);

			od_hashmap_elt_t key;
			key.len = desc.operator_name_len;
			key.data = desc.operator_name;

			od_hashmap_elt_t value;
			value.len = desc.description_len;
			value.data = desc.description;

			od_hashmap_elt_t *value_ptr = &value;

			int opname_start_offset =
				kiwi_be_parse_opname_offset(data, size);
			if (opname_start_offset < 0) {
				return OD_ECLIENT_READ;
			}

			od_hash_t body_hash =
				od_murmur_hash(data + opname_start_offset +
						       desc.operator_name_len,
					       size - opname_start_offset -
						       desc.operator_name_len);

			od_hash_t client_hash = od_murmur_hash(
				client->id.id, strlen(client->id.id));

			assert(client->prep_stmt_ids);
#ifndef YB_SUPPORT_FOUND
			if (od_hashmap_insert(client->prep_stmt_ids, keyhash,
					      &key, &value_ptr)) {
				if (value_ptr->len != desc.description_len ||
				    strncmp(desc.description, value_ptr->data,
					    value_ptr->len) != 0) {
					value_ptr->len = desc.description_len;
					value_ptr->data = desc.description;

					/* redeploy
					* previous
					* client allocated prepared stmt with same name
					*/
					char buf[OD_HASH_LEN];
					od_snprintf(buf, OD_HASH_LEN, "%08x",
						    body_hash);

					msg = kiwi_fe_write_close(
						NULL, 'S', buf, OD_HASH_LEN);
					if (msg == NULL) {
						return OD_ESERVER_WRITE;
					}
					rc = od_write(&server->io, &msg);
					if (rc == -1) {
						od_error(&instance->logger,
							 "parse", NULL, server,
							 "write error: %s",
							 od_io_error(
								 &server->io));
						return OD_ESERVER_WRITE;
					}
					msg = kiwi_fe_write_parse_description(
						NULL, buf, OD_HASH_LEN,
						desc.description,
						desc.description_len);
					if (msg == NULL) {
						return OD_ESERVER_WRITE;
					}
				} else {
					char buf[OD_HASH_LEN];
					od_snprintf(buf, OD_HASH_LEN, "%08x",
						    body_hash);
					msg = kiwi_fe_write_parse_description(
						NULL, buf, OD_HASH_LEN,
						desc.description,
						desc.description_len);
					if (msg == NULL) {
						return OD_ESERVER_WRITE;
					}
				}
			} else {
				char buf[OD_HASH_LEN];
				od_snprintf(buf, OD_HASH_LEN, "%08x",
					    body_hash);
				msg = kiwi_fe_write_parse_description(
					NULL, buf, OD_HASH_LEN,
					desc.description, desc.description_len);
				if (msg == NULL) {
					return OD_ESERVER_WRITE;
				}
			}
#endif

			/*
			 * YB: it should not matter if stmt exists in client hashmap,
			 * unconditionally send Parse and deal with any potential errors
			 * as provided by server.
			 *
			 * The only exception is when we are in optimized extended query
			 * protocol mode, in which case we send a special no-op Parse
			 * to the server if the query was already parsed on it.
			 */
			od_hashmap_insert(client->prep_stmt_ids, keyhash, &key, &value_ptr);

			int server_key_len = 0;
			char *server_key = yb_prepare_server_key(desc.operator_name, desc.operator_name_len,
						desc.description, desc.description_len,
						client->id.id,
						instance->config.yb_optimized_extended_query_protocol ? 0 : strlen(client->id.id),
						&server_key_len);

			if (!server_key) {
				od_error(&instance->logger, "parse", client,
					 server, "failed to allocate memory");
				return OD_EOOM;
			}

			od_hashmap_elt_t server_key_desc = {server_key, server_key_len};

			od_hash_t yb_stmt_hash = od_murmur_hash(server_key_desc.data, server_key_desc.len);

			char buf[OD_HASH_LEN];
			od_snprintf(buf, OD_HASH_LEN, "%08x", yb_stmt_hash);

			key.len = desc.description_len;
			key.data = desc.description;

			int refcnt = 0;
			value.data = &refcnt;
			value.len = sizeof(int);

			value_ptr = &value;

			if (od_hashmap_insert(server->prep_stmts, yb_stmt_hash,
					      &server_key_desc, &value_ptr) == 0) {
				od_debug(
					&instance->logger,
					"rewrite parse initial deploy", client,
					server,
					"deploy %.*s operator hash %u to server",
					server_key_desc.len, server_key_desc.data, keyhash);
				free(server_key_desc.data);
				// rewrite msg
				// allocate prepered statement under name equal to yb_stmt_hash

				machine_msg_t *msg;
				msg = kiwi_fe_write_parse_description(NULL, buf, OD_HASH_LEN,
					desc.description, desc.description_len, KIWI_FE_PARSE);
				if (msg == NULL) {
					return OD_ESERVER_WRITE;
				}

				if (instance->config.log_query ||
				    route->rule->log_query) {
					od_frontend_log_parse(
						instance, client,
						"rewrite parse",
						machine_msg_data(msg),
						machine_msg_size(msg));
				}

				// stat backend parse msg
				od_stat_parse(&route->stats);

				rc = machine_iov_add(relay->iov, msg);
				retstatus = OD_SKIP;
				if (rc == -1) {
					od_error(&instance->logger, "parse",
						 NULL, server, "out of memory");
					return OD_EOOM;
				}
			} else {
				int *refcnt = value_ptr->data;
				*refcnt = 1 + *refcnt;
				free(server_key_desc.data);

				machine_msg_t *msg;
				if (!instance->config.yb_optimized_extended_query_protocol) {
					od_debug(&instance->logger, "parse",
						 client, server,
						 "unoptimized parse, send packet to server");
					msg = kiwi_fe_write_parse_description(NULL, buf, OD_HASH_LEN,
						desc.description, desc.description_len, KIWI_FE_PARSE);
				}
				else { // no-op parse
					od_debug(&instance->logger, "parse",
						 client, server,
						 "optimized parse, send no-op to server");
					msg = kiwi_fe_write_parse_description(NULL, buf, OD_HASH_LEN, desc.description,
						desc.description_len, YB_KIWI_FE_NO_PARSE_PARSE_COMPLETE);
				}
				if (msg == NULL) {
					return OD_ESERVER_WRITE;
				}

				rc = machine_iov_add(relay->iov, msg);
				retstatus = OD_SKIP;
				if (rc == -1) {
					od_error(&instance->logger, "parse",
						 NULL, server, "out of memory");
					return OD_EOOM;
				}
			}
#ifndef YB_SUPPORT_FOUND
			machine_msg_t *pmsg;
			pmsg = kiwi_be_write_parse_complete(NULL);
			if (pmsg == NULL) {
				return OD_ESERVER_WRITE;
			}
			rc = od_write(&client->io, &pmsg);
			forwarded = 1;

			if (rc == -1) {
				od_error(&instance->logger, "parse", client,
					 NULL, "write error: %s",
					 od_io_error(&client->io));
				return OD_ESERVER_WRITE;
			}
#endif
		}
		break;
	case KIWI_FE_BIND:
		if (instance->config.log_query || route->rule->log_query)
			od_frontend_log_bind(instance, client, "bind", data,
					     size);

		if (route->rule->pool->reserve_prepared_statement) {
			uint32_t operator_name_len;
			char *operator_name;

			int rc;
			rc = kiwi_be_read_bind_stmt_name(
				data, size, &operator_name, &operator_name_len);

			if (rc == -1) {
				return OD_ECLIENT_READ;
			}

			/* YB: unnamed prepared statement, check if re-parse needed */
			if (operator_name[0] == '\0') {
				assert(client->yb_unnamed_prep_stmt.description);

				if (od_id_cmp(&client->id, &server->yb_unnamed_prep_stmt_client_id))
					break;

				machine_msg_t *msg_new = NULL;
				msg_new = kiwi_fe_write_parse_description(
					NULL, client->yb_unnamed_prep_stmt.operator_name,
					client->yb_unnamed_prep_stmt.operator_name_len,
					client->yb_unnamed_prep_stmt.description,
					client->yb_unnamed_prep_stmt.description_len,
					YB_KIWI_FE_PARSE_NO_PARSE_COMPLETE);

				if (instance->config.log_query ||
					route->rule->log_query) {
					od_frontend_log_parse(
						instance, client,
						"rewrite parse",
						machine_msg_data(msg_new),
						machine_msg_size(msg_new));
				}

				od_stat_parse(&route->stats);
				rc = machine_iov_add(relay->iov, msg_new);
				if (rc == -1) {
					od_error(&instance->logger,
						 "rewrite parse", NULL, server,
						 "out of memory",
						 od_io_error(&server->io));
					return OD_ESERVER_WRITE;
				}
				break;
			}

			int opname_start_offset =
				kiwi_be_bind_opname_offset(data, size);
			if (opname_start_offset < 0) {
				return OD_ECLIENT_READ;
			}

			od_hashmap_elt_t key;
			key.len = operator_name_len;
			key.data = operator_name;

			od_hash_t keyhash = od_murmur_hash(key.data, key.len);

			od_hashmap_elt_t *desc =
				(od_hashmap_elt_t *)od_hashmap_find(
					client->prep_stmt_ids, keyhash, &key);
			if (desc == NULL) {
				od_debug(
					&instance->logger, "remote client",
					client, server,
					"%.*s (%u) operator was not prepared by this client",
					operator_name_len, operator_name,
					keyhash);
				return OD_ESERVER_WRITE;
			}

			od_hash_t body_hash =
				od_murmur_hash(desc->data, desc->len);
			od_hash_t client_hash =
				od_murmur_hash(client->id.id,
					       strlen(client->id.id));

			int server_key_len = 0;
			char *server_key = yb_prepare_server_key(operator_name, operator_name_len,
						desc->data, desc->len,
						client->id.id,
						instance->config.yb_optimized_extended_query_protocol ? 0 : strlen(client->id.id),
						&server_key_len);

			if (!server_key) {
				od_error(&instance->logger, "bind", client,
					 server, "failed to allocate memory");
				return OD_EOOM;
			}

			od_hashmap_elt_t server_key_desc = {server_key, server_key_len};

			od_hash_t yb_stmt_hash = od_murmur_hash(server_key_desc.data, server_key_desc.len);

			od_debug(&instance->logger, "rewrite bind", client,
				 server, "statement: %.*s, hash: %08x",
				 desc->len, desc->data, yb_stmt_hash);

			od_hashmap_elt_t value;
			int refcnt = 1;
			value.data = &refcnt;
			value.len = sizeof(int);
			od_hashmap_elt_t *value_ptr = &value;

			char opname[OD_HASH_LEN];
			od_snprintf(opname, OD_HASH_LEN, "%08x", yb_stmt_hash);

			if (od_hashmap_insert(server->prep_stmts, yb_stmt_hash,
					      &server_key_desc, &value_ptr) == 0) {
				od_debug(
					&instance->logger,
					"rewrite parse before bind", client,
					server,
					"deploy %.*s operator hash %u to server",
					server_key_desc.len, server_key_desc.data, keyhash);
				free(server_key_desc.data);
				// rewrite msg
				// allocate prepered statement under name equal to yb_stmt_hash

				machine_msg_t *msg;
				msg = kiwi_fe_write_parse_description(
					NULL, opname, OD_HASH_LEN, desc->data,
					desc->len, YB_KIWI_FE_PARSE_NO_PARSE_COMPLETE);

				if (msg == NULL) {
					return OD_ESERVER_WRITE;
				}

				if (instance->config.log_query ||
				    route->rule->log_query) {
					od_frontend_log_parse(
						instance, client,
						"rewrite parse",
						machine_msg_data(msg),
						machine_msg_size(msg));
				}

				od_stat_parse(&route->stats);
				rc = machine_iov_add(relay->iov, msg);
				retstatus = OD_SKIP;
				if (rc == -1) {
					od_error(&instance->logger,
						 "rewrite parse", NULL, server,
						 "out of memory");
					return OD_EOOM;
				}
			} else {
				int *refcnt = value_ptr->data;
				*refcnt = 1 + *refcnt;
				free(server_key_desc.data);
			}

			machine_msg_t *msg;
			msg = od_frontend_rewrite_msg(data, size,
						      opname_start_offset,
						      operator_name_len,
						      yb_stmt_hash);

			if (msg == NULL) {
				return OD_ESERVER_WRITE;
			}

			if (instance->config.log_query ||
			    route->rule->log_query) {
				od_frontend_log_bind(instance, client,
						     "rewrite bind",
						     machine_msg_data(msg),
						     machine_msg_size(msg));
			}

			rc = machine_iov_add(relay->iov, msg);
			retstatus = OD_SKIP;

			if (rc == -1) {
				od_error(&instance->logger, "rewrite bind",
					 NULL, server, "out of memory");
				return OD_EOOM;
			}
		}
		break;
	case KIWI_FE_EXECUTE:
		if (instance->config.log_query || route->rule->log_query)
			od_frontend_log_execute(instance, client, data, size);
		break;
	case KIWI_FE_CLOSE:
		if (route->rule->pool->reserve_prepared_statement) {
			char *name;
			uint32_t name_len;
			kiwi_fe_close_type_t type;
			int rc;

			if (od_frontend_parse_close(data, size, &name,
						    &name_len,
						    &type) != OK_RESPONSE) {
				return OD_ESERVER_WRITE;
			}

			if (type == KIWI_FE_CLOSE_PREPARED_STATEMENT) {
				retstatus = OD_SKIP;
				od_log(
					&instance->logger,
					"close prepared statement",
					client, server, "ignore closing prepared statement: %.*s, report it as closed "
					"by returning a close complete message from connection manager",
					name_len, name);

				machine_msg_t *pmsg;
				pmsg = kiwi_be_write_close_complete(NULL);

				/* TODO(#29442): Replace with async write to avoid TCP deadlock */
				rc = od_write(&client->io, &pmsg);
				if (rc == -1) {
					od_error(&instance->logger,
						 "close report", NULL, server,
						 "write error: %s",
						 od_io_error(&server->io));
					return OD_ECLIENT_WRITE;
				}
			}

			if (instance->config.log_query ||
			    route->rule->log_query) {
				od_frontend_log_close(instance, client, name,
						      name_len, type);
			}

		} else if (instance->config.log_query ||
			   route->rule->log_query) {
			char *name;
			uint32_t name_len;
			kiwi_fe_close_type_t type;

			if (od_frontend_parse_close(data, size, &name,
						    &name_len,
						    &type) != OK_RESPONSE) {
				return OD_ESERVER_WRITE;
			}

			od_frontend_log_close(instance, client, name, name_len,
					      type);
		}
		break;
	default:
		break;
	}

	/* update server stats */
	od_stat_query_start(&server->stats_state);
	return retstatus;
}

static void od_frontend_remote_server_on_read(od_relay_t *relay, int size)
{
	od_stat_t *stats = relay->on_read_arg;
	od_stat_recv_server(stats, size);
}

static void od_frontend_remote_client_on_read(od_relay_t *relay, int size)
{
	od_stat_t *stats = relay->on_read_arg;
	od_stat_recv_client(stats, size);
}

static inline od_frontend_status_t od_frontend_poll_catchup(od_client_t *client,
							    od_route_t *route,
							    uint32_t timeout)
{
	od_instance_t *instance = client->global->instance;

	od_dbg_printf_on_dvl_lvl(
		1, "client %s polling replica for catchup with timeout %d\n",
		client->id.id, timeout);
	for (int checks = 0; checks < route->rule->catchup_checks; ++checks) {
		od_dbg_printf_on_dvl_lvl(1, "current cached time %d\n",
					 machine_timeofday_sec());
		int lag = machine_timeofday_sec() - route->last_heartbeat;
		if (lag < 0) {
			lag = 0;
		}
		if ((uint32_t)lag < timeout) {
			return OD_OK;
		}
		od_debug(
			&instance->logger, "catchup", client, NULL,
			"client %s replication %d lag is over catchup timeout %d\n",
			client->id.id, lag, timeout);
		od_frontend_info(
			client,
			"replication lag %d is over catchup timeout %d\n", lag,
			timeout);
		machine_sleep(1000);
	}
	return OD_ECATCHUP_TIMEOUT;
}

static inline od_frontend_status_t
od_frontend_remote_process_server(od_server_t *server, od_client_t *client)
{
	od_frontend_status_t status = od_relay_step(&server->relay);
	int rc;
	od_instance_t *instance = client->global->instance;

	if (status == OD_DETACH) {
		/* detach on transaction or statement pooling  */
		/* write any pending data to server first */
		status = od_relay_flush(&server->relay);
		if (status != OD_OK)
			return status;

		od_relay_detach(&client->relay);
		od_relay_stop(&server->relay);

		/* cleanup server */
		rc = od_reset(server);
		if (rc != 1) {
			return OD_ESERVER_WRITE;
		}

		od_debug(&instance->logger, "detach", client, server,
			 "client %s%.*s detached from %s%.*s",
			 client->id.id_prefix,
			 (int)sizeof(client->id.id_prefix), client->id.id,
			 server->id.id_prefix,
			 (int)sizeof(server->id.id_prefix), server->id.id);

		/* push server connection back to route pool */
		od_router_t *router = client->global->router;
		od_router_detach(router, client);
		server = NULL;
	} else if (status != OD_OK) {
		return status;
	}
	return OD_OK;
}

static od_frontend_status_t od_frontend_remote(od_client_t *client)
{
	od_route_t *route = client->route;
	client->cond = machine_cond_create();

	if (client->cond == NULL) {
		return OD_EOOM;
	}

	od_frontend_status_t status;

	/* enable client notification mechanism */
	int rc;
	rc = machine_read_start(client->notify_io, client->cond);
	if (rc == -1) {
		return OD_ECLIENT_READ;
	}

	bool reserve_session_server_connection =
		route->rule->reserve_session_server_connection;

	status = od_relay_start(&client->relay, client->cond, OD_ECLIENT_READ,
				OD_ESERVER_WRITE,
				od_frontend_remote_client_on_read,
				&route->stats, od_frontend_remote_client,
				client, reserve_session_server_connection);

	if (status != OD_OK) {
		return status;
	}

	od_server_t *server = NULL;
	od_instance_t *instance = client->global->instance;

	for (;;) {
		for (;;) {
			if (od_should_drop_connection(client, server)) {
				/* Odyssey is going to shut down or client conn is dropped
				* due some idle timeout, we drop the connection  */
				/* a sort of EAGAIN */
				status = OD_ECLIENT_READ;
				break;
			}

// Disabled the unnecessary logs
#ifndef YB_SUPPORT_FOUND
#if OD_DEVEL_LVL != OD_RELEASE_MODE
			if (server != NULL && server->is_allocated &&
			    server->is_transaction &&
			    od_server_synchronized(server)) {
				od_dbg_printf_on_dvl_lvl(
					1,
					"here we have idle in transaction: cid %s\n",
					client->id.id);
			}
#endif
#endif
			/* one minute */
			if (machine_cond_wait(client->cond, 60000) == 0) {
				client->time_last_active = machine_time_us();
#ifndef YB_SUPPORT_FOUND
				od_dbg_printf_on_dvl_lvl(
					1,
					"change client last active time %lld\n",
					client->time_last_active);
#endif
				break;
			}
		}

		if (od_frontend_status_is_err(status))
			break;

		/* client operations */
		status = od_frontend_ctl(client);
		if (status != OD_OK)
			break;

		server = client->server;

		/* attach */
		status = od_relay_step(&client->relay);
		if (status == OD_ATTACH) {
			uint32_t catchup_timeout = route->rule->catchup_timeout;
#ifndef YB_GUC_SUPPORT_VIA_SHMEM
			/* YB: This is never expected to get a variable */
			kiwi_var_t *timeout_var = yb_kiwi_vars_get(
				&client->yb_vars_session,
				"odyssey_catchup_timeout",
				yb_od_instance_should_lowercase_guc_name(
					instance));
#else
			kiwi_var_t *timeout_var =
				kiwi_vars_get(&client->vars,
					      KIWI_VAR_ODYSSEY_CATCHUP_TIMEOUT);
#endif

			if (timeout_var != NULL) {
				/* if there is catchup pgoption variable in startup packet */
				char *end;
				uint32_t user_catchup_timeout =
					strtol(timeout_var->value, &end, 10);
				if (end == timeout_var->value +
						   timeout_var->value_len) {
					// if where is no junk after number, thats ok
					catchup_timeout = user_catchup_timeout;
				} else {
					od_error(
						&instance->logger, "catchup",
						client, NULL,
						"junk after catchup timeout, ignore value");
				}
			}

			if (catchup_timeout) {
				status = od_frontend_poll_catchup(
					client, route, catchup_timeout);
			}

			if (od_frontend_status_is_err(status))
				break;

			assert(server == NULL);

		yb_retry_attach:
			/*
			 * YB: Separate the attach and deploy phases around the initialization of the relay to
			 * allow correct handling of the condition variables used to synchronize code flow around
			 * IO operations. This should be done only if in optimized GUC support mode; it is safe
			 * to initialize the relay after deploy phase without optimization of GUC support.
			 * 
			 * Here are the sequence of operations for each mode:
			 * Without optimization: attach -> deploy -> relay_init -> relay_start
			 * With optimization: attach -> relay_init -> deploy -> relay_start
			 */
			status = od_frontend_attach(client, "main", NULL);
			if (status != OD_OK)
				break;
			server = client->server;

			if (instance->config.yb_optimized_session_parameters) {
				status = yb_od_relay_start_init(
					&server->relay, client->cond, OD_ESERVER_READ,
					OD_ECLIENT_WRITE,
					od_frontend_remote_server_on_read,
					&route->stats, od_frontend_remote_server,
					client);
				if (status != OD_OK)
					break;
			}

			int rc;
			rc = od_deploy(client, "main");
			if (rc == -1)
				status = OD_ESERVER_WRITE;

			/*
			 * YB: This return condition is only possible for optimized support
			 * for GUC variables. It occurs due to conflict-related errors when
			 * dealing with concurrent transactions; odyssey assumes that the
			 * transaction is rolled back on the server and proceeds with
			 * operations, where the server process is still in a transaction.
			 * Close the current server process and retry by attaching to a
			 * different server whenever this occurs.
			 */
			if (rc == -2) {
				assert(instance->config.yb_optimized_session_parameters);
				od_error(
					&instance->logger, "main", client, server,
					"deploy error: conflict with another transaction");
				od_router_close(client->global->router, client);
				goto yb_retry_attach;
			}

			if (client->deploy_err)
				status = YB_OD_DEPLOY_ERR;

			/* set number of replies to discard */
			client->server->deploy_sync = rc;

			od_server_sync_request(server, server->deploy_sync);

			if (status != OD_OK)
				break;

			if (!instance->config.yb_optimized_session_parameters) {
				status = yb_od_relay_start_init(
					&server->relay, client->cond, OD_ESERVER_READ,
					OD_ECLIENT_WRITE,
					od_frontend_remote_server_on_read,
					&route->stats, od_frontend_remote_server,
					client);
				if (status != OD_OK)
					break;
			}

			/*
			 * YB: Start IO operations only after concluding deploy operations in optimized
			 * GUC support mode. In unoptimized mode, we mimic upstream odyssey behavior of
			 * calling od_frontend_attach_and_deploy(), followed by od_relay_start().
			 */
			yb_od_relay_start_io(&server->relay, reserve_session_server_connection);
			od_relay_attach(&client->relay, &server->io);
			od_relay_attach(&server->relay, &client->io);

			/* retry read operation after attach */
			continue;
		} else if (status != OD_OK) {
			break;
		}

		if (server == NULL)
			continue;

		status = od_frontend_remote_process_server(server, client);
		if (status != OD_OK) {
			break;
		}
	}

	if (client->server) {
		od_server_t *curr_server = client->server;

		od_frontend_status_t flush_status;
		flush_status = od_relay_flush(&curr_server->relay);
		od_relay_stop(&curr_server->relay);
		if (flush_status != OD_OK) {
			return flush_status;
		}

		flush_status = od_relay_flush(&client->relay);
		if (flush_status != OD_OK) {
			return flush_status;
		}
	}

	od_relay_stop(&client->relay);
	return status;
}

static void od_frontend_cleanup(od_client_t *client, char *context,
				od_frontend_status_t status,
				od_error_logger_t *l)
{
	od_instance_t *instance = client->global->instance;
	od_router_t *router = client->global->router;
	od_route_t *route = client->route;
	char peer[128];
	int rc;

	od_server_t *server = client->server;

	if (od_frontend_status_is_err(status)) {
		od_error_logger_store_err(l, status);

		if (route->extra_logging_enabled &&
		    !od_route_is_dynamic(route)) {
			od_error_logger_store_err(route->err_logger, status);
		}
	}

	switch (status) {
	case OD_STOP:
	/* fallthrough */
	case OD_OK:
		/* graceful disconnect or kill */
		if (instance->config.log_session) {
			od_log(&instance->logger, context, client, server,
			       "client disconnected (route %s.%s)",
			       route->rule->db_name, route->rule->user_name);
		}
		if (!client->server)
			break;

		rc = od_reset(server);
		if (rc != 1) {
			/* close backend connection */
			od_router_close(router, client);
			break;
		}
		/* push server to router server pool */
		od_router_detach(router, client);
		break;

	case OD_EOOM:
		od_error(&instance->logger, context, client, server, "%s",
			 "memory allocation error");
		if (client->server)
			od_router_close(router, client);
		break;

	case OD_EATTACH:
		assert(server == NULL);
		assert(client->route != NULL);
		od_frontend_fatal(client, KIWI_CONNECTION_FAILURE,
				  "failed to get remote server connection");
		break;

	case OD_EATTACH_TOO_MANY_CONNECTIONS:
		assert(server == NULL);
		assert(client->route != NULL);
		od_frontend_fatal(
			client, KIWI_TOO_MANY_CONNECTIONS,
			"too many active clients for user (pool_size for "
			"user %s.%s reached %d)",
			client->startup.database.value,
			client->startup.user.value,
			client->rule != NULL ? client->rule->pool->size : -1);
		break;

	case OD_ECLIENT_READ:
		/*fallthrough*/
	case OD_ECLIENT_WRITE:
		/* close client connection and reuse server
			 * link in case of client errors */

		od_getpeername(client->io.io, peer, sizeof(peer), 1, 1);
		od_log(&instance->logger, context, client, server,
		       "client disconnected (read/write error, addr %s): %s, status %s",
		       peer, od_io_error(&client->io),
		       od_frontend_status_to_str(status));
		if (!client->server)
			break;
		/*
		 * Since client has been disconnected, if IOV buffer has pending data, that means
		 * write socket buffer got full at some point. So we want to avoid writing any new packet
		 * on the server socket whether from od_reset or terminate packet to avoid TCP deadlock.
		 * TODO(mkumar): GH#29417: Make conn mgr consume results from server socket even when
		 * server is synchronized. Currently od_reset consumes the results but until only server is
		 * not synchronized.
		 */
		if (server->relay.iov != NULL && machine_iov_pending(server->relay.iov)) {
			od_debug(&instance->logger, context, client, server,
					 "Client disconnected and server's IOV buffer has pending data on it. "
					 "Directly closing the server socket without sending terminate packet.");
			/*
			 * TODO(mkumar): GH#29459: Sending 'terminate' packet causes deadlock. Sending 'Sync'
			 * in order to reuse the connection can also cause deadlock. Need to investigate more.
			 * Therefore directly closing the socket and marking the server offline will cleanup
			 * the server in od_router_detach and thus killing the backend connection as well.
			 */
			od_io_close(&server->io);
			server->offline = true;
		}
		else {
			rc = od_reset(server);
			if (rc != 1) {
				/* close backend connection */
				od_log(&instance->logger, context, client, server,
					   "reset unsuccessful, closing server connection");
				od_router_close(router, client);
				break;
			}
		}
		/* push server to router server pool */
		od_router_detach(router, client);
		break;

	case OD_ESERVER_CONNECT:
		/* server attached to client and connection failed */
		if (server->error_connect && route->rule->client_fwd_error) {
			/* forward server error to client */
			od_frontend_error_fwd(client);
		} else {
			od_frontend_fatal(
				client, KIWI_CONNECTION_FAILURE,
				"failed to connect to remote server %s%.*s",
				server->id.id_prefix,
				(int)sizeof(server->id.id), server->id.id);
		}
		/* close backend connection */
		od_router_close(router, client);
		break;
	case OD_ECATCHUP_TIMEOUT:
		/* close client connection and close server
			 * connection in case of server errors */
		od_log(&instance->logger, context, client, server,
		       "replication lag is too big, failed to wait replica for catchup: status %s",
		       od_frontend_status_to_str(status));
		od_frontend_error(
			client, KIWI_CONNECTION_FAILURE,
			"remote server read/write error: failed to wait replica for catchup");
		break;
	case OD_ESERVER_READ:
	case OD_ESERVER_WRITE:
		/* close client connection and close server
			 * connection in case of server errors */
		od_log(&instance->logger, context, client, server,
		       "server disconnected (read/write error): %s, status %s",
		       od_io_error(&server->io),
		       od_frontend_status_to_str(status));
		od_frontend_error(client, KIWI_CONNECTION_FAILURE,
				  "remote server read/write error %s%.*s: %s",
				  server->id.id_prefix,
				  (int)sizeof(server->id.id), server->id.id,
				  od_io_error(&server->io));
		/* close backend connection */
		od_router_close(router, client);
		break;
	case OD_UNDEF:
	case OD_SKIP:
	case OD_ATTACH:
	/* fallthrough */
	case OD_DETACH:
	case OD_ESYNC_BROKEN:
		od_error(&instance->logger, context, client, server,
			 "unexpected error status %s (%d)",
			 od_frontend_status_to_str(status), (uint32)status);
		od_router_close(router, client);
		break;
	case YB_OD_DEPLOY_ERR:
		/* close both client and server connection */
		/* backend connection is not in a usable state */ 
		od_error(&instance->logger, context, client, server,
				"deploy error: %s, status %s", client->deploy_err->message,
				od_frontend_status_to_str(status));
		od_frontend_fatal(client, client->deploy_err->code, client->deploy_err->message);
		/* close backend connection */
		od_router_close(router, client);
		break;
	default:
		od_error(
			&instance->logger, context, client, server,
			"unexpected error status %s (%d), possible corruption, abort()",
			od_frontend_status_to_str(status), (uint32)status);
		abort();
	}
}

static void od_application_name_add_host(od_client_t *client)
{
	/* YB: This is expected to never get executed */
	if (client == NULL || client->io.io == NULL)
		return;
	od_instance_t *instance = client->global->instance;

	char app_name_with_host[KIWI_MAX_VAR_SIZE];
	char peer_name[KIWI_MAX_VAR_SIZE];
	int app_name_len = 7;
	char *app_name = "unknown";
#ifndef YB_GUC_SUPPORT_VIA_SHMEM
	kiwi_var_t *app_name_var = yb_kiwi_vars_get(
		&client->yb_vars_session, "application_name",
		yb_od_instance_should_lowercase_guc_name(instance));
#else
	kiwi_var_t *app_name_var =
		kiwi_vars_get(&client->vars, KIWI_VAR_APPLICATION_NAME);
#endif
	if (app_name_var != NULL) {
		app_name_len = app_name_var->value_len;
		app_name = app_name_var->value;
	}
	od_getpeername(client->io.io, peer_name, sizeof(peer_name), 1,
		       0); // return code ignored

	int length =
		od_snprintf(app_name_with_host, KIWI_MAX_VAR_SIZE, "%.*s - %s",
			    app_name_len, app_name, peer_name);
#ifndef YB_GUC_SUPPORT_VIA_SHMEM
	kiwi_vars_update(&client->yb_vars_session, "application_name", 17,
			 app_name_with_host, length + 1,
			 yb_od_instance_should_lowercase_guc_name(
				 instance)); // return code ignored
#else
	kiwi_vars_set(&client->vars, KIWI_VAR_APPLICATION_NAME,
		      app_name_with_host,
		      length + 1); // return code ignored
#endif
}

#ifdef YB_GUC_SUPPORT_VIA_SHMEM
/*
 * Clean the shared memory segment which is storing the client's context.
 * A control connection will be used here.
 */
int yb_clean_shmem(od_client_t *client, od_server_t *server)
{
	od_instance_t *instance = client->global->instance;
	od_route_t *route = client->route;
	machine_msg_t *msg;
	int rc = 0;

	msg = kiwi_fe_write_set_client_id(NULL, -client->client_id);

	/* Send `SET SESSION PARAMETER` packet. */
	rc = od_write(&server->io, &msg);
	if (rc == -1) {
		od_debug(&instance->logger, "clean shared memory", client, server,
			 "Unable to send `SET SESSION PARAMETER` packet");
		return -1;
	} else {
		od_debug(&instance->logger, "clean shared memory", client, server,
			 "Sent `SET SESSION PARAMETER` packet for %d", client->client_id);
	}

	client->client_id = 0;

	/* Wait for the KIWI_BE_READY_FOR_QUERY packet. */
	for (;;) {
		msg = od_read(&server->io, UINT32_MAX);
		if (msg == NULL) {
			if (!machine_timedout()) {
				od_error(&instance->logger, "clean shared memory",
					 server->client, server,
					 "read error from server: %s",
					 od_io_error(&server->io));
				return -1;
			}
		}

		kiwi_be_type_t type;
		type = *(char *)machine_msg_data(msg);
		od_debug(&instance->logger, "clean shared memory", server->client,
			 server, "Got a packet of type: %s",
			 kiwi_be_type_to_string(type));

		machine_msg_free(msg);

		if (type == KIWI_BE_READY_FOR_QUERY) {
			return 0;
		} else if (type == KIWI_BE_ERROR_RESPONSE) {
			return -1;
		}
	}
}
#endif

void od_frontend(void *arg)
{
	od_client_t *client = arg;
	od_instance_t *instance = client->global->instance;
	od_router_t *router = client->global->router;
	od_extention_t *extentions = client->global->extentions;
	od_module_t *modules = extentions->modules;

	/* log client connection */
	if (instance->config.log_session) {
		od_getpeername(client->io.io, client->peer,
			       OD_CLIENT_MAX_PEERLEN, 1, 1);
		od_log(&instance->logger, "startup", client, NULL,
		       "new client connection %s", client->peer);
	}

	/* attach client io to worker machine event loop */
	int rc;
	rc = od_io_attach(&client->io);
	if (rc == -1) {
		od_error(&instance->logger, "startup", client, NULL,
			 "failed to transfer client io");
		od_client_free_extended(client);
		od_atomic_u32_dec(&router->clients_routing);
		return;
	}

	rc = machine_io_attach(client->notify_io);
	if (rc == -1) {
		od_error(&instance->logger, "startup", client, NULL,
			 "failed to transfer client notify io");
		od_client_free_extended(client);
		od_atomic_u32_dec(&router->clients_routing);
		return;
	}

	/* ensure global client_max limit */
	uint32_t clients = od_atomic_u32_inc(&router->clients);
	if (instance->config.client_max_set &&
	    clients >= (uint32_t)instance->config.client_max) {
		od_frontend_error(
			client, KIWI_TOO_MANY_CONNECTIONS,
			"too many tcp connections (global client_max %d)",
			instance->config.client_max);
		od_frontend_close(client);
		od_atomic_u32_dec(&router->clients_routing);
		return;
	}

	/* handle startup */
	rc = od_frontend_startup(client);
	if (rc == -1) {
		od_frontend_close(client);
		od_atomic_u32_dec(&router->clients_routing);
		return;
	}

	/* handle cancel request */
	if (client->startup.is_cancel) {
		od_log(&instance->logger, "startup", client, NULL,
		       "cancel request");
		od_router_cancel_t cancel;
		od_router_cancel_init(&cancel);
		rc = od_router_cancel(router, &client->startup.key, &cancel);
		if (rc == 0) {
			od_cancel(client->global, cancel.storage, &cancel.key,
				  &cancel.id);
			od_router_cancel_free(&cancel);
		}
		od_frontend_close(client);
		od_atomic_u32_dec(&router->clients_routing);
		return;
	}

	/* Use client id as backend key for the client.
	 *
	 * This key will be used to identify a server by
	 * user cancel requests. The key must be regenerated
	 * for each new client-server assignment, to avoid
	 * possibility of cancelling requests by a previous
	 * server owners.
	 */
	client->key.key_pid = client->id.id_a;
	client->key.key = client->id.id_b;

	/* pre-auth callback */
	od_list_t *i;
	od_list_foreach(&modules->link, i)
	{
		od_module_t *module;
		module = od_container_of(i, od_module_t, link);
		if (module->auth_attempt_cb(client) ==
		    OD_MODULE_CB_FAIL_RETCODE) {
			goto cleanup;
		}
	}

#ifdef YB_SUPPORT_FOUND
	/* HBA check */
	rc = od_hba_process(client);
#endif

	char client_ip[64];
	od_getpeername(client->io.io, client_ip, sizeof(client_ip), 1, 0);

	/* client authentication */
	if (rc == OK_RESPONSE)
		rc = od_auth_frontend(client);

	if (rc == OK_RESPONSE) {
		od_log(&instance->logger, "auth", client, NULL,
		       "ip '%s' user '%s.%s': host based authentication allowed",
		       client_ip, client->startup.database.value,
		       client->startup.user.value);
	} else {
		od_error(
			&instance->logger, "auth", client, NULL,
			"ip '%s' user '%s.%s': host based authentication rejected",
			client_ip, client->startup.database.value,
			client->startup.user.value);

		/* rc == -1
		 * here we ignore module retcode because auth already failed
		 * we just inform side modules that usr was trying to log in
		 */
		od_list_foreach(&modules->link, i)
		{
			od_module_t *module;
			module = od_container_of(i, od_module_t, link);
			module->auth_complete_cb(client, rc);
		}
		od_atomic_u32_dec(&router->clients_routing);
		goto cleanup;
	}

	/* auth result callback */
	od_list_foreach(&modules->link, i)
	{
		od_module_t *module;
		module = od_container_of(i, od_module_t, link);
		rc = module->auth_complete_cb(client, rc);
		if (rc != OD_MODULE_CB_OK_RETCODE) {
			// user blocked from module callback
			goto cleanup;
		}
	}

	/* route client */
	od_router_status_t router_status;
	router_status = od_router_route(router, client);

	od_route_t *route = (od_route_t *)client->route;
	od_route_lock(route);

	/* If the client's version is greater than existing maximum version
	 * for pool,mark all existing idle and active backends for removal.
	 */

	const char *version_matching_flag =
		getenv("YB_YSQL_CONN_MGR_VERSION_MATCHING");
	version_matching = version_matching_flag != NULL &&
			   strcmp(version_matching_flag, "true") == 0;

	const char *version_matching_connect_higher_version_flag = getenv(
		"YB_YSQL_CONN_MGR_VERSION_MATCHING_CONNECT_HIGHER_VERSION");
	version_matching_connect_higher_version =
		version_matching_connect_higher_version_flag &&
		strcmp(version_matching_connect_higher_version_flag, "true") ==
			0;

	if (version_matching && client->logical_client_version >
	    route->max_logical_client_version) {
		od_debug(
			&instance->logger, "auth backend", client, NULL,
			"invalidate all existing active and idle backends of the route"
			", with user = %s, db = %s, having %d idle backends and %d active backends",
			(char *)route->yb_user_name,
			(char *)route->yb_database_name,
			route->server_pool.count_idle,
			route->server_pool.count_active);
		route->max_logical_client_version =
			client->logical_client_version;
		od_list_t *target = &route->server_pool.idle;
		od_list_t *i, *n;
		od_list_foreach_safe(target, i, n)
		{
			od_server_t *server_idle =
				od_container_of(i, od_server_t, link);
			server_idle->marked_for_close = true;
		}
		target = &route->server_pool.active;
		od_list_foreach_safe(target, i, n)
		{
			od_server_t *server_active =
				od_container_of(i, od_server_t, link);
			server_active->marked_for_close = true;
		}
	}
	od_route_unlock(route);

	/* routing is over */
	od_atomic_u32_dec(&router->clients_routing);

	if (od_likely(router_status == OD_ROUTER_OK)) {
		od_route_t *route = client->route;
		if (route->rule->application_name_add_host) {
			od_application_name_add_host(client);
		}

		/*
		 * YB: We never specify any variable in route->rule->vars,
		 * so this code is not needed and is hence commented out
		//override clients pg options if configured
		rc = kiwi_vars_override(&client->yb_vars_session,
					&route->rule->vars);
		if (rc == -1) {
			goto cleanup;
		}
		 */

		if (instance->config.log_session) {
			od_log(&instance->logger, "startup", client, NULL,
			       "route '%s.%s' to '%s.%s'",
			       client->startup.database.value,
			       client->startup.user.value, route->rule->db_name,
			       route->rule->user_name);
		}
	} else {
		char peer[128];
		od_getpeername(client->io.io, peer, sizeof(peer), 1, 1);

		if (od_router_status_is_err(router_status)) {
			od_error_logger_store_err(router->router_err_logger,
						  router_status);
		}

		switch (router_status) {
		case OD_ROUTER_ERROR:
			od_error(&instance->logger, "startup", client, NULL,
				 "routing failed for '%s' client, closing",
				 peer);
			od_frontend_error(client, KIWI_SYSTEM_ERROR,
					  "client routing failed");
			break;
		case OD_ROUTER_INSUFFICIENT_ACCESS:
			// disabling blind ldapsearch via odyssey error messages
			// to collect user account attributes
			od_error(
				&instance->logger, "startup", client, NULL,
				"route for '%s.%s' is not found by ldapsearch for '%s' client, closing",
				client->startup.database.value,
				client->startup.user.value, peer);
			od_frontend_error(client, KIWI_SYNTAX_ERROR,
					  "incorrect password");
			break;
		case OD_ROUTER_ERROR_NOT_FOUND:
			od_error(
				&instance->logger, "startup", client, NULL,
				"route for '%s.%s' is not found for '%s' client, closing",
				client->startup.database.value,
				client->startup.user.value, peer);
			od_frontend_error(client, KIWI_UNDEFINED_DATABASE,
					  "route for '%s.%s' is not found",
					  client->startup.database.value,
					  client->startup.user.value);
			break;
		case OD_ROUTER_ERROR_LIMIT:
			od_error(
				&instance->logger, "startup", client, NULL,
				"global connection limit reached for '%s' client, closing",
				peer);

			od_frontend_error(
				client, KIWI_TOO_MANY_CONNECTIONS,
				"too many client tcp connections (global client_max)");
			break;
		case OD_ROUTER_ERROR_LIMIT_ROUTE:
			od_error(
				&instance->logger, "startup", client, NULL,
				"route connection limit reached for client '%s', closing",
				peer);
			od_frontend_error(
				client, KIWI_TOO_MANY_CONNECTIONS,
				"too many client tcp connections (client_max for user %s.%s "
				"%d)",
				client->startup.database.value,
				client->startup.user.value,
				client->rule != NULL ?
					client->rule->client_max :
					-1);
			break;
		case OD_ROUTER_ERROR_REPLICATION:
			od_error(
				&instance->logger, "startup", client, NULL,
				"invalid value for parameter \"replication\" for client '%s'",
				peer);

			od_frontend_error(
				client, KIWI_CONNECTION_FAILURE,
				"invalid value for parameter \"replication\"");
			break;
		default:
			assert(0);
			break;
		}

		od_frontend_close(client);
		return;
	}

	/* setup client and run main loop */
	route = client->route;

	od_frontend_status_t status;
	status = OD_UNDEF;
	switch (route->rule->storage->storage_type) {
	case OD_RULE_STORAGE_LOCAL: {
		status = od_frontend_local_setup(client);
		if (status != OD_OK)
			break;

		status = od_frontend_local(client);
		break;
	}
	case OD_RULE_STORAGE_REMOTE: {
		status = od_frontend_setup(client);
		if (status != OD_OK)
			break;

		status = od_frontend_remote(client);
		break;
	}
	}
	od_error_logger_t *l;
	l = router->route_pool.err_logger;

	od_frontend_cleanup(client, "main", status, l);

	od_list_foreach(&modules->link, i)
	{
		od_module_t *module;
		module = od_container_of(i, od_module_t, link);
		module->disconnect_cb(client, status);
	}

	/* cleanup */

cleanup:
#ifdef YB_GUC_SUPPORT_VIA_SHMEM
	/* clean shared memory associated with the client */
	if (client->client_id != 0)
		yb_execute_on_control_connection(client, yb_clean_shmem);
#endif

	/* detach client from its route */
	if (client->route != NULL)
		od_router_unroute(router, client);

	/* close frontend connection */
	od_frontend_close(client);
}

int yb_execute_on_control_connection(od_client_t *client,
				     int (*function)(od_client_t *,
						     od_server_t *))
{
	od_global_t *global = client->global;
	kiwi_var_t *user = &client->startup.user;
	od_instance_t *instance = global->instance;
	od_router_t *router = global->router;

	/* internal client */
	od_client_t *control_conn_client;
	control_conn_client =
		od_client_allocate_internal(global, "control connection");
	if (control_conn_client == NULL) {
		od_debug(
			&instance->logger, "control connection",
			control_conn_client, NULL,
			"failed to allocate internal client for the control connection");
		goto failed_to_acquire_control_connection;
	}

	if (client->startup.replication.value_len != 0) {
		yb_kiwi_var_set(&control_conn_client->startup.replication,
			client->startup.replication.value,
			client->startup.replication.value_len);
	}

	/* set control connection route user and database */
#ifndef YB_GUC_SUPPORT_VIA_SHMEM
	yb_kiwi_var_set(&control_conn_client->startup.user,
		     "control_connection_user", 24);
	yb_kiwi_var_set(&control_conn_client->startup.database,
		     "control_connection_db", 22);
#else
	kiwi_var_set(&control_conn_client->startup.user, KIWI_VAR_UNDEF,
		     "control_connection_user", 24);
	kiwi_var_set(&control_conn_client->startup.database, KIWI_VAR_UNDEF,
		     "control_connection_db", 22);
#endif

	/* route */
	od_router_status_t status;
	status = od_router_route(router, control_conn_client);
	if (status != OD_ROUTER_OK) {
		od_debug(
			&instance->logger, "control connection query",
			control_conn_client, NULL,
			"failed to route internal client for control connection: %s",
			od_router_status_to_str(status));

		od_client_free_extended(control_conn_client);
		goto failed_to_acquire_control_connection;
	}

	/* attach */
	status = od_router_attach(router, control_conn_client, false, client);
	if (status != OD_ROUTER_OK) {
		od_debug(
			&instance->logger, "control connection",
			control_conn_client, NULL,
			"failed to attach internal client for control connection to route: %s",
			od_router_status_to_str(status));
		od_router_unroute(router, control_conn_client);
		od_client_free_extended(control_conn_client);
		goto failed_to_acquire_control_connection;
	}

	od_server_t *server;
	server = control_conn_client->server;

	od_debug(&instance->logger, "control connection", control_conn_client,
		 server, "attached to server %s%.*s", server->id.id_prefix,
		 (int)sizeof(server->id.id), server->id.id);

	int rc = -1;

	/*
	 * YB: check for open socket here. Exit if closed.
	 * This check is an auth-specific check to allow early exit
	 * in case a client times out during authentication, where we
	 * skip requisitioning a physical backend to save time
	 * (and avoid a "cascading timeout" situation).
	 */
	if (yb_machine_io_is_socket_closed(client->io.io)) {
		od_debug(
			&instance->logger, "control connection", client, NULL,
			"socket is closed. Queued client timed out. Aborting auth");
		rc = NOT_OK_RESPONSE;
		goto cleanup;
	}

	/* connect to server, if necessary */
	if (server->io.io == NULL) {
		rc = od_backend_connect(server, "control connection", NULL,
					control_conn_client);
		if (rc == NOT_OK_RESPONSE) {
			/* [#28252] TODO: Merge the code below into the cleanup label */
			od_debug(&instance->logger, "control connection",
				 control_conn_client, server,
				 "failed to acquire backend connection: %s",
				 od_io_error(&server->io));
			od_router_close(router, control_conn_client);
			od_router_unroute(router, control_conn_client);
			od_client_free_extended(control_conn_client);
			goto failed_to_acquire_control_connection;
		}
	}

	rc = function(client, server);

	/*
	 * close the backend connection as we don't want to reuse machines in this
	 * pool if auth-backend is enabled.
	 */

cleanup:
	if (instance->config.yb_use_auth_backend)
		server->offline = true;
	od_router_detach(router, control_conn_client);
	od_router_unroute(router, control_conn_client);
	od_client_free_extended(control_conn_client);

	if (rc == -1)
		return NOT_OK_RESPONSE;

	return OK_RESPONSE;

failed_to_acquire_control_connection:
	od_frontend_fatal(client, KIWI_CONNECTION_FAILURE,
			  "failed to connect to remote server");
	return NOT_OK_RESPONSE;
}

static inline int yb_od_frontend_error_fwd(od_client_t *client)
{
	od_server_t *server = client->server;
	assert(server != NULL);
	assert(server->error_connect != NULL);
	kiwi_fe_error_t error;
	int rc;
	rc = kiwi_fe_read_error(machine_msg_data(server->error_connect),
				machine_msg_size(server->error_connect),
				&error);
	if (rc == -1)
		return -1;
	int detail_len = error.detail ? strlen(error.detail) : 0;
	int hint_len = error.hint ? strlen(error.hint) : 0;

	machine_msg_t *msg;
	msg = kiwi_be_write_error_as(NULL, error.severity, error.code,
				     error.detail, detail_len, error.hint,
				     hint_len, error.message, strlen(error.message));
	if (msg == NULL)
		return -1;
	return od_write(&client->io, &msg);
}

int yb_auth_via_auth_backend(od_client_t *client)
{
	od_global_t *global = client->global;
	kiwi_var_t *user = &client->startup.user;
	od_instance_t *instance = global->instance;
	od_router_t *router = global->router;

	assert(instance->config.yb_use_auth_backend);

	od_debug(
		&instance->logger, "auth backend",
		client, NULL,
		"yb_auth_via_auth_backend started");

	/* internal client */
	od_client_t *control_conn_client;
	control_conn_client =
		od_client_allocate_internal(global, "auth backend");
	if (control_conn_client == NULL) {
		od_debug(
			&instance->logger, "auth backend",
			client, NULL,
			"failed to allocate internal client for the auth backend");
		goto failed_to_acquire_auth_backend;
	}

	if (client->startup.replication.value_len != 0) {
		yb_kiwi_var_set(&control_conn_client->startup.replication,
			client->startup.replication.value,
			client->startup.replication.value_len);
	}

	/*
	 * Set control connection route user and database. The auth-backend is
	 * created from the control pool, so these values are set so that the pool
	 * gets matched in the below call to od_router_route.
	 */
#ifndef YB_GUC_SUPPORT_VIA_SHMEM
	yb_kiwi_var_set(&control_conn_client->startup.user,
		     "control_connection_user", 24);
	yb_kiwi_var_set(&control_conn_client->startup.database,
		     "control_connection_db", 22);
#else
	kiwi_var_set(&control_conn_client->startup.user, KIWI_VAR_UNDEF,
		     "control_connection_user", 24);
	kiwi_var_set(&control_conn_client->startup.database, KIWI_VAR_UNDEF,
		     "control_connection_db", 22);
#endif

	/* route */
	od_router_status_t status;
	status = od_router_route(router, control_conn_client);
	if (status != OD_ROUTER_OK) {
		od_debug(
			&instance->logger, "auth backend query",
			control_conn_client, NULL,
			"failed to route internal client for auth backend: %s",
			od_router_status_to_str(status));

		od_client_free_extended(control_conn_client);
		goto failed_to_acquire_auth_backend;
	}

	/* attach */
	status = od_router_attach(router, control_conn_client, false, client);
	if (status != OD_ROUTER_OK) {
		od_debug(
			&instance->logger, "auth backend",
			control_conn_client, NULL,
			"failed to attach internal client for auth backend to route: %s",
			od_router_status_to_str(status));
		od_router_unroute(router, control_conn_client);
		od_client_free_extended(control_conn_client);
		goto failed_to_acquire_auth_backend;
	}

	od_server_t *server;
	server = control_conn_client->server;
	server->yb_auth_backend = true;

	od_debug(&instance->logger, "auth backend", control_conn_client,
		 server, "attached to auth backend %s%.*s", server->id.id_prefix,
		 (int)sizeof(server->id.id), server->id.id);
	
	int rc;

	/*
	 * YB: check for open socket here. Exit if closed.
	 * This check is an auth-specific check to allow early exit
	 * in case a client times out during authentication, where we
	 * skip requisitioning a physical backend to save time
	 * (and avoid a "cascading timeout" situation).
	 */
	if (yb_machine_io_is_socket_closed(client->io.io)) {
		od_debug(&instance->logger, "auth backend",
			client, NULL,
			"socket is closed. Queued client timed out. Aborting auth");
		rc = NOT_OK_RESPONSE;
		goto cleanup;
	}

	/*
	 * Set the client user and database for authentication. Once, the control
	 * pool is selected for the backend, we can now set these values to the
	 * actual user and database values since we have to send them to the
	 * auth-backend.
	 */
#ifndef YB_GUC_SUPPORT_VIA_SHMEM
	yb_kiwi_var_set(&control_conn_client->startup.user,
		     client->startup.user.value, client->startup.user.value_len);
	yb_kiwi_var_set(&control_conn_client->startup.database,
		     client->startup.database.value, client->startup.database.value_len);
#else
	kiwi_var_set(&control_conn_client->startup.user, KIWI_VAR_UNDEF,
		     client->startup.user.value, client->startup.user.value_len);
	kiwi_var_set(&control_conn_client->startup.database, KIWI_VAR_UNDEF,
		     client->startup.database.value, client->startup.database.value_len);
#endif

	/* connect to server */
	assert(server->io.io == NULL);
	control_conn_client->yb_external_client = client;
	control_conn_client->yb_is_authenticating = true;
	od_getpeername(client->io.io, control_conn_client->yb_client_address,
		       sizeof(control_conn_client->yb_client_address), 1, 0);
	rc = od_backend_connect(server, "auth backend", NULL,
							control_conn_client);
	/*Store the client's logical_client_version as auth backend's logical_client_version*/
	od_debug(&instance->logger, "auth backend", control_conn_client, server,
		 "auth's backend logical client version = %d", server->logical_client_version);
	client->logical_client_version = server->logical_client_version;

	control_conn_client->yb_is_authenticating = false;
	if (rc == NOT_OK_RESPONSE) {
		od_debug(&instance->logger, "auth backend",
				 control_conn_client, server,
				 "failed to acquire auth-backend connection: %s",
				 od_io_error(&server->io));
		goto cleanup;
	}

	/* Set the value of the user_oid and db_oid received from the auth backend. */
	client->yb_db_oid = control_conn_client->yb_db_oid;
	client->yb_user_oid = control_conn_client->yb_user_oid;

cleanup:
	/* Send any saved errors to the client. */
	if (server->error_connect)
	{
		client->server = server;
		yb_od_frontend_error_fwd(client);
		client->server = NULL;
	}

	/*
	 * close the backend connection as we don't want to reuse machines in this
	 * pool.
	 */
	server->offline = true;
	od_router_detach(router, control_conn_client);
	od_router_unroute(router, control_conn_client);
	od_client_free_extended(control_conn_client);

	if (rc == NOT_OK_RESPONSE) {
		od_frontend_fatal(client, KIWI_CONNECTION_FAILURE,
				"failed to connect to remote server");
		return NOT_OK_RESPONSE;
	}

	return OK_RESPONSE;

failed_to_acquire_auth_backend:
	od_frontend_fatal(client, KIWI_CONNECTION_FAILURE,
			  "failed to connect to remote server");
	return NOT_OK_RESPONSE;
}
