
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <arpa/inet.h>
#include <assert.h>

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

#define YB_SHMEM_KEY_FORMAT "shmkey="

void od_backend_close(od_server_t *server)
{
	assert(server->route == NULL);
	assert(server->io.io == NULL);
	assert(server->tls == NULL);
	server->is_transaction = 0;
	server->yb_sticky_connection = false;
	server->reset_timeout = false;
	server->idle_time = 0;
	kiwi_key_init(&server->key);
	kiwi_key_init(&server->key_client);
	od_server_free(server);
}

static inline int od_backend_terminate(od_server_t *server)
{
	machine_msg_t *msg;
	msg = kiwi_fe_write_terminate(NULL);
	if (msg == NULL)
		return -1;
	return od_write(&server->io, &msg);
}

void od_backend_close_connection(od_server_t *server)
{
	/* YB NOTE: Log the type of the backend being closed. */
	od_instance_t *instance = server->global->instance;
	od_debug(&instance->logger, "backend", NULL, server,
		"closing %s backend connection",
		server->yb_auth_backend ? "auth" : "regular");

	/* failed to connect to endpoint, so notring to do */
	if (server->io.io == NULL) {
		/* YB NOTE: Cleanup error_connect and tls even if we cannot connect */
		goto cleanup;
	}
	if (machine_connected(server->io.io))
		od_backend_terminate(server);

	od_io_close(&server->io);

cleanup:
	if (server->error_connect) {
		machine_msg_free(server->error_connect);
		server->error_connect = NULL;
	}

	if (server->tls) {
		machine_tls_free(server->tls);
		server->tls = NULL;
	}
}

void od_backend_evict_server_hashmap(od_server_t *server, char *context, char *data, 
		uint32_t size)
{
	od_instance_t *instance = server->global->instance;
	od_debug(&instance->logger, context, NULL, server, "evicting hashmap entry from server");

	char *stmt_name;
	uint32_t stmt_name_len;
	int rc = kiwi_fe_read_parse_error_yb(data, size, &stmt_name, &stmt_name_len);
	if (rc == -1) {
		od_error(&instance->logger, context, NULL, server, 
			"failed to parse error message from server");
		return;
	}
	od_hash_t keyhash = strtoul(stmt_name, NULL, 16);
	if (yb_od_hashmap_find_key_and_remove(server->prep_stmts, keyhash)) {
		od_debug(&instance->logger, context, NULL, server, 
			"Evicted %u hashmap entry from server", keyhash);
	}
	else {
		od_error(&instance->logger, context, NULL, server, 
			"failed to evict %u hashmap entry from server", keyhash);
	}
}

void od_backend_error(od_server_t *server, char *context, char *data,
		      uint32_t size)
{
	od_instance_t *instance = server->global->instance;
	kiwi_fe_error_t error;
	int detail_len = 0;
	int hint_len = 0;

	od_client_t *yb_server_client = server->client;
	od_client_t *yb_external_client = (yb_server_client->yb_is_authenticating) ?
					 yb_server_client->yb_external_client :
					 yb_server_client;

	int rc;
	rc = kiwi_fe_read_error(data, size, &error);
	if (rc == -1) {
		od_error(&instance->logger, context, yb_external_client, server,
			 "failed to parse error message from server");
		return;
	}

	od_error(&instance->logger, context, yb_external_client, server, "%s %s %s",
		 error.severity, error.code, error.message);

	if (error.detail) {
		od_error(&instance->logger, context, yb_external_client, server,
			 "DETAIL: %s", error.detail);
		detail_len = strlen(error.detail);
	}

	/* catch and store error to be forwarded later if we are in deploy phase */
	if (od_server_in_deploy(server)) {
		od_client_t* client = (od_client_t*) (yb_external_client);
		client->deploy_err = (kiwi_fe_error_t *) malloc(sizeof(kiwi_fe_error_t));
		kiwi_fe_read_error(data, size, client->deploy_err);
	}

	if (error.hint) {
		od_error(&instance->logger, context, yb_external_client, server,
			 "HINT: %s", error.hint);
		hint_len = strlen(error.hint);

		if (strcmp(error.hint, "Database might have been dropped by another user") == 0)
		{
			/* Reset the route and close the client */
			yb_mark_routes_inactive(server->global->router,
							  ((od_route_t*)server->route)->id.yb_db_oid, -1);

			if (yb_external_client != NULL &&
			    ((od_client_t *)yb_external_client)->type ==
				    OD_POOL_CLIENT_EXTERNAL)
			{
				machine_msg_t *msg;
				msg = kiwi_be_write_error_as(NULL, error.severity, error.code,
				     error.detail, detail_len, error.hint,
				     hint_len, error.message, strlen(error.message));
				/* YB: best-effort forward to client, already handling error */
				if (msg == NULL)
					return;
				od_write(&yb_external_client->io, &msg);
			}
		}
	}

	if (strstr(error.message, "invalid role OID")) {
		/* Reset the route and close the client */
		yb_mark_routes_inactive(server->global->router, -1,
						  ((od_route_t*)server->route)->id.yb_user_oid);

		if (yb_external_client != NULL &&
			((od_client_t *)yb_external_client)->type == OD_POOL_CLIENT_EXTERNAL)
			{
				machine_msg_t *msg;
				msg = kiwi_be_write_error_as(NULL, error.severity, error.code,
				     error.detail, detail_len, error.hint,
				     hint_len, error.message, strlen(error.message));
				/* YB: best-effort forward to client, already handling error */
				if (msg == NULL)
					return;
				od_write(&yb_external_client->io, &msg);
			}	
		}
}

