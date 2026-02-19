/*-------------------------------------------------------------------------
 *
 * yb_auth_passthrough.c
 * Utilities for Ysql Connection Manager/Yugabyte (Postgres layer) integration
 * that have to be defined on the Ysql Connection Manager (Odyssey) side.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *	  sources/yb_auth_passthrough.c
 *
 *-------------------------------------------------------------------------
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

#define YB_SHMEM_KEY_FORMAT "shmkey="
#define CONTEXT_AUTH_PASSTHROUGH "yb auth passthrough"

enum YB_CLI_AUTH_STATUS {
	YB_CLI_AUTH_FAILED,
	YB_CLI_AUTH_SUCCESS,
	YB_CLI_AUTH_PROGRESS,
	YB_CLI_AUTH_AWAIT_FIN
};

/*
 * Forward a "WARNING" packet as a "FATAL" packet to the client.
 * WARNING packets are of type KIWI_BE_NOTICE_RESPONSE.
 */
static int yb_forward_fatal_msg(od_client_t *client,
				machine_msg_t *warning_packet)
{
	int rc = 0;
	kiwi_be_type_t type = *(char *)machine_msg_data(warning_packet);
	kiwi_fe_error_t fatal_pkt;

	if (type != KIWI_BE_NOTICE_RESPONSE)
		goto errmsg;

	rc = kiwi_fe_read_notice(machine_msg_data(warning_packet),
				 machine_msg_size(warning_packet), &fatal_pkt);
	if (rc == -1)
		goto errmsg;

	od_frontend_fatal_forward(client, fatal_pkt.code, fatal_pkt.message);
	return 0;

errmsg:
	od_frontend_fatal(
		client, KIWI_PROTOCOL_VIOLATION,
		"Error while forwarding the FATAL packet comming from the pg_backend.");
	return -1;
}

static void yb_control_connection_failed(od_server_t *server,
					 od_instance_t *instance)
{
	od_debug(&instance->logger, CONTEXT_AUTH_PASSTHROUGH, NULL, server,
		 "Control connection failed during the auth passthrough");

	/*
	 * GH issue #19781 This connection should not be kept in the pool,
	 * mark it as offile to avoide the "Broken physical connection error".
	 */
	server->offline = 1;
}

static int yb_server_write_auth_passthrough_request_pkt(od_client_t *client,
						       od_server_t *server,
						       od_instance_t *instance)
{
	int rc = -1;
	machine_msg_t *msg;
	od_route_t *route = server->route;

	char user_name[64], db_name[64];
	int user_name_len, db_name_len;

	strcpy(user_name, (char *)client->startup.user.value);
	user_name_len = client->startup.user.value_len;

	strcpy(db_name, (char *)client->startup.database.value);
	db_name_len = client->startup.database.value_len;

	char client_address[128];
	od_getpeername(client->io.io, client_address, sizeof(client_address), 1,
		       0);

	char yb_logical_conn_type[2] = "x";
	yb_logical_conn_type[0] = client->tls ? YB_LOGICAL_ENCRYPTED_CONN :
						YB_LOGICAL_UNENCRYPTED_CONN;

	msg = yb_kiwi_fe_write_authentication(NULL);
	/* Send `Auth Passthrough Request` packet. */
	rc = od_write(&server->io, &msg);
	if (rc == -1) {
		yb_control_connection_failed(server, instance);
		od_frontend_fatal(
			client, KIWI_PROTOCOL_VIOLATION,
			"Unable to send auth passthrough request, broken control connection");
		return -1;
	}
	od_debug(
		&instance->logger, CONTEXT_AUTH_PASSTHROUGH, client, server,
		"starting Auth Passthrough, sent 'Auth Passthrough Request' packet");

	const int max_default_args = 16;
	int num_startup_args = client->yb_startup_settings.size;
	kiwi_var_t *startup_vars = client->yb_startup_settings.vars;

	int argc = 0;
	kiwi_fe_arg_t *argv = malloc(sizeof(kiwi_fe_arg_t) *
				     (max_default_args + 2 * num_startup_args));

	yb_kiwi_set_fe_arg(&argv[argc++], YB_NAME_AND_SIZEOF("user"));
	yb_kiwi_set_fe_arg(&argv[argc++], user_name, user_name_len);
	yb_kiwi_set_fe_arg(&argv[argc++], YB_NAME_AND_SIZEOF("database"));
	yb_kiwi_set_fe_arg(&argv[argc++], db_name, db_name_len);

	/* override the remote host sent to the control backend. */
	yb_kiwi_set_fe_arg(&argv[argc++],
			   YB_NAME_AND_SIZEOF("yb_auth_remote_host"));
	yb_kiwi_set_fe_arg(&argv[argc++], client_address,
			   strlen(client_address) + 1);

	/* send the connection type to the control backend. */
	yb_kiwi_set_fe_arg(&argv[argc++],
			   YB_NAME_AND_SIZEOF("yb_logical_conn_type"));
	yb_kiwi_set_fe_arg(&argv[argc++], yb_logical_conn_type, 2);

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

	/* We only allocated max_default_args spaces for these variables, so assert that */
	assert(argc <= max_default_args);

	/*
	 * Also send external client's startup packet settings in the startup packet
	 * to the control backend.
	 */
	for (int i = 0; i < num_startup_args; ++i) {
		yb_kiwi_set_fe_arg(&argv[argc++], startup_vars[i].name,
				   startup_vars[i].name_len);
		yb_kiwi_set_fe_arg(&argv[argc++], startup_vars[i].value,
				   startup_vars[i].value_len);
	}

	msg = kiwi_fe_write_startup_message(NULL, argc, argv);
	free(argv);
	argv = NULL;

	if (msg == NULL)
		return -1;
	rc = od_write(&server->io, &msg);
	if (rc == -1) {
		od_error(&instance->logger, "auth passthrough startup", client,
			 server, "write error: %s", od_io_error(&server->io));
		return -1;
	}

	od_debug(&instance->logger, CONTEXT_AUTH_PASSTHROUGH, client, server,
		 "forwarded startup packet");

	return 0;
}

