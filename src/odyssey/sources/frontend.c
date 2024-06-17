
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

static inline void od_frontend_close(od_client_t *client)
{
	assert(client->route == NULL);
	assert(client->server == NULL);

	od_router_t *router = client->global->router;
	od_atomic_u32_dec(&router->clients);

	od_io_close(&client->io);
	if (client->notify_io) {
		machine_close(client->notify_io);
		machine_io_free(client->notify_io);
		client->notify_io = NULL;
	}
	od_client_free(client);
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
	return od_write(&client->io, msg);
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
	return od_write(&client->io, msg);
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
	return od_write(&client->io, msg);
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
	return od_write(&client->io, msg);
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
	return od_write(&client->io, msg);
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

		int rc = kiwi_be_read_startup(machine_msg_data(msg),
					      machine_msg_size(msg),
					      &client->startup, &client->vars);
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
		rc = od_write(&client->io, msg);
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
				  &client->startup, &client->vars);
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

			if (yb_frontend_error_is_db_does_not_exist(client)) {
				yb_resolve_db_status(server->global,
					((od_route_t *)server->route)->yb_database_entry,
					NULL);

				if (((od_route_t *)server->route)
					    ->yb_database_entry->status == YB_DB_ACTIVE) {
					od_router_close(router, client);
					continue;
				}
			}

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

static inline od_frontend_status_t od_frontend_setup_params(od_client_t *client)
{
	od_instance_t *instance = client->global->instance;
	od_router_t *router = client->global->router;
	od_route_t *route = client->route;

	/* ensure route has cached server parameters */
	int rc;
	rc = kiwi_params_lock_count(&route->params);
	if (rc == 0) {
		kiwi_params_t route_params;
		kiwi_params_init(&route_params);

		od_frontend_status_t status;
		status = od_frontend_attach(client, "setup", &route_params);
		if (status != OD_OK) {
			kiwi_params_free(&route_params);
			return status;
		}

		// close backend connection
		od_router_close(router, client);

		/* There is possible race here, so we will discard our
		 * attempt if params are already set */
		rc = kiwi_params_lock_set_once(&route->params, &route_params);
		if (!rc)
			kiwi_params_free(&route_params);
	}

	od_debug(&instance->logger, "setup", client, NULL, "sending params:");

	/* send parameters set by client or cached by the route */
	kiwi_param_t *param = route->params.params.list;

	machine_msg_t *stream = machine_msg_create(0);
	if (stream == NULL)
		return OD_EOOM;

	while (param) {
#ifndef YB_GUC_SUPPORT_VIA_SHMEM
		kiwi_var_t *var;
		var = yb_kiwi_vars_get(&client->vars, kiwi_param_name(param));
#else
		kiwi_var_type_t type;
		type = kiwi_vars_find(&client->vars, kiwi_param_name(param),
				      param->name_len);
		kiwi_var_t *var;
		var = kiwi_vars_get(&client->vars, type);
#endif

		machine_msg_t *msg;
		if (var) {
			msg = kiwi_be_write_parameter_status(stream, var->name,
							     var->name_len,
							     var->value,
							     var->value_len);

			od_debug(&instance->logger, "setup", client, NULL,
				 " %.*s = %.*s", var->name_len, var->name,
				 var->value_len, var->value);
		} else {
			msg = kiwi_be_write_parameter_status(
				stream, kiwi_param_name(param), param->name_len,
				kiwi_param_value(param), param->value_len);

			od_debug(&instance->logger, "setup", client, NULL,
				 " %.*s = %.*s", param->name_len,
				 kiwi_param_name(param), param->value_len,
				 kiwi_param_value(param));
		}
		if (msg == NULL) {
			machine_msg_free(stream);
			return OD_EOOM;
		}

		param = param->next;
	}

	rc = od_write(&client->io, stream);
	if (rc == -1)
		return OD_ECLIENT_WRITE;

	return OD_OK;
}