int od_backend_ready(od_server_t *server, char *data, uint32_t size)
{
	int status;
	int rc;
	rc = kiwi_fe_read_ready(data, size, &status);
	if (rc == -1)
		return -1;

	// We piggyback the stickiness bit 'i' from the response message
	// to be absorbed by odyssey, which then in turn makes the connection
	// sticky, and then assumes the usual code workflow of being outside the 
	// transaction block.
	if (status == 'I' || status == 'i') {
		if (status == 'i') {
			/* increment only if becoming sticky for the first time */
			if (!server->yb_sticky_connection)
				((od_route_t *)(server->route))->server_pool.yb_count_sticky++;

			server->yb_sticky_connection = true;
			*kiwi_header_data((kiwi_header_t *)data) = 'I';
		} else {
			/* decrement only if transitioning from sticky to unsticky */
			if (server->yb_sticky_connection)
				((od_route_t *)(server->route))->server_pool.yb_count_sticky--;
			server->yb_sticky_connection = false;
		}
		/* no active transaction */
		server->is_transaction = 0;
	} else if (status == 'T' || status == 'E') {
		/* in active transaction or in interrupted
		 * transaction block */
		server->is_transaction = 1;
	}

	/* update server sync reply state */
	od_server_sync_reply(server);
	return 0;
}

static int yb_read_client_id_from_notice_pkt(od_client_t *client,
					     od_server_t *server,
					     od_instance_t *instance,
					     machine_msg_t *msg)
{
	kiwi_fe_error_t hint;
	int rc = -1;

	/* Received a NOTICE packet, it can be the HINT containing the client id */
	rc = kiwi_fe_read_notice(machine_msg_data(msg), machine_msg_size(msg),
				&hint);
	if (rc == -1) {
		od_error(&instance->logger, "read clientid", client, server,
			 "failed to parse notice message from server");
		return -1;
	}

	/*
	 * If the HINT contains the client id, store it. Ignore the data otherwise.
	 */
	char *data = hint.hint;
	if (data != NULL &&
		strncmp(data, YB_SHMEM_KEY_FORMAT, strlen(YB_SHMEM_KEY_FORMAT)) == 0) {
		assert(client->client_id == 0);
		client->client_id = atoi(data + strlen(YB_SHMEM_KEY_FORMAT));
	}

	return 0;
}

static inline int yb_send_parameter_status_async(od_relay_t *relay, char *name,
						 int name_len, char *value,
						 int value_len)
{
	machine_msg_t *msg = kiwi_be_write_parameter_status(
		NULL, name, name_len, value, value_len);
	if (msg == NULL) {
		return -1;
	}
	int rc = machine_iov_add(relay->iov, msg);
	if (rc != 0) {
		return -1;
	}
	return 0;
}

static inline int yb_send_parameter_status_sync(od_io_t *io, char *name,
						int name_len, char *value,
						int value_len)
{
	machine_msg_t *msg = kiwi_be_write_parameter_status(
		NULL, name, name_len, value, value_len);
	if (msg == NULL) {
		return -1;
	}
	int rc = od_write(io, &msg);
	if (rc != 0) {
		return -1;
	}
	return 0;
}