static machine_msg_t *yb_read_auth_pkt_from_server(od_client_t *client,
						   od_server_t *server,
						   od_instance_t *instance)
{
	machine_msg_t *msg = NULL;

	msg = od_read(&server->io, UINT32_MAX);
	if (msg == NULL) {
		if (!machine_timedout()) {
			od_error(&instance->logger, CONTEXT_AUTH_PASSTHROUGH,
				 server->client, server,
				 "read error from server: %s",
				 od_io_error(&server->io));
			return NULL;
		}
	}

	return msg;
}

/*
 * In this scenario, pg_backend is expecting a packet from the client.
 * Since the client exited before sending any packet, we need to make sure that
 * pg_backend exits from the read loop.
 *
 * Send an empty password response packet leading to an auth failure
 * at database side.
 *
 * This case is observed when connecting to the database via ysqlsh.
 */

static void yb_client_exit_mid_passthrough(od_server_t *server,
					   od_instance_t *instance)
{
	machine_msg_t *msg = kiwi_fe_write_password(NULL, "", 1);
	if (od_write(&server->io, &msg) == -1) {
		od_error(&instance->logger, CONTEXT_AUTH_PASSTHROUGH, NULL,
			 server, "write error in sever: %s",
			 od_io_error(&server->io));
		server->offline = 1;
	}
}

/*
 * TODO (vikram.damle) (#29176): Merge this function with the copy defined in backend.c
 * This will merge when auth passthrough flow is merged with auth backend.
 */

static inline int
yb_send_parameter_status_auth_passthrough(od_io_t *io, char *name, int name_len,
					  char *value, int value_len)
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

static int yb_forward_auth_pkt_client_to_server(od_client_t *client,
						od_server_t *server,
						od_instance_t *instance)
{
	machine_msg_t *msg;
	kiwi_fe_type_t type;
	int rc = -1;

	/* Wait for password response packet from the client. */
	while (true) {
		msg = od_read(&client->io, UINT32_MAX);
		if (msg == NULL) {
			od_error(&instance->logger, CONTEXT_AUTH_PASSTHROUGH,
				 client, NULL, "read error in middleware: %s",
				 od_io_error(&client->io));
			yb_client_exit_mid_passthrough(server, instance);
			return -1;
		}

		type = *(char *)machine_msg_data(msg);

		/*
		 * Packet type `KIWI_FE_PASSWORD_MESSAGE` is used by client
		 * to respond to the server packet for:
		 * 		GSSAPI, SSPI and password response messages
		 */
		if (type == KIWI_FE_PASSWORD_MESSAGE)
			break;
		machine_msg_free(msg);
	}

	/* Forward the password response packet to the database. */
	rc = od_write(&server->io, &msg);
	if (rc == -1) {
		od_error(
			&instance->logger, CONTEXT_AUTH_PASSTHROUGH, client,
			server,
			"Unable to forward the password response to the server");
		return -1;
	}

	od_debug(&instance->logger, CONTEXT_AUTH_PASSTHROUGH, client, server,
		 "Forwaded the password response to the server");
	return 0;
}