static inline od_frontend_status_t od_frontend_setup(od_client_t *client)
{
	od_instance_t *instance = client->global->instance;
	od_route_t *route = client->route;

	/* set paremeters */
	od_frontend_status_t status;
	status = od_frontend_setup_params(client);
	if (status != OD_OK)
		return status;

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
	rc = od_write(&client->io, stream);
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
	rc = od_write(&client->io, stream);
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

		rc = od_write(&client->io, stream);
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
	switch (type) {
	case KIWI_BE_ERROR_RESPONSE:
		od_backend_error(server, "main", data, size);
		break;
	/* fallthrough */
	case YB_ROLE_OID_PARAMETER_STATUS:
	case KIWI_BE_PARAMETER_STATUS:
		rc = od_backend_update_parameter(server, "main", data, size, 0);
		if (rc == -1)
			return relay->error_read;
		break;
	case KIWI_BE_COPY_IN_RESPONSE:
	case KIWI_BE_COPY_OUT_RESPONSE:
		server->is_copy = 1;
		break;
	case KIWI_BE_COPY_DONE:
		server->is_copy = 0;
		break;
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
	case KIWI_BE_PARSE_COMPLETE:
		if (route->rule->pool->reserve_prepared_statement) {
			// skip msg
			is_deploy = 1;
		}
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
	if (is_deploy)
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

static od_frontend_status_t od_frontend_remote_client(od_relay_t *relay,
						      char *data, int size)
{
	od_client_t *client = relay->on_packet_arg;
	od_instance_t *instance = client->global->instance;
	(void)size;
	od_route_t *route = client->route;
	assert(route != NULL);

	int prev_named_prep_stmt = 1;

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
	machine_msg_t *msg = NULL;
	bool forwarded = 0;
	switch (type) {
	case KIWI_FE_COPY_DONE:
	case KIWI_FE_COPY_FAIL:
		server->is_copy = 0;
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

			assert(client->prep_stmt_ids);
			retstatus = OD_SKIP;
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

			od_debug(&instance->logger, "rewrite describe", client,
				 server, "statement: %.*s, hash: %08x",
				 desc->len, desc->data, body_hash);

			char opname[OD_HASH_LEN];
			od_snprintf(opname, OD_HASH_LEN, "%08x", body_hash);

			int refcnt = 0;
			od_hashmap_elt_t value;
			value.data = &refcnt;
			value.len = sizeof(int);
			od_hashmap_elt_t *value_ptr = &value;

			// send parse msg if needed
			if (od_hashmap_insert(server->prep_stmts, body_hash,
					      desc, &value_ptr) == 0) {
				od_debug(
					&instance->logger,
					"rewrite parse before describe", client,
					server,
					"deploy %.*s operator hash %u to server",
					desc->len, desc->data, keyhash);
				// rewrite msg
				// allocate prepered statement under name equal to body hash

				msg = kiwi_fe_write_parse_description(
					NULL, opname, OD_HASH_LEN, desc->data,
					desc->len);
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
				rc = od_write(&server->io, msg);
				if (rc == -1) {
					od_error(&instance->logger, "describe",
						 NULL, server,
						 "write error: %s",
						 od_io_error(&server->io));
					return OD_ESERVER_WRITE;
				}
			} else {
				int *refcnt;
				refcnt = value_ptr->data;
				*refcnt = 1 + *refcnt;
			}

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
			rc = od_write(&server->io, msg);
			forwarded = 1;
			if (rc == -1) {
				od_error(&instance->logger, "describe", NULL,
					 server, "write error: %s",
					 od_io_error(&server->io));
				return OD_ESERVER_WRITE;
			}
		}
		break;
	case KIWI_FE_PARSE:
		if (instance->config.log_query || route->rule->log_query)
			od_frontend_log_parse(instance, client, "parse", data,
					      size);

		if (route->rule->pool->reserve_prepared_statement) {
			/* skip client parse msg */
			retstatus = OD_SKIP;
			kiwi_prepared_statement_t desc;
			int rc;
			rc = kiwi_be_read_parse_dest(data, size, &desc);
			if (rc) {
				return OD_ECLIENT_READ;
			}

			if (desc.operator_name[0] == '\0') {
				/* no need for odyssey to track unnamed prepared statements */
				prev_named_prep_stmt = 0;
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

			assert(client->prep_stmt_ids);
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
					rc = od_write(&server->io, msg);
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

			key.len = desc.description_len;
			key.data = desc.description;

			int refcnt = 0;
			value.data = &refcnt;
			value.len = sizeof(int);

			value_ptr = &value;

			if (od_hashmap_insert(server->prep_stmts, body_hash,
					      &key, &value_ptr) == 0) {
				od_debug(
					&instance->logger,
					"rewrite parse initial deploy", client,
					server,
					"deploy %.*s operator hash %u to server",
					key.len, key.data, keyhash);
				// rewrite msg
				// allocate prepered statement under name equal to body hash

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
				rc = od_write(&server->io, msg);
				if (rc == -1) {
					od_error(&instance->logger, "parse",
						 NULL, server,
						 "write error: %s",
						 od_io_error(&server->io));
					return OD_ESERVER_WRITE;
				}
			} else {
				int *refcnt = value_ptr->data;
				*refcnt = 1 + *refcnt;

				if (instance->config.log_query ||
				    route->rule->log_query) {
					od_stat_parse_reuse(&route->stats);
					od_log(&instance->logger, "parse",
					       client, server,
					       "stmt already exists, simply report its ok");
				}
				machine_msg_free(msg);
			}

			machine_msg_t *pmsg;
			pmsg = kiwi_be_write_parse_complete(NULL);
			if (pmsg == NULL) {
				return OD_ESERVER_WRITE;
			}
			rc = od_write(&client->io, pmsg);
			forwarded = 1;

			if (rc == -1) {
				od_error(&instance->logger, "parse", client,
					 NULL, "write error: %s",
					 od_io_error(&client->io));
				return OD_ESERVER_WRITE;
			}
		}
		break;
	case KIWI_FE_BIND:
		if (instance->config.log_query || route->rule->log_query)
			od_frontend_log_bind(instance, client, "bind", data,
					     size);

		if (route->rule->pool->reserve_prepared_statement) {
			retstatus = OD_SKIP;
			uint32_t operator_name_len;
			char *operator_name;

			int rc;
			rc = kiwi_be_read_bind_stmt_name(
				data, size, &operator_name, &operator_name_len);

			if (rc == -1) {
				return OD_ECLIENT_READ;
			}

			/* unnamed prepared statement, ignore processing of the packet */
			if (operator_name[0] == '\0') {
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

			od_debug(&instance->logger, "rewrite bind", client,
				 server, "statement: %.*s, hash: %08x",
				 desc->len, desc->data, body_hash);

			od_hashmap_elt_t value;
			int refcnt = 1;
			value.data = &refcnt;
			value.len = sizeof(int);
			od_hashmap_elt_t *value_ptr = &value;

			char opname[OD_HASH_LEN];
			od_snprintf(opname, OD_HASH_LEN, "%08x", body_hash);

			if (od_hashmap_insert(server->prep_stmts, body_hash,
					      desc, &value_ptr) == 0) {
				od_debug(
					&instance->logger,
					"rewrite parse before bind", client,
					server,
					"deploy %.*s operator hash %u to server",
					desc->len, desc->data, keyhash);
				// rewrite msg
				// allocate prepered statement under name equal to body hash

				msg = kiwi_fe_write_parse_description(
					NULL, opname, OD_HASH_LEN, desc->data,
					desc->len);

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
				rc = od_write(&server->io, msg);
				if (rc == -1) {
					od_error(&instance->logger,
						 "rewrite parse", NULL, server,
						 "write error: %s",
						 od_io_error(&server->io));
					return OD_ESERVER_WRITE;
				}
			} else {
				int *refcnt = value_ptr->data;
				*refcnt = 1 + *refcnt;
			}

			msg = od_frontend_rewrite_msg(data, size,
						      opname_start_offset,
						      operator_name_len,
						      body_hash);

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

			rc = od_write(&server->io, msg);
			forwarded = 1;

			if (rc == -1) {
				od_error(&instance->logger, "rewrite bind",
					 NULL, server, "write error: %s",
					 od_io_error(&server->io));
				return OD_ESERVER_WRITE;
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
			forwarded = 1;

			if (od_frontend_parse_close(data, size, &name,
						    &name_len,
						    &type) != OK_RESPONSE) {
				return OD_ESERVER_WRITE;
			}

			if (type == KIWI_FE_CLOSE_PREPARED_STATEMENT) {
				retstatus = OD_SKIP;
				od_debug(
					&instance->logger,
					"ingore closing prepared statement, report its closed",
					client, server, "statement: %.*s",
					name_len, name);

				machine_msg_t *pmsg;
				pmsg = kiwi_be_write_close_complete(NULL);

				rc = od_write(&client->io, pmsg);
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

	/* If the retstatus is not SKIP */
	if (route->rule->pool->reserve_prepared_statement && forwarded != 1) {
		msg = kiwi_fe_copy_msg(msg, data, size);
		int rc = od_write(&server->io, msg);
		if (rc == -1) {
			od_error(&instance->logger, "error while forwarding",
				client, server, "Got error while forwarding the packet");
			return OD_ESERVER_WRITE;
		}

		/* unnamed prepared statement was parsed, send ParseComplete to client */
		if (prev_named_prep_stmt == 0) {

			machine_msg_t *pcmsg;
			pcmsg = kiwi_be_write_parse_complete(NULL);

			if (pcmsg == NULL) {
				return OD_ESERVER_WRITE;
			}

			rc = od_write(&client->io, pcmsg);

			if (rc == -1) {
				od_error(&instance->logger, "parse", client,
					 NULL, "write error: %s",
					 od_io_error(&client->io));
				return OD_ESERVER_WRITE;
			}
		}

		retstatus = OD_SKIP;
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
			if (yb_is_route_invalid(client->route)) {
				od_frontend_fatal(
					client, KIWI_CONNECTION_FAILURE,
					"Database might have been dropped by another user");
				status = OD_ECLIENT_READ;
				break;
			}

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
			kiwi_var_t *timeout_var =
				yb_kiwi_vars_get(&client->vars, "odyssey_catchup_timeout");
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
			status = od_frontend_attach_and_deploy(client, "main");
			if (status != OD_OK)
				break;
			server = client->server;
			status = od_relay_start(
				&server->relay, client->cond, OD_ESERVER_READ,
				OD_ECLIENT_WRITE,
				od_frontend_remote_server_on_read,
				&route->stats, od_frontend_remote_server,
				client, reserve_session_server_connection);
			if (status != OD_OK)
				break;
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

		if (yb_is_route_invalid(client->route)) {
			if (client->type == OD_POOL_CLIENT_EXTERNAL)
				od_frontend_fatal(
					client, KIWI_CONNECTION_FAILURE,
					"Database might have been dropped by another user");
			return;
		}

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
		rc = od_reset(server);
		if (rc != 1) {
			/* close backend connection */
			od_log(&instance->logger, context, client, server,
			       "reset unsuccessful, closing server connection");
			od_router_close(router, client);
			break;
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
				"deploy error: %s, status %s", client->deploy_err,
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
	if (client == NULL || client->io.io == NULL)
		return;
	char app_name_with_host[KIWI_MAX_VAR_SIZE];
	char peer_name[KIWI_MAX_VAR_SIZE];
	int app_name_len = 7;
	char *app_name = "unknown";
#ifndef YB_GUC_SUPPORT_VIA_SHMEM
	kiwi_var_t *app_name_var =
		yb_kiwi_vars_get(&client->vars, "application_name");
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
	kiwi_vars_update(&client->vars, "application_name", 17,
		      app_name_with_host, length + 1); // return code ignored
#else
	kiwi_vars_set(&client->vars, KIWI_VAR_APPLICATION_NAME,
		      app_name_with_host,
		      length + 1); // return code ignored
#endif
}

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
	rc = od_write(&server->io, msg);
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
		od_io_close(&client->io);
		machine_close(client->notify_io);
		od_client_free(client);
		od_atomic_u32_dec(&router->clients_routing);
		return;
	}

	rc = machine_io_attach(client->notify_io);
	if (rc == -1) {
		od_error(&instance->logger, "startup", client, NULL,
			 "failed to transfer client notify io");
		od_io_close(&client->io);
		machine_close(client->notify_io);
		od_client_free(client);
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
	if (rc == OK_RESPONSE) {
		rc = od_auth_frontend(client);
		od_log(&instance->logger, "auth", client, NULL,
		       "ip '%s' user '%s.%s': host based authentication allowed",
		       client_ip, client->startup.database.value,
		       client->startup.user.value);
	} else {
/* For auth passthrough, error message will be directly forwaded to the client */
#ifndef YB_SUPPORT_FOUND
		od_error(
			&instance->logger, "auth", client, NULL,
			"ip '%s' user '%s.%s': host based authentication rejected",
			client_ip, client->startup.database.value,
			client->startup.user.value);
		od_frontend_error(client, KIWI_INVALID_PASSWORD,
				  "host based authentication rejected");
#endif
	}

	if (rc != OK_RESPONSE) {
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

	/* routing is over */
	od_atomic_u32_dec(&router->clients_routing);

	if (od_likely(router_status == OD_ROUTER_OK)) {
		od_route_t *route = client->route;
		if (route->rule->application_name_add_host) {
			od_application_name_add_host(client);
		}

		//override clients pg options if configured
		rc = kiwi_vars_override(&client->vars, &route->rule->vars);
		if (rc == -1) {
			goto cleanup;
		}

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
	od_route_t *route = client->route;

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
#ifdef YB_SUPPORT_FOUND
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

		if (control_conn_client->io.io) {
			machine_close(control_conn_client->io.io);
			machine_io_free(control_conn_client->io.io);
		}
		od_client_free(control_conn_client);
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
		if (control_conn_client->io.io) {
			machine_close(control_conn_client->io.io);
			machine_io_free(control_conn_client->io.io);
		}
		od_client_free(control_conn_client);
		goto failed_to_acquire_control_connection;
	}

	od_server_t *server;
	server = control_conn_client->server;

	od_debug(&instance->logger, "control connection", control_conn_client,
		 server, "attached to server %s%.*s", server->id.id_prefix,
		 (int)sizeof(server->id.id), server->id.id);

	/* connect to server, if necessary */
	int rc;
	if (server->io.io == NULL) {
		rc = od_backend_connect(server, "control connection", NULL,
					control_conn_client);
		if (rc == NOT_OK_RESPONSE) {
			od_debug(&instance->logger, "control connection",
				 control_conn_client, server,
				 "failed to acquire backend connection: %s",
				 od_io_error(&server->io));
			od_router_close(router, control_conn_client);
			od_router_unroute(router, control_conn_client);
			if (control_conn_client->io.io) {
				machine_close(control_conn_client->io.io);
				machine_io_free(control_conn_client->io.io);
			}
			od_client_free(control_conn_client);
			goto failed_to_acquire_control_connection;
		}
	}

	rc = function(client, server);

	/* detach and unroute */
	od_router_detach(router, control_conn_client);
	od_router_unroute(router, control_conn_client);
	if (control_conn_client->io.io) {
		machine_close(control_conn_client->io.io);
		machine_io_free(control_conn_client->io.io);
	}
	od_client_free(control_conn_client);

	if (rc == -1)
		return -1;

	return OK_RESPONSE;

failed_to_acquire_control_connection:
	od_frontend_fatal(client, KIWI_CONNECTION_FAILURE,
			  "failed to connect to remote server");
	return NOT_OK_RESPONSE;
}