static inline int od_backend_startup(od_server_t *server,
				     kiwi_params_t *route_params,
				     od_client_t *client)
{
	od_instance_t *instance = server->global->instance;
	od_route_t *route = server->route;

	bool is_authenticating = client->yb_is_authenticating;
	char db_name[64], user_name[64];
	int db_name_len, user_name_len;
	char yb_logical_conn_type[2];

	if (is_authenticating)
	{
		/*
		 * While authenticating, the client parameter refers to the internal
		 * control-connection client while this yb_external_client is the
		 * original client that has made the connection to the connection
		 * manager.
		 */
		assert(client->yb_external_client != NULL);
		assert(instance->config.yb_use_auth_backend);

		/*
		 * Read and use the database and user values from the client instead of
		 * the route since the route will have the user, db of the control pool.
		 * See yb_auth_via_auth_backend for more.
		 */
		strcpy(db_name, (char *)client->startup.database.value);
		db_name_len = client->startup.database.value_len;

		strcpy(user_name, (char *)client->startup.user.value);
		user_name_len = client->startup.user.value_len;

		yb_logical_conn_type[0] = (client->yb_external_client->tls) ?
						  YB_LOGICAL_ENCRYPTED_CONN :
						  YB_LOGICAL_UNENCRYPTED_CONN;
	}
	else
	{
		strcpy(db_name, (char *)route->yb_database_name);
		db_name_len = route->yb_database_name_len + 1;

		strcpy(user_name, (char *)route->yb_user_name);
		user_name_len = route->yb_user_name_len + 1;

		/*
		 * The connection between connection manager and the backend is always
		 * unencrypted.
		 */
		yb_logical_conn_type[0] = YB_LOGICAL_UNENCRYPTED_CONN;
	}
	yb_logical_conn_type[1] = '\0';

	od_client_t *external_client = client->yb_external_client;
	int argc = 0;
	const int max_default_args = 16;
	int num_startup_args =
		external_client ? external_client->yb_startup_settings.size : 0;

	kiwi_fe_arg_t *argv = malloc(sizeof(kiwi_fe_arg_t) *
				     (max_default_args + 2 * num_startup_args));

	yb_kiwi_set_fe_arg(&argv[argc++], YB_NAME_AND_SIZEOF("user"));
	yb_kiwi_set_fe_arg(&argv[argc++], user_name, user_name_len);
	yb_kiwi_set_fe_arg(&argv[argc++], YB_NAME_AND_SIZEOF("database"));
	yb_kiwi_set_fe_arg(&argv[argc++], db_name, db_name_len);
	yb_kiwi_set_fe_arg(&argv[argc++],
			   YB_NAME_AND_SIZEOF("yb_use_tserver_key_auth"));
	yb_kiwi_set_fe_arg(&argv[argc++], is_authenticating ? "0" : "1", 2);
	yb_kiwi_set_fe_arg(&argv[argc++],
			   YB_NAME_AND_SIZEOF("yb_is_client_ysqlconnmgr"));
	yb_kiwi_set_fe_arg(&argv[argc++], "1", 2);
	yb_kiwi_set_fe_arg(&argv[argc++], YB_NAME_AND_SIZEOF("yb_authonly"));
	yb_kiwi_set_fe_arg(&argv[argc++], is_authenticating ? "1" : "0", 2);

	if (route->id.physical_rep) {
		yb_kiwi_set_fe_arg(&argv[argc++],
				   YB_NAME_AND_SIZEOF("replication"));
		yb_kiwi_set_fe_arg(&argv[argc++], YB_NAME_AND_SIZEOF("on"));
	} else if (route->id.logical_rep) {
		yb_kiwi_set_fe_arg(&argv[argc++],
				   YB_NAME_AND_SIZEOF("replication"));
		yb_kiwi_set_fe_arg(&argv[argc++],
				   YB_NAME_AND_SIZEOF("database"));
	}

	/* write auth backend specific parameters. */
	if (is_authenticating) {
		/* override the remote host sent to the auth backend. */
		yb_kiwi_set_fe_arg(&argv[argc++],
				   YB_NAME_AND_SIZEOF("yb_auth_remote_host"));
		yb_kiwi_set_fe_arg(&argv[argc++], client->yb_client_address,
				   strlen(client->yb_client_address) + 1);

		/* send the connection type to the auth backend. */
		yb_kiwi_set_fe_arg(&argv[argc++],
				   YB_NAME_AND_SIZEOF("yb_logical_conn_type"));
		yb_kiwi_set_fe_arg(&argv[argc++], yb_logical_conn_type, 2);
	}

	/* We only allocated max_default_args spaces for these variables, so assert that */
	assert(argc <= max_default_args);

	if (is_authenticating) {
		/*
		 * Also send external client's startup packet settings in the startup packet
		 * to auth backend
		 */
		kiwi_var_t *startup_vars =
			external_client->yb_startup_settings.vars;
		for (int i = 0; i < external_client->yb_startup_settings.size;
		     ++i) {
			yb_kiwi_set_fe_arg(&argv[argc++], startup_vars[i].name,
					   startup_vars[i].name_len);
			yb_kiwi_set_fe_arg(&argv[argc++], startup_vars[i].value,
					   startup_vars[i].value_len);
		}
	}

	machine_msg_t *msg = kiwi_fe_write_startup_message(NULL, argc, argv);
	free(argv);
	if (msg == NULL)
		return -1;
	int rc;
	rc = od_write(&server->io, &msg);
	if (rc == -1) {
		od_error(&instance->logger, "startup", NULL, server,
			 "write error: %s", od_io_error(&server->io));
		return -1;
	}

	/* update request count and sync state */
	od_server_sync_request(server, 1);

	while (1) {
		msg = od_read(&server->io, UINT32_MAX);
		if (msg == NULL) {
			od_error(&instance->logger, "startup", NULL, server,
				 "read error: %s", od_io_error(&server->io));
			return -1;
		}

		kiwi_be_type_t type = *(char *)machine_msg_data(msg);
		od_debug(&instance->logger, "startup", NULL, server,
			 "received packet type: %s",
			 kiwi_be_type_to_string(type));

		switch (type) {
		case KIWI_BE_READY_FOR_QUERY:
			od_backend_ready(server, machine_msg_data(msg),
					 machine_msg_size(msg));
			machine_msg_free(msg);
			return 0;
		case KIWI_BE_AUTHENTICATION:
			rc = od_auth_backend(server, msg, client);
			/*
			 * Skip freeing the message in case of authentication backend.
			 * In case of auth backend, od_auth_backend itself will free the
			 * message after forwarding the packet to the client.
			 */
			if (!is_authenticating)
				machine_msg_free(msg);
			if (rc == -1)
				return -1;
			break;
		case KIWI_BE_BACKEND_KEY_DATA:
			rc = kiwi_fe_read_key(machine_msg_data(msg),
					      machine_msg_size(msg),
					      &server->key);
			machine_msg_free(msg);
			if (rc == -1) {
				od_error(
					&instance->logger, "startup", NULL,
					server,
					"failed to parse BackendKeyData message");
				return -1;
			}
			break;
		case KIWI_BE_PARAMETER_STATUS:
			od_error(
				&instance->logger, "startup", NULL, server,
				"Did not expect ParameterStatus 'S' packet from Postgres, refusing to parse");
			return -1;
		case YB_CONN_MGR_PARAMETER_STATUS: {
			char *name;
			uint32_t name_len;
			char *value;
			uint32_t value_len;
			char flags;
			rc = kiwi_fe_read_yb_parameter(machine_msg_data(msg),
						       machine_msg_size(msg),
						       &name, &name_len, &value,
						       &value_len, &flags);
			if (rc == -1) {
				machine_msg_free(msg);
				od_error(
					&instance->logger, "startup", NULL,
					server,
					"failed to parse ParameterStatus message");
				return -1;
			}

			od_debug(
				&instance->logger,
				is_authenticating ? "auth" : "startup", NULL,
				server,
				"Received YbParameterStatus, name: %.*s, value: %.*s, flags: 0x%X",
				name_len, name, value_len, value, flags);

			/* Parse the yb_logical_client_version to store it in server */
			if (strlen(name) == 25 && strcmp("yb_logical_client_version", name) == 0) {
				server->logical_client_version = atoi(value);
				machine_msg_free(msg);
				break;
			}

			/* Explicitly ignoring these variables. We don't want to replay these */
			if ((name_len == sizeof("yb_is_client_ysqlconnmgr") &&
			     strcmp(name, "yb_is_client_ysqlconnmgr") == 0) ||
			    (name_len == sizeof("yb_use_tserver_key_auth") &&
			     strcmp(name, "yb_use_tserver_key_auth") == 0)) {
				machine_msg_free(msg);
				break;
			}

			if (is_authenticating &&
			    flags & YB_PARAM_STATUS_REPORT_ENABLED) {
				/*
				 * We only care about reported variables when
				 * auth backend starts
				 */
				int rc = yb_send_parameter_status_sync(
					&client->yb_external_client->io, name,
					name_len, value, value_len);
				if (rc != 0) {
					od_error(
						&instance->logger, "auth", NULL,
						server,
						"Unable to send ParameterStatus for GUC %.*s to client",
						name_len, name);
					machine_msg_free(msg);
					return rc;
				}
			}

			if (is_authenticating) {
				if (flags &
				     YB_PARAM_STATUS_SOURCE_STARTUP) {
					/*
					 * The parameters here are the ones set by the startup packet in
					 * the auth backend. These are the parameters that have to be replayed
					 * in a transactional backend to get the same impact as the client's
					 * startup packet.
					 * See od_frontend_setup_params() for more details.
					 */
					kiwi_vars_update(
						&client->yb_external_client
							 ->yb_vars_startup,
						name, name_len, value,
						value_len,
						yb_od_instance_should_lowercase_guc_name(
							instance));
				}
			} else if ((name_len != sizeof("session_authorization") ||
				strncmp(name, "session_authorization", name_len))) {
				// set server parameters, ignore startup session_authorization
				// session_authorization is sent by the server during startup,
				// if not ignored, will make every connection sticky
				if (flags &

				     YB_PARAM_STATUS_SOURCE_STARTUP)
					kiwi_vars_update(
						&server->yb_vars_default, name,
						name_len, value, value_len,
						yb_od_instance_should_lowercase_guc_name(
							instance));
				else if (flags &
					 YB_PARAM_STATUS_USERSET_OR_SUSET_SOURCE_SESSION)
					kiwi_vars_update(
						&server->yb_vars_session, name,
						name_len, value, value_len,
						yb_od_instance_should_lowercase_guc_name(
							instance));
			}

			if ((name_len != sizeof("session_authorization") ||
				strncmp(name, "session_authorization", name_len)) &&
				route_params) {
				// skip volatile params
				// we skip in_hot_standby here because it may change
				// during connection lifetime, if server was
				// promoted
				if (name_len != sizeof("in_hot_standby") ||
					strncmp(name, "in_hot_standby", name_len)) {
					kiwi_param_t *param;
					param = kiwi_param_allocate(name, name_len, value, value_len);
					if (param)
						kiwi_params_add(route_params, param);
				}
			}

			machine_msg_free(msg);
			break;
		}
		case KIWI_BE_NOTICE_RESPONSE:
#ifdef YB_GUC_SUPPORT_VIA_SHMEM
			/*
			 * Store the client_id from the notice packet during authentication
			 * if received.
			 */
			if (is_authenticating) {
				rc = yb_read_client_id_from_notice_pkt(
					client->yb_external_client, server,
					instance, msg);
				if (rc == -1) {
					machine_msg_free(msg);
					return -1;
				}
			}
#endif
			machine_msg_free(msg);
			break;
		case YB_KIWI_BE_FATAL_FOR_LOGICAL_CONNECTION:
				yb_handle_fatalforlogicalconnection_pkt(
					is_authenticating ? client->yb_external_client : client,
					server);
			machine_msg_free(msg);
			return -1;
		case KIWI_BE_ERROR_RESPONSE:
			od_backend_error(server, "startup",
					 machine_msg_data(msg),
					 machine_msg_size(msg));
			server->error_connect = msg;
			return -1;
		case YB_OID_DETAILS:
			if (is_authenticating)
				/* Read the oid details */
				rc = yb_handle_oid_pkt_client(instance, client, msg);
			else
				rc = yb_handle_oid_pkt_server(instance, server, msg);

			machine_msg_free(msg);
			if (rc == -1)
				return -1;
			if (yb_is_route_invalid(route))
				return -1;
			break;
		default:
			machine_msg_free(msg);
			od_debug(&instance->logger, "startup", NULL, server,
				 "unexpected message: %s",
				 kiwi_be_type_to_string(type));
			return -1;
		}
	}
	od_unreachable();
	return 0;
}