/* Write a packet on the client socket */
static int yb_client_write_pkt(od_client_t *client, od_server_t *server,
			       od_instance_t *instance, machine_msg_t *msg,
			       enum YB_CLI_AUTH_STATUS progress)
{
	int rc = od_write(&client->io, &msg);
	if (rc == -1) {
		od_error(&instance->logger, CONTEXT_AUTH_PASSTHROUGH, client,
			 NULL, "write error in middleware: %s",
			 od_io_error(&client->io));

		if (progress == YB_CLI_AUTH_PROGRESS) {
			/*
			 * pg_backend will be expecting a packet,
			 * send an empty password packet.
			 */
			yb_client_exit_mid_passthrough(server, instance);
		}

		/* Since the client socket is closed, we need not send any fatal packet */
		return -1;
	}

	return rc;
}

/*
 * Read the authentication packets from the server,
 * process it and forward it to the client.
 */
static enum YB_CLI_AUTH_STATUS
yb_forward_auth_pkt_server_to_client(od_client_t *client, od_server_t *server,
				     od_instance_t *instance)
{
	kiwi_be_type_t type;
	machine_msg_t *msg = NULL;
	int rc = -1;
	int auth_pkt_type = 0;
	enum YB_CLI_AUTH_STATUS progress = YB_CLI_AUTH_PROGRESS;

	/* 
	 * Forward all the packets comming from the server until
	 * we receive a "AUTH" pkt 
	 */
	while (true) {
		msg = yb_read_auth_pkt_from_server(client, server, instance);

		if (msg == NULL) {
			server->offline = 1;
			return YB_CLI_AUTH_FAILED;
		}

		type = *(char *)machine_msg_data(msg);
		od_debug(&instance->logger, CONTEXT_AUTH_PASSTHROUGH,
			 server->client, server,
			 "Got %s packet from the server",
			 kiwi_be_type_to_string(type));

		/*
		 * Here, expected packet can be of the following type
		 * 		1. KIWI_BE_AUTHENTICATION
		 * 		2. KIWI_BE_NOTICE_RESPONSE
		 * 		3. KIWI_BE_ERROR_RESPONSE
		 * 		4. YB_KIWI_BE_FATAL_FOR_LOGICAL_CONNECTION
		 * For any other packet we need to report an error.
		 */
		switch (type) {
		case KIWI_BE_AUTHENTICATION:
			/*
			 * If the packet is of type:
			 * 		1. AUTHOK --> forward it and mark the client authenticated.
			 * 		2. AUTHFAILED --> forward the next notice packet as the FATAL
			 */
			auth_pkt_type = yb_kiwi_fe_auth_packet_type(
				machine_msg_data(msg), machine_msg_size(msg));

			if (auth_pkt_type < 0) {
				od_error(
					&instance->logger,
					CONTEXT_AUTH_PASSTHROUGH, NULL, server,
					"failed to parse authentication message");
				yb_control_connection_failed(server, instance);
				od_frontend_fatal(
					client, KIWI_PROTOCOL_VIOLATION,
					"Unable to read the control connection packet");
				return YB_CLI_AUTH_FAILED;
			}
			od_debug(&instance->logger, CONTEXT_AUTH_PASSTHROUGH, NULL, server,
				"received server packet auth-type: %d", auth_pkt_type);

			if (auth_pkt_type == 0)  /* AUTHOK pkt */
				progress = YB_CLI_AUTH_SUCCESS;
			else if(auth_pkt_type == 12)  /* SCRAM Fin: wait for AuthOK */
				progress = YB_CLI_AUTH_AWAIT_FIN;
			else 
				progress = YB_CLI_AUTH_PROGRESS;

			rc = yb_client_write_pkt(client, server, instance, msg,
						 progress);
			if (rc == -1) {
				return YB_CLI_AUTH_FAILED;
			}

			return progress;

		case KIWI_BE_NOTICE_RESPONSE:
			/* Simply forward the packet */
			rc = yb_client_write_pkt(client, server, instance, msg,
						 progress);
			if (rc == -1) {
				return YB_CLI_AUTH_FAILED;
			}

			/* Wait in the loop for the Auth packet */
			continue;

		case YB_KIWI_BE_FATAL_FOR_LOGICAL_CONNECTION:
			yb_handle_fatalforlogicalconnection_pkt(client, server);

			/* No need to wait for the Auth packet */
			return YB_CLI_AUTH_FAILED;

		case KIWI_BE_ERROR_RESPONSE:
		default:
			progress = YB_CLI_AUTH_FAILED;
			rc = yb_client_write_pkt(client, server, instance, msg,
						 progress);

			od_debug(&instance->logger, CONTEXT_AUTH_PASSTHROUGH,
				 client, server,
				 "Received an unexpected error message");
			server->offline = 1;
			return progress;
		}
	}
}