static inline int od_backend_connect_to(od_server_t *server, char *context,
					char *host, int port,
					od_tls_opts_t *tlsopts)
{
	od_instance_t *instance = server->global->instance;
	assert(server->io.io == NULL);

	/* create io handle */
	machine_io_t *io;
	io = machine_io_create();
	if (io == NULL)
		return -1;

	/* set network options */
	machine_set_nodelay(io, instance->config.nodelay);
	if (instance->config.keepalive > 0) {
		machine_set_keepalive(io, 1, instance->config.keepalive,
				      instance->config.keepalive_keep_interval,
				      instance->config.keepalive_probes,
				      instance->config.keepalive_usr_timeout);
	}

	int rc;
	rc = od_io_prepare(&server->io, io, instance->config.readahead);
	if (rc == -1) {
		od_error(&instance->logger, context, NULL, server,
			 "failed to set server io");
		machine_close(io);
		machine_io_free(io);
		return -1;
	}

	/* set tls options */
	if (tlsopts->tls_mode != OD_CONFIG_TLS_DISABLE) {
		server->tls = od_tls_backend(tlsopts);
		if (server->tls == NULL)
			return -1;
	}

	uint64_t time_connect_start = 0;
	if (instance->config.log_session)
		time_connect_start = machine_time_us();

	struct sockaddr_un saddr_un;
	struct sockaddr_in saddr_v4;
	struct sockaddr_in6 saddr_v6;
	struct sockaddr *saddr;
	struct addrinfo *ai = NULL;

	/* resolve server address */
#ifdef YB_SUPPORT_FOUND
	/* 
	 * In upstream odyssey, checking only host value if is null isn't sufficient to identify out of
	 * host and unix socket.
	 */
	if (host && strlen(host) > 0) {
#else
	if (host) {
#endif
		/* assume IPv6 or IPv4 is specified */
		int rc_resolve = -1;
		if (strchr(host, ':')) {
			/* v6 */
			memset(&saddr_v6, 0, sizeof(saddr_v6));
			saddr_v6.sin6_family = AF_INET6;
			saddr_v6.sin6_port = htons(port);
			rc_resolve =
				inet_pton(AF_INET6, host, &saddr_v6.sin6_addr);
			saddr = (struct sockaddr *)&saddr_v6;
		} else {
			/* v4 or hostname */
			memset(&saddr_v4, 0, sizeof(saddr_v4));
			saddr_v4.sin_family = AF_INET;
			saddr_v4.sin_port = htons(port);
			rc_resolve =
				inet_pton(AF_INET, host, &saddr_v4.sin_addr);
			saddr = (struct sockaddr *)&saddr_v4;
		}

		/* schedule getaddrinfo() execution */
		if (rc_resolve != 1) {
			char rport[16];
			od_snprintf(rport, sizeof(rport), "%d", port);

			rc = machine_getaddrinfo(host, rport, NULL, &ai, 0);
			if (rc != 0) {
				od_error(&instance->logger, context, NULL,
					 server, "failed to resolve %s:%d",
					 host, port);
				return NOT_OK_RESPONSE;
			}
			assert(ai != NULL);
			saddr = ai->ai_addr;
		}
		/* connected */

	} else {
		/* set unix socket path */
		memset(&saddr_un, 0, sizeof(saddr_un));
		saddr_un.sun_family = AF_UNIX;
		saddr = (struct sockaddr *)&saddr_un;
		od_snprintf(saddr_un.sun_path, sizeof(saddr_un.sun_path),
			    "%s/.s.PGSQL.%d", instance->config.unix_socket_dir,
			    port);
#ifdef YB_SUPPORT_FOUND
		od_debug(&instance->logger, context, server->client,
			       server,
			       "Ysql Connection Manager connecting to unix socket at %s", saddr_un.sun_path);
#endif
	}

	uint64_t time_resolve = 0;
	if (instance->config.log_session) {
		time_resolve = machine_time_us() - time_connect_start;
	}

	/* connect to server */
	rc = machine_connect(server->io.io, saddr, UINT32_MAX);
	if (ai) {
		freeaddrinfo(ai);
	}

	if (rc == NOT_OK_RESPONSE) {
		if (host) {
			od_error(&instance->logger, context, server->client,
				 server, "failed to connect to %s:%d", host,
				 port);
		} else {
			od_error(&instance->logger, context, server->client,
				 server, "failed to connect to %s",
				 saddr_un.sun_path);
		}
		return NOT_OK_RESPONSE;
	}

	/* do tls handshake */
	if (tlsopts->tls_mode != OD_CONFIG_TLS_DISABLE) {
		rc = od_tls_backend_connect(server, &instance->logger, tlsopts);
		if (rc == NOT_OK_RESPONSE) {
			return NOT_OK_RESPONSE;
		}
	}

	uint64_t time_connect = 0;
	if (instance->config.log_session) {
		time_connect = machine_time_us() - time_connect_start;
	}

	/* log server connection */
	if (instance->config.log_session) {
		if (host) {
			od_log(&instance->logger, context, server->client,
			       server,
			       "new server connection %s:%d (connect time: %d usec, "
			       "resolve time: %d usec)",
			       host, port, (int)time_connect,
			       (int)time_resolve);
		} else {
			od_log(&instance->logger, context, server->client,
			       server,
			       "new server connection %s (connect time: %d usec, resolve "
			       "time: %d usec)",
			       saddr_un.sun_path, (int)time_connect,
			       (int)time_resolve);
		}
	}

	return 0;
}

static inline int od_storage_parse_rw_check_response(machine_msg_t *msg)
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
	uint32_t resp_len;
	rc = kiwi_read32(&resp_len, &pos, &pos_size);
	if (kiwi_unlikely(rc == -1)) {
		goto error;
	}

	/* we expect exactly one row */
	if (resp_len != 1) {
		return NOT_OK_RESPONSE;
	}
	/* pg is in recovery false means db is open for write */
	if (pos[0] == 'f') {
		return OK_RESPONSE;
	}
	/* fallthrough to error */
error:
	return NOT_OK_RESPONSE;
}

static inline od_retcode_t od_backend_attemp_connect_with_tsa(
	od_server_t *server, char *context, kiwi_params_t *route_params,
	char *host, int port, od_tls_opts_t *opts,
	od_target_session_attrs_t attrs, od_client_t *client)
{
	assert(attrs == OD_TARGET_SESSION_ATTRS_RO ||
	       attrs == OD_TARGET_SESSION_ATTRS_RW);

	od_retcode_t rc;
	machine_msg_t *msg;

	rc = od_backend_connect_to(server, context, host, port, opts);
	if (rc == NOT_OK_RESPONSE) {
		od_backend_close_connection(server);
		return rc;
	}

	/* send startup and do initial configuration */
	rc = od_backend_startup(server, route_params, client);
	if (rc == NOT_OK_RESPONSE) {
		od_backend_close_connection(server);
		return rc;
	}

	/* Check if server is read-write */
	msg = od_query_do(server, context, "SELECT pg_is_in_recovery()", NULL);
	if (msg == NULL) {
		od_backend_close_connection(server);
		return NOT_OK_RESPONSE;
	}

	switch (attrs) {
	case OD_TARGET_SESSION_ATTRS_RW:
		rc = od_storage_parse_rw_check_response(msg);
		break;
	case OD_TARGET_SESSION_ATTRS_RO:
		/* this is primary, but we are forsed to find ro backend */
		if (od_storage_parse_rw_check_response(msg) == OK_RESPONSE) {
			rc = NOT_OK_RESPONSE;
		} else {
			rc = OK_RESPONSE;
		}
		break;
	default:
		abort();
	}
	machine_msg_free(msg);

	if (rc != OK_RESPONSE) {
		od_backend_close_connection(server);
	}
	return rc;
}