/*
 * Handle the forwarding of authentication packets in Ysql Connection Manager.
 * Any packet related to the auth will be forwarded to the client in this function.
 *
 * Incase the client gets authenticated but there is a failure post authentication
 * and before sending the READY_FOR_QUERY packet, client will receive a
 * AuthOK message followed by a FATAL packet.
 */
static int yb_route_auth_packets(od_server_t *server, od_client_t *client)
{
	od_instance_t *instance = server->global->instance;
	enum YB_CLI_AUTH_STATUS status;

	yb_server_write_auth_passthrough_request_pkt(client, server, instance);

	while (true) {
		status = yb_forward_auth_pkt_server_to_client(client, server,
							      instance);
		switch (status) {
		case YB_CLI_AUTH_FAILED:
			od_error(&instance->logger, CONTEXT_AUTH_PASSTHROUGH, client, server,
				"failed to relay auth passthrough Server to Client. Exiting...");
			return -1;
		case YB_CLI_AUTH_SUCCESS:
			return 0;
		case YB_CLI_AUTH_AWAIT_FIN:
			/* 
			 * In case of mechanisms like SCRAM, the server sends
			 * the last packet before sending the AuthOK packet.
			 * Thus, we need to forward two consecutive packets from
			 * the server. Reading from the client hangs indefinitely.
			 */
			continue;
		case YB_CLI_AUTH_PROGRESS:
			break;
		}

		if (yb_forward_auth_pkt_client_to_server(client, server,
							 instance) < 0)
			return -1;
	}
}

static bool yb_pkt_contains_client_id(char *data)
{
	if (data != NULL) {
		return strncmp(data, YB_SHMEM_KEY_FORMAT,
			       strlen(YB_SHMEM_KEY_FORMAT)) == 0;
	}

	return false;
}

static int yb_read_client_id_from_notice_pkt(od_client_t *client,
					     od_server_t *server,
					     od_instance_t *instance,
					     machine_msg_t *msg)
{
	kiwi_fe_error_t hint;
	int rc = -1;

	/* Received a NOTICE packet, it can be the HINT containing the client id */
	if (kiwi_fe_read_notice(machine_msg_data(msg), machine_msg_size(msg),
				&hint) == -1) {
		od_debug(&instance->logger, "read clientid", client, server,
			 "failed to parse error message from server");
		return -1;
	}

	if (yb_pkt_contains_client_id(hint.hint)) {
		assert(client->client_id == 0);
		client->client_id =
			atoi(hint.hint + strlen(YB_SHMEM_KEY_FORMAT));
		return 0;
	}
	return -1;
}