int od_backend_connect(od_server_t *server, char *context,
		       kiwi_params_t *route_params, od_client_t *client)
{
	od_route_t *route = server->route;
	assert(route != NULL);
	od_instance_t *instance = server->global->instance;

	od_rule_storage_t *storage;
	storage = route->rule->storage;

	/* connect to server */
	od_retcode_t rc;
	size_t i;

	switch (storage->target_session_attrs) {
	case OD_TARGET_SESSION_ATTRS_RW:
		for (i = 0; i < storage->endpoints_count; ++i) {
			if (od_backend_attemp_connect_with_tsa(
				    server, context, route_params,
				    storage->endpoints[i].host,
				    storage->endpoints[i].port,
				    storage->tls_opts,
				    OD_TARGET_SESSION_ATTRS_RW,
				    client) == NOT_OK_RESPONSE) {
				/*backend connection not macthed by TSA */
				assert(server->io.io == NULL);
				continue;
			}

			/* target host found! */
			od_debug(&instance->logger, context, NULL, server,
				 "primary found on %s:%d",
				 storage->endpoints[i].host,
				 storage->endpoints[i].port);

			server->endpoint_selector = i;
			return OK_RESPONSE;
		}

		od_debug(&instance->logger, context, NULL, server,
			 "failed to find primary within %s", storage->host);

		return NOT_OK_RESPONSE;
	case OD_TARGET_SESSION_ATTRS_RO:
		for (i = 0; i < storage->endpoints_count; ++i) {
			if (od_backend_attemp_connect_with_tsa(
				    server, context, route_params,
				    storage->endpoints[i].host,
				    storage->endpoints[i].port,
				    storage->tls_opts,
				    OD_TARGET_SESSION_ATTRS_RO,
				    client) == NOT_OK_RESPONSE) {
				/*backend connection not macthed by TSA */
				assert(server->io.io == NULL);
				continue;
			}

			/* target host found! */
			od_debug(&instance->logger, context, NULL, server,
				 "standby found on %s:%d",
				 storage->endpoints[i].host,
				 storage->endpoints[i].port);

			server->endpoint_selector = i;
			return OK_RESPONSE;
		}

		od_debug(&instance->logger, context, NULL, server,
			 "failed to find standby within %s", storage->host);

		return NOT_OK_RESPONSE;
	case OD_TARGET_SESSION_ATTRS_ANY:
	/* fall throught */
	default:
		/* use rr_counter here */
		rc = od_backend_connect_to(server, context,
					   storage->endpoints[0].host,
					   storage->endpoints[0].port,
					   storage->tls_opts);
		if (rc == NOT_OK_RESPONSE) {
			return NOT_OK_RESPONSE;
		}

		/* send startup and do initial configuration */
		rc = od_backend_startup(server, route_params, client);
		if (rc == OK_RESPONSE) {
			server->endpoint_selector = 0;
		}
		return rc;
	}
}

int od_backend_connect_cancel(od_server_t *server, od_rule_storage_t *storage,
			      kiwi_key_t *key)
{
	od_instance_t *instance = server->global->instance;
	/* connect to server */
	int rc;
	rc = od_backend_connect_to(
		server, "cancel",
		storage->endpoints[server->endpoint_selector].host,
		storage->endpoints[server->endpoint_selector].port,
		storage->tls_opts);
	if (rc == NOT_OK_RESPONSE) {
		return NOT_OK_RESPONSE;
	}

	/* send cancel request */
	machine_msg_t *msg;
	msg = kiwi_fe_write_cancel(NULL, key->key_pid, key->key);
	if (msg == NULL)
		return -1;

	rc = od_write(&server->io, &msg);
	if (rc == -1) {
		od_error(&instance->logger, "cancel", NULL, NULL,
			 "write error: %s", od_io_error(&server->io));
		return -1;
	}

	return 0;
}

int od_backend_update_parameter(od_server_t *server, char *context, char *data,
				uint32_t size, int server_only)
{
	od_instance_t *instance = server->global->instance;
	od_client_t *client = server->client;

	char *name;
	uint32_t name_len;
	char *value;
	uint32_t value_len;
	char flags = 0;
	bool should_lowercase_name =
		yb_od_instance_should_lowercase_guc_name(instance);

	int rc;
	rc = kiwi_fe_read_yb_parameter(data, size, &name, &name_len, &value,
				       &value_len, &flags);
	if (rc == -1) {
		od_error(&instance->logger, context, NULL, server,
			 "failed to parse ParameterStatus message");
		return -1;
	}

	if (!server_only && flags & YB_PARAM_STATUS_REPORT_ENABLED) {
		/* Send ParameterStatus to client if GUC_REPORT is enabled */
		int rc = yb_send_parameter_status_async(
			&server->relay, name, name_len, value, value_len);
		if (rc != 0) {
			od_error(&instance->logger, context, NULL, server,
				 "Unable to send ParameterStatus to client");
			return -1;
		}
	}

	/* connection manager does not track role-dependent parameters */
	if (strcmp("role", name) == 0 || strcmp("session_authorization", name) == 0)
		return 0;

	/* update server only or client and server parameter */
	od_debug(&instance->logger, context, client, server,
		 "%.*s = %.*s, flags: 0x%X", name_len, name, value_len, value,
		 flags);

	/*
	 * YB: It is possible that an earlier set variable becomes unset due
	 * to transaction rollback or RESET statement. In that case, it needs
	 * to be removed from server and client variable lists
	 */

	if (flags & YB_PARAM_STATUS_SOURCE_STARTUP)
		kiwi_vars_update(&server->yb_vars_default, name, name_len,
				 value, value_len, should_lowercase_name);
	else if (flags & YB_PARAM_STATUS_DEFAULT_VAL_RESET)
		yb_kiwi_vars_remove_if_exists(&server->yb_vars_default, name,
					      name_len, should_lowercase_name);

	if (flags & YB_PARAM_STATUS_USERSET_OR_SUSET_SOURCE_SESSION)
		kiwi_vars_update(&server->yb_vars_session, name, name_len,
				 value, value_len, should_lowercase_name);
	else if (flags & YB_PARAM_STATUS_SESSION_VAL_RESET)
		yb_kiwi_vars_remove_if_exists(&server->yb_vars_session, name,
					      name_len, should_lowercase_name);

	if (!server_only) {
		if (flags & YB_PARAM_STATUS_USERSET_OR_SUSET_SOURCE_SESSION)
			kiwi_vars_update(&client->yb_vars_session, name,
					 name_len, value, value_len,
					 should_lowercase_name);
		else if (flags & YB_PARAM_STATUS_SESSION_VAL_RESET)
			yb_kiwi_vars_remove_if_exists(&client->yb_vars_session,
						      name, name_len,
						      should_lowercase_name);
	}

	return 0;
}