int yb_auth_frontend_passthrough(od_client_t *client, od_server_t *server)
{
	od_global_t *global = client->global;
	kiwi_var_t *user = &client->startup.user;
	od_instance_t *instance = global->instance;
	od_router_t *router = global->router;
	kiwi_be_type_t type;
	machine_msg_t *msg;
	int rc = -1;
	int rc_auth = -1;

	assert(!instance->config.yb_use_auth_backend);

	rc = yb_route_auth_packets(server, client);
	rc_auth = rc;

	/*
	 * Wait till the `READY_FOR_QUERY` packet is received.
	 * TODO (vikram.damle) (#29176): Need a `reset phase` for control backends in
	 * authentication. The backend may send extra information that is no longer
	 * needed if auth fails as part of its internal state reset (eg. GUC reset
	 * ParameterStatus packets). Need to clear the "buffer" of incoming messages
	 * before returning the control backend to the pool.
	 */
	while (true) {
		msg = od_read(&server->io, UINT32_MAX);
		if (msg == NULL) {
			if (!machine_timedout()) {
				od_error(&instance->logger,
					 CONTEXT_AUTH_PASSTHROUGH,
					 server->client, server,
					 "read error from server: %s",
					 od_io_error(&server->io));
				return -1;
			}
		}

		type = *(char *)machine_msg_data(msg);
		od_debug(&instance->logger, CONTEXT_AUTH_PASSTHROUGH,
			 server->client, server,
			 "Got %s packet from the pg_backend",
			 kiwi_be_type_to_string(type));

		switch (type) {
		case KIWI_BE_READY_FOR_QUERY:
#ifdef YB_GUC_SUPPORT_VIA_SHMEM
			if (client->client_id == 0) {
				od_frontend_fatal(
					client, KIWI_PROTOCOL_VIOLATION,
					"Unable to allocate the shared memory segment");
				return -1;
			}
#endif
			machine_msg_free(msg);
			return rc_auth;

		case YB_KIWI_BE_FATAL_FOR_LOGICAL_CONNECTION:
			yb_handle_fatalforlogicalconnection_pkt(client, server);
			rc_auth = -1;
			machine_msg_free(msg);
			continue;

		case KIWI_BE_NOTICE_RESPONSE:
			if (yb_read_client_id_from_notice_pkt(
				    client, server, instance, msg) < 0) {
				/*
				 * The notice packet does not contains any client id and
				 * thus it is required to forward this notice packet to the client
				 */
				rc = od_write(&client->io, &msg);
				if (rc < 0)
					rc_auth = -1;
				continue;
			}
			machine_msg_free(msg);

			break;

		case YB_OID_DETAILS:
			/* Read the oid details */
			yb_handle_oid_pkt_client(instance, client, msg);
			machine_msg_free(msg);
			continue;

		case KIWI_BE_PARAMETER_STATUS:
			od_error(
				&instance->logger, CONTEXT_AUTH_PASSTHROUGH,
				NULL, server,
				"Did not expect ParameterStatus 'S' packet from Postgres, refusing to parse");
			machine_msg_free(msg);
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
					&instance->logger,
					CONTEXT_AUTH_PASSTHROUGH, NULL, server,
					"failed to parse ParameterStatus message");
				return -1;
			}

			od_debug(
				&instance->logger, CONTEXT_AUTH_PASSTHROUGH,
				NULL, server,
				"Received YbParameterStatus, name: %.*s, value: %.*s, flags: 0x%X",
				name_len, name, value_len, value, flags);

			/* Parse the yb_logical_client_version to store it in server */
			if (name_len == sizeof("yb_logical_client_version") &&
			    strcmp("yb_logical_client_version", name) == 0) {
				client->logical_client_version = atoi(value);
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

			if (flags & YB_PARAM_STATUS_REPORT_ENABLED) {
				/*
				 * We only care about reported variables when
				 * auth backend starts
				 */
				int rc =
					yb_send_parameter_status_auth_passthrough(
						&client->io, name, name_len,
						value, value_len);
				if (rc != 0 && rc_auth == 0) {
					od_error(
						&instance->logger, "auth", NULL,
						server,
						"Unable to send ParameterStatus for GUC %.*s to client",
						name_len, name);
					machine_msg_free(msg);
					return rc;
				}
			}

			if (flags & YB_PARAM_STATUS_SOURCE_STARTUP) {
				/*
				 * The parameters here are the ones set by the startup packet in
				 * the auth backend (here, passthrough). These are the parameters
				 * that have to be replayed in a transactional backend to get the
				 * same impact as the client's startup packet.
				 * See od_frontend_setup_params() for more details.
				 * TODO (vikram.damle) (#29178): Check what has to be done for GUC name
				 * casing now that we are "fixing" auth passthrough.
				 */
				kiwi_vars_update(
					&client->yb_vars_startup, name,
					name_len, value, value_len,
					yb_od_instance_should_lowercase_guc_name(
						instance));
			}

			machine_msg_free(msg);
			break;
		}

		case KIWI_BE_ERROR_RESPONSE:
			/* Physical connection is broken, no need to wait for readyForQuery pkt */
			machine_msg_free(msg);
			server->offline = 1;
			break;
		default:
			od_error(
				&instance->logger, CONTEXT_AUTH_PASSTHROUGH,
				client, server,
				"got unhandled packet type %s (0x%x) during auth passthrough",
				kiwi_be_type_to_string(type), type);
			machine_msg_free(msg);
			return -1;
		}
	}
}

/*
 * Handle FatalForLogicalConnection packet.
 * The immediate next WARNING packet should be treated as a FATAL packet,
 * for the client.
 * Forward this packet as a FATAL and close the client connection.
 */
void yb_handle_fatalforlogicalconnection_pkt(od_client_t *client,
					     od_server_t *server)
{
	od_instance_t *instance = client->global->instance;
	machine_msg_t *msg;

	msg = od_read(&server->io, UINT32_MAX);
	if (msg == NULL) {
		if (!machine_timedout()) {
			od_error(&instance->logger,
				 "handling fatalforlogicalconnection pkt",
				 server->client, server,
				 "read error from server: %s",
				 od_io_error(&server->io));

			od_frontend_fatal(
				client, KIWI_PROTOCOL_VIOLATION,
				"Error while reading the FATAL message from the pg_backend.");
			return;
		}
	}

	yb_forward_fatal_msg(client, msg);
	machine_msg_free(msg);
}