int od_backend_ready_wait(od_server_t *server, char *context, int count,
			  uint32_t time_ms)
{
	od_instance_t *instance = server->global->instance;
	int ready = 0;
	for (;;) {
		machine_msg_t *msg;
		msg = od_read(&server->io, time_ms);
		if (msg == NULL) {
			if (!machine_timedout()) {
				od_error(&instance->logger, context,
					 server->client, server,
					 "read error: %s",
					 od_io_error(&server->io));
			}
			/* return new status if timeout error */
			return -2;
		}
		kiwi_be_type_t type = *(char *)machine_msg_data(msg);
		od_debug(&instance->logger, context, server->client, server,
			 "%s", kiwi_be_type_to_string(type));

		if (type == KIWI_BE_PARAMETER_STATUS) {
			od_error(
				&instance->logger, context, server->client,
				server,
				"Unexpected ParameterStatus packet 'S' from postgres, refusing to parse");
			continue;
		} else if (type == YB_CONN_MGR_PARAMETER_STATUS) {
			/* update server parameter */
			int rc;
			rc = od_backend_update_parameter(server, context,
							 machine_msg_data(msg),
							 machine_msg_size(msg),
							 1);
			if (rc == -1) {
				machine_msg_free(msg);
				return -1;
			}
		} else if (type == KIWI_BE_ERROR_RESPONSE) {
			od_backend_error(server, context, machine_msg_data(msg),
					 machine_msg_size(msg));
			machine_msg_free(msg);
			continue;
		} else if (type == YB_BE_PARSE_PREPARE_ERROR_RESPONSE) {
			od_backend_evict_server_hashmap(server, context,
				machine_msg_data(msg), machine_msg_size(msg));
			machine_msg_free(msg);
			continue;
		} else if (type == KIWI_BE_READY_FOR_QUERY) {
			od_backend_ready(server, machine_msg_data(msg),
					 machine_msg_size(msg));
			ready++;
			if (ready == count) {
				machine_msg_free(msg);
				return 0;
			}
		}
		machine_msg_free(msg);
	}
	/* never reached */
}

od_retcode_t od_backend_query_send(od_server_t *server, char *context,
				   char *query, char *param, int len)
{
	od_instance_t *instance = server->global->instance;

	machine_msg_t *msg;
	if (param) {
		msg = kiwi_fe_write_prep_stmt(NULL, query, param);
	} else {
		msg = kiwi_fe_write_query(NULL, query, len);
	}

	if (msg == NULL) {
		return NOT_OK_RESPONSE;
	}

	int rc;
	rc = od_write(&server->io, &msg);
	if (rc == -1) {
		od_error(&instance->logger, context, server->client, server,
			 "write error: %s", od_io_error(&server->io));
		return NOT_OK_RESPONSE;
	}

	/* update server sync state */
	od_server_sync_request(server, 1);
	return OK_RESPONSE;
}

od_retcode_t od_backend_query(od_server_t *server, char *context, char *query,
			      char *param, int len, uint32_t timeout,
			      uint32_t count)
{
	if (od_backend_query_send(server, context, query, param, len) ==
	    NOT_OK_RESPONSE) {
		return NOT_OK_RESPONSE;
	}
	od_retcode_t rc =
		od_backend_ready_wait(server, context, count, timeout);
	return rc;
}
