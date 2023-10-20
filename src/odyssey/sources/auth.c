
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

#define YB_SHMEM_KEY_FORMAT "shmkey="

#ifndef YB_SUPPORT_FOUND

static inline int od_auth_frontend_cleartext(od_client_t *client)
{
	od_instance_t *instance = client->global->instance;
	od_route_t *route = client->route;

	/* AuthenticationCleartextPassword */
	machine_msg_t *msg;
	msg = kiwi_be_write_authentication_clear_text(NULL);
	if (msg == NULL)
		return -1;
	int rc;
	rc = od_write(&client->io, msg);
	if (rc == -1) {
		od_error(&instance->logger, "auth", client, NULL,
			 "write error: %s", od_io_error(&client->io));
		return -1;
	}

	/* wait for password response */
	while (1) {
		msg = od_read(&client->io, UINT32_MAX);
		if (msg == NULL) {
			od_error(&instance->logger, "auth", client, NULL,
				 "read error: %s", od_io_error(&client->io));
			return -1;
		}
		kiwi_fe_type_t type = *(char *)machine_msg_data(msg);
		od_debug(&instance->logger, "auth", client, NULL, "%s",
			 kiwi_fe_type_to_string(type));
		if (type == KIWI_FE_PASSWORD_MESSAGE)
			break;
		machine_msg_free(msg);
	}

	/* read password message */
	kiwi_password_t client_token;
	kiwi_password_init(&client_token);

	rc = kiwi_be_read_password(machine_msg_data(msg), machine_msg_size(msg),
				   &client_token);
	if (rc == -1) {
		od_error(&instance->logger, "auth", client, NULL,
			 "password read error");
		od_frontend_error(client, KIWI_PROTOCOL_VIOLATION,
				  "bad password message");
		kiwi_password_free(&client_token);
		machine_msg_free(msg);
		return -1;
	}

	if (route->rule->enable_password_passthrough) {
		kiwi_password_copy(&client->received_password, &client_token);
		od_debug(&instance->logger, "auth", client, NULL,
			 "saved user password to perform backend auth");
	}

	od_extention_t *extentions = client->global->extentions;

#ifdef LDAP_FOUND
	if (client->rule->ldap_endpoint_name) {
		od_debug(&instance->logger, "auth", client, NULL,
			 "checking passwd against ldap endpoint %s",
			 client->rule->ldap_endpoint_name);

		rc = od_auth_ldap(client, &client_token);
		kiwi_password_free(&client_token);
		machine_msg_free(msg);
		if (rc != OK_RESPONSE) {
			goto auth_failed;
		}
		return OK_RESPONSE;
	}
#endif
	if (client->rule->auth_module) {
		od_module_t *modules = extentions->modules;

		/* auth callback */
		od_module_t *module;
		module = od_modules_find(modules, client->rule->auth_module);
		if (module->od_auth_cleartext_cb == NULL) {
			kiwi_password_free(&client_token);
			machine_msg_free(msg);
			goto auth_failed;
		}
		int rc = module->od_auth_cleartext_cb(client, &client_token);
		kiwi_password_free(&client_token);
		machine_msg_free(msg);
		if (rc != OD_MODULE_CB_OK_RETCODE) {
			goto auth_failed;
		}
		return OK_RESPONSE;
	}

#ifdef PAM_FOUND
	/* support PAM authentication */
	if (client->rule->auth_pam_service) {
		od_pam_convert_passwd(client->rule->auth_pam_data,
				      client_token.password);

		rc = od_pam_auth(client->rule->auth_pam_service,
				 client->startup.user.value,
				 client->rule->auth_pam_data, client->io.io);
		kiwi_password_free(&client_token);
		machine_msg_free(msg);
		if (rc == -1) {
			goto auth_failed;
		}
		return OK_RESPONSE;
	}
#endif

	/* use remote or local password source */
	kiwi_password_t client_password;
	if (client->rule->auth_query) {
		char peer[128];
		od_getpeername(client->io.io, peer, sizeof(peer), 1, 0);
		od_debug(&instance->logger, "auth", client, NULL,
			 "running auth_query for peer %s", peer);
		rc = od_auth_query(client, peer);
		if (rc == -1) {
			od_error(&instance->logger, "auth", client, NULL,
				 "failed to make auth_query");
			od_frontend_error(
				client,
				KIWI_INVALID_AUTHORIZATION_SPECIFICATION,
				"failed to make auth query");
			kiwi_password_free(&client_token);
			machine_msg_free(msg);
			return NOT_OK_RESPONSE;
		}

		// TODO: consider support for empty password case.
		if (client->password.password == NULL) {
			od_log(&instance->logger, "auth", client, NULL,
			       "user '%s.%s' incorrect user from %s",
			       client->startup.database.value,
			       client->startup.user.value, peer);
			od_frontend_error(client, KIWI_INVALID_PASSWORD,
					  "incorrect user");
			kiwi_password_free(&client_token);
			machine_msg_free(msg);
			return NOT_OK_RESPONSE;
		}
		client_password = client->password;
	} else {
		client_password.password_len = client->rule->password_len + 1;
		client_password.password = client->rule->password;
	}

	/* authenticate */
	int check = kiwi_password_compare(&client_password, &client_token);
	kiwi_password_free(&client_token);
	machine_msg_free(msg);
	if (check) {
		return OK_RESPONSE;
	}

	goto auth_failed;

auth_failed:
	od_log(&instance->logger, "auth", client, NULL,
	       "user '%s.%s' incorrect password",
	       client->startup.database.value, client->startup.user.value);
	od_frontend_error(client, KIWI_INVALID_PASSWORD, "incorrect password");
	return NOT_OK_RESPONSE;
}

static inline int od_auth_frontend_md5(od_client_t *client)
{
	od_instance_t *instance = client->global->instance;

	/* generate salt */
	uint32_t salt =
		kiwi_password_salt(&client->key, (uint32_t)machine_lrand48());

	/* AuthenticationMD5Password */
	machine_msg_t *msg;
	msg = kiwi_be_write_authentication_md5(NULL, (char *)&salt);
	if (msg == NULL)
		return -1;
	int rc;
	rc = od_write(&client->io, msg);
	if (rc == -1) {
		od_error(&instance->logger, "auth", client, NULL,
			 "write error: %s", od_io_error(&client->io));
		return -1;
	}

	/* wait for password response */
	while (1) {
		msg = od_read(&client->io, UINT32_MAX);
		if (msg == NULL) {
			od_error(&instance->logger, "auth", client, NULL,
				 "read error: %s", od_io_error(&client->io));
			return -1;
		}
		kiwi_fe_type_t type = *(char *)machine_msg_data(msg);
		od_debug(&instance->logger, "auth", client, NULL, "%s",
			 kiwi_fe_type_to_string(type));
		if (type == KIWI_FE_PASSWORD_MESSAGE)
			break;
		machine_msg_free(msg);
	}

	/* read password message */
	kiwi_password_t client_token;
	kiwi_password_init(&client_token);
	rc = kiwi_be_read_password(machine_msg_data(msg), machine_msg_size(msg),
				   &client_token);
	if (rc == -1) {
		od_error(&instance->logger, "auth", client, NULL,
			 "password read error");
		od_frontend_error(client, KIWI_PROTOCOL_VIOLATION,
				  "bad password message");
		kiwi_password_free(&client_token);
		machine_msg_free(msg);
		return -1;
	}

	/* use remote or local password source */
	kiwi_password_t client_password;
	kiwi_password_init(&client_password);

	kiwi_password_t query_password;
	kiwi_password_init(&query_password);

	if (client->rule->auth_query) {
		char peer[128];
		od_getpeername(client->io.io, peer, sizeof(peer), 1, 0);
		rc = od_auth_query(client, peer);
		if (rc == -1) {
			od_error(&instance->logger, "auth", client, NULL,
				 "failed to make auth_query");
			od_frontend_error(
				client,
				KIWI_INVALID_AUTHORIZATION_SPECIFICATION,
				"failed to make auth query");
			kiwi_password_free(&client_token);
			kiwi_password_free(&query_password);
			machine_msg_free(msg);
			return -1;
		}

		// TODO: consider support for empty password case.
		if (client->password.password == NULL) {
			od_log(&instance->logger, "auth", client, NULL,
			       "user '%s.%s' incorrect user from %s",
			       client->startup.database.value,
			       client->startup.user.value, peer);
			od_frontend_error(client, KIWI_INVALID_PASSWORD,
					  "incorrect user");
			kiwi_password_free(&client_token);
			machine_msg_free(msg);
			return -1;
		}

		query_password = client->password;
		query_password.password_len = client->password.password_len - 1;
	} else {
		query_password.password_len = client->rule->password_len;
		query_password.password = client->rule->password;
	}

#ifdef LDAP_FOUND
	if (client->rule->ldap_endpoint) {
		od_debug(&instance->logger, "auth", client, NULL,
			 "checking passwd against ldap endpoint %s",
			 client->rule->ldap_endpoint_name);

		rc = od_auth_ldap(client, &client_token);
		kiwi_password_free(&client_token);
		machine_msg_free(msg);
		if (rc != OK_RESPONSE) {
			od_log(&instance->logger, "auth", client, NULL,
			       "user '%s.%s' incorrect password",
			       client->startup.database.value,
			       client->startup.user.value);
			od_frontend_error(client, KIWI_INVALID_PASSWORD,
					  "incorrect password");
			return NOT_OK_RESPONSE;
		}
		return OK_RESPONSE;
	}
#endif

	/* prepare password hash */
	rc = kiwi_password_md5(&client_password, client->startup.user.value,
			       client->startup.user.value_len - 1,
			       query_password.password,
			       query_password.password_len, (char *)&salt);
	if (rc == -1) {
		od_error(&instance->logger, "auth", client, NULL,
			 "memory allocation error");
		kiwi_password_free(&client_password);
		kiwi_password_free(&client_token);
		if (client->rule->auth_query)
			kiwi_password_free(&query_password);
		machine_msg_free(msg);
		return -1;
	}

	/* authenticate */
	int check = kiwi_password_compare(&client_password, &client_token);
	kiwi_password_free(&client_password);
	kiwi_password_free(&client_token);
	machine_msg_free(msg);

	if (!check) {
		od_log(&instance->logger, "auth", client, NULL,
		       "user '%s.%s' incorrect password",
		       client->startup.database.value,
		       client->startup.user.value);
		od_frontend_error(client, KIWI_INVALID_PASSWORD,
				  "incorrect password");
		return -1;
	}

	return 0;
}

#ifdef USE_SCRAM

static inline int od_auth_frontend_scram_sha_256(od_client_t *client)
{
	od_instance_t *instance = client->global->instance;

	/* request AuthenticationSASL */
	machine_msg_t *msg =
		kiwi_be_write_authentication_sasl(NULL, "SCRAM-SHA-256");
	if (msg == NULL)
		return -1;

	int rc = od_write(&client->io, msg);
	if (rc == -1) {
		od_error(&instance->logger, "auth", client, NULL,
			 "write error: %s", od_io_error(&client->io));

		return -1;
	}

	/* wait for SASLInitialResponse */
	while (1) {
		msg = od_read(&client->io, UINT32_MAX);
		if (msg == NULL) {
			od_error(&instance->logger, "auth", client, NULL,
				 "read error: %s", od_io_error(&client->io));

			return -1;
		}

		kiwi_fe_type_t type = *(char *)machine_msg_data(msg);

		od_debug(&instance->logger, "auth", client, NULL, "%s",
			 kiwi_fe_type_to_string(type));

		if (type == KIWI_FE_PASSWORD_MESSAGE)
			break;

		machine_msg_free(msg);
	}

	/* read the SASLInitialResponse */
	char *mechanism;
	char *auth_data;
	size_t auth_data_size;
	rc = kiwi_be_read_authentication_sasl_initial(machine_msg_data(msg),
						      machine_msg_size(msg),
						      &mechanism, &auth_data,
						      &auth_data_size);
	if (rc == -1) {
		od_frontend_error(client,
				  KIWI_INVALID_AUTHORIZATION_SPECIFICATION,
				  "malformed SASLInitialResponse message");
		machine_msg_free(msg);
		return -1;
	}

	if (strcmp(mechanism, "SCRAM-SHA-256") != 0) {
		od_frontend_error(client,
				  KIWI_INVALID_AUTHORIZATION_SPECIFICATION,
				  "unsupported SASL authorization mechanism");
		machine_msg_free(msg);
		return -1;
	}

	/* use remote or local password source */
	kiwi_password_t query_password;
	kiwi_password_init(&query_password);

	if (client->rule->auth_query) {
		char peer[128];
		od_getpeername(client->io.io, peer, sizeof(peer), 1, 0);
		rc = od_auth_query(client, peer);
		if (rc == -1) {
			od_error(&instance->logger, "auth", client, NULL,
				 "failed to make auth_query");
			od_frontend_error(
				client,
				KIWI_INVALID_AUTHORIZATION_SPECIFICATION,
				"failed to make auth query");
			kiwi_password_free(&query_password);
			machine_msg_free(msg);
			return -1;
		}

		// TODO: consider support for empty password case.
		if (client->password.password == NULL) {
			od_log(&instance->logger, "auth", client, NULL,
			       "user '%s.%s' incorrect user from %s",
			       client->startup.database.value,
			       client->startup.user.value, peer);
			od_frontend_error(client, KIWI_INVALID_PASSWORD,
					  "incorrect user");
			machine_msg_free(msg);
			return -1;
		}

		query_password = client->password;
	} else {
		query_password.password_len = client->rule->password_len;
		query_password.password = client->rule->password;
	}

	od_scram_state_t scram_state;
	od_scram_state_init(&scram_state);

	/* try to parse authentication data */
	rc = od_scram_read_client_first_message(&scram_state, auth_data,
						auth_data_size);
	machine_msg_free(msg);
	switch (rc) {
	case 0:
		break;

	case -1:
		return -1;

	case -2:
		od_frontend_error(client,
				  KIWI_INVALID_AUTHORIZATION_SPECIFICATION,
				  "malformed SASLInitialResponse message");
		return -1;

	case -3:
		od_frontend_error(
			client, KIWI_FEATURE_NOT_SUPPORTED,
			"doesn't support channel binding at the moment");
		return -1;

	case -4:
		od_frontend_error(
			client, KIWI_FEATURE_NOT_SUPPORTED,
			"doesn't support authorization identity at the moment");
		return -1;

	case -5:
		od_frontend_error(
			client, KIWI_FEATURE_NOT_SUPPORTED,
			"doesn't support mandatory extensions at the moment");
		return -1;
	}

	rc = od_scram_parse_verifier(&scram_state, query_password.password);
	if (rc == -1)
		rc = od_scram_init_from_plain_password(&scram_state,
						       query_password.password);

	if (rc == -1) {
		od_frontend_error(
			client, KIWI_INVALID_AUTHORIZATION_SPECIFICATION,
			"invalid user password or SCRAM secret, check your config");

		return -1;
	}

	msg = od_scram_create_server_first_message(&scram_state);
	if (msg == NULL) {
		kiwi_password_free(&query_password);
		od_scram_state_free(&scram_state);

		return -1;
	}

	rc = od_write(&client->io, msg);
	if (rc == -1) {
		od_error(&instance->logger, "auth", client, NULL,
			 "write error: %s", od_io_error(&client->io));

		return -1;
	}

	/* wait for SASLResponse */
	while (1) {
		// TODO: here's infinite wait, need to replace it with
		// client_login_timeout
		msg = od_read(&client->io, UINT32_MAX);
		if (msg == NULL) {
			od_error(&instance->logger, "auth", client, NULL,
				 "read error: %s", od_io_error(&client->io));

			return -1;
		}

		kiwi_fe_type_t type = *(char *)machine_msg_data(msg);

		od_debug(&instance->logger, "auth", client, NULL, "%s",
			 kiwi_fe_type_to_string(type));

		if (type == KIWI_FE_PASSWORD_MESSAGE)
			break;

		machine_msg_free(msg);
	}

	/* read the SASLResponse */
	rc = kiwi_be_read_authentication_sasl(machine_msg_data(msg),
					      machine_msg_size(msg), &auth_data,
					      &auth_data_size);

	if (rc == -1) {
		od_frontend_error(client,
				  KIWI_INVALID_AUTHORIZATION_SPECIFICATION,
				  "malformed client SASLResponse");

		machine_msg_free(msg);
		return -1;
	}

	char *final_nonce;
	size_t final_nonce_size;
	char *client_proof;
	rc = od_scram_read_client_final_message(&scram_state, auth_data,
						auth_data_size, &final_nonce,
						&final_nonce_size,
						&client_proof);
	if (rc == -1) {
		od_frontend_error(client,
				  KIWI_INVALID_AUTHORIZATION_SPECIFICATION,
				  "malformed client SASLResponse");

		machine_msg_free(msg);
		return -1;
	}

	/* verify signatures */
	rc = od_scram_verify_final_nonce(&scram_state, final_nonce,
					 final_nonce_size);
	if (rc == -1) {
		od_frontend_error(
			client, KIWI_INVALID_AUTHORIZATION_SPECIFICATION,
			"malformed client SASLResponse: nonce doesn't match");

		machine_msg_free(msg);
		return -1;
	}

	rc = od_scram_verify_client_proof(&scram_state, client_proof);
	if (rc == -1) {
		od_frontend_error(client,
				  KIWI_INVALID_AUTHORIZATION_SPECIFICATION,
				  "password authentication failed");

		machine_msg_free(msg);
		return -1;
	}

	machine_msg_free(msg);
	/* SASLFinal Message */
	msg = od_scram_create_server_final_message(&scram_state);
	if (msg == NULL) {
		kiwi_password_free(&query_password);
		od_scram_state_free(&scram_state);

		return -1;
	}

	rc = od_write(&client->io, msg);
	if (rc == -1) {
		od_error(&instance->logger, "auth", client, NULL,
			 "write error: %s", od_io_error(&client->io));

		return -1;
	}

	return 0;
}

#endif

static inline int od_auth_frontend_cert(od_client_t *client)
{
	od_instance_t *instance = client->global->instance;
	if (!client->startup.is_ssl_request) {
		od_error(&instance->logger, "auth", client, NULL,
			 "TLS connection required");
		od_frontend_error(client,
				  KIWI_INVALID_AUTHORIZATION_SPECIFICATION,
				  "TLS connection required");
		return -1;
	}

	/* compare client certificate common name */
	od_route_t *route = client->route;
	int rc;
	if (route->rule->auth_common_name_default) {
		rc = machine_io_verify(client->io.io, route->rule->user_name);
		if (!rc) {
			return 0;
		}
	}

	od_list_t *i;
	od_list_foreach(&route->rule->auth_common_names, i)
	{
		od_rule_auth_t *auth;
		auth = od_container_of(i, od_rule_auth_t, link);
		rc = machine_io_verify(client->io.io, auth->common_name);
		if (!rc) {
			return 0;
		}
	}

	od_error(&instance->logger, "auth", client, NULL,
		 "TLS certificate common name mismatch");
	od_frontend_error(client, KIWI_INVALID_PASSWORD,
			  "TLS certificate common name mismatch");
	return -1;
}

static inline int od_auth_frontend_block(od_client_t *client)
{
	od_instance_t *instance = client->global->instance;
	od_log(&instance->logger, "auth", client, NULL,
	       "user '%s.%s' is blocked", client->startup.database.value,
	       client->startup.user.value);
	od_frontend_error(
		client, KIWI_INVALID_AUTHORIZATION_SPECIFICATION,
		"user blocked: %s %s",
		client->rule->db_is_default ? "(unknown database)" :
					      client->startup.database.value,
		client->rule->user_is_default ? "(unknown user)" :
						client->startup.user.value);
	return 0;
}
#else

int yb_forward_auth_packets(od_server_t *server, od_client_t *client)
{
	od_instance_t *instance = server->global->instance;
	int rc = 0;
	machine_msg_t *ret_msg = NULL;
	machine_msg_t *msg;
	char client_address[128];
	od_getpeername(client->io.io, client_address, sizeof(client_address), 1,
		       0);

	msg = kiwi_fe_write_authentication(NULL, client->startup.user.value,
					   client->startup.database.value,
					   client_address);

	/* Send `Auth Passthrough Request` packet. */
	rc = od_write(&server->io, msg);
	if (rc == -1) {
		od_debug(&instance->logger, "auth passthrough", client, server,
			 "Unable to send `Auth Passthrough Request` packet");
		goto server_failed;
	} else {
		od_debug(&instance->logger, "auth passthrough", client, server,
			 "Sent `Auth Passthrough Request` packet");
	}

	for (;;) {
		// Wait fot the packet from the server.
		msg = od_read(&server->io, UINT32_MAX);
		if (msg == NULL) {
			if (!machine_timedout()) {
				od_error(&instance->logger, "auth passthrough",
					 server->client, server,
					 "read error from server: %s",
					 od_io_error(&server->io));
				return -1;
			}
		}
		int save_msg = 0;
		kiwi_be_type_t type = *(char *)machine_msg_data(msg);

		od_debug(&instance->logger, "auth passthrough", server->client,
			 server, "%s", kiwi_be_type_to_string(type));

		// Process the packet.
		if (type == KIWI_BE_AUTHENTICATION) {
			int is_authenticated = 0;
			is_authenticated = yb_kiwi_fe_is_authok_packet(
				machine_msg_data(msg), machine_msg_size(msg));
			if (is_authenticated == -1) {
				od_error(
					&instance->logger, "auth passthrough",
					NULL, server,
					"failed to parse authentication message");
				goto server_failed;
			} else if (is_authenticated == 1) {
				od_debug(&instance->logger, "auth passthrough",
					 server->client, server,
					 "Authenticated");
				return 0;
			}
		} else if (type == KIWI_BE_ERROR_RESPONSE) {
			// Forward the error packet to the client.
			// TODO (janand): Convert error response into 'FATAL'.
			rc = od_write(&client->io, msg);
			return -1;
		} else
			continue;

		if (msg == NULL)
			return -1;

		// Forward the packet to the client and wait for client's response.
		rc = od_write(&client->io, msg);
		if (rc == -1) {
			od_error(&instance->logger, "auth passthrough", client,
				 NULL, "write error in middleware: %s",
				 od_io_error(&client->io));
			goto client_failed;
		} else
			od_log(&instance->logger, "auth passthrough", client,
			       NULL, " Auth request sent");

		/* Wait for password response packet from the client. */
		while (1) {
			msg = od_read(&client->io, UINT32_MAX);
			if (msg == NULL) {
				od_error(&instance->logger, "auth passthrough",
					 client, NULL,
					 "read error in middleware: %s",
					 od_io_error(&client->io));
				goto client_failed;
			}
			kiwi_fe_type_t type = *(char *)machine_msg_data(msg);
			od_debug(&instance->logger, "auth passthrough", client,
				 NULL, "%s", kiwi_fe_type_to_string(type));
			/*
			 * Packet type `KIWI_FE_PASSWORD_MESSAGE` is used by client
			 * to respond to the server packet for:
			 * 		GSSAPI, SSPI and password response messages
			 */
			if (type == KIWI_FE_PASSWORD_MESSAGE)
				break;
			machine_msg_free(msg);
		}

		// Forward the password response packet to the database.
		rc = od_write(&server->io, msg);
		if (rc == -1) {
			od_error(
				&instance->logger, "auth passthrough", client,
				server,
				"Unable to forward the password response to the server");
			return -1;
		} else
			od_log(&instance->logger, "auth passthrough", client,
			       server,
			       "Forwaded the password response to the server");
	}

/*
 * Handle the situation in which client gets disconnected, while
 * the database is still waiting for password response packet.
 */
client_failed : {
	/*
	 * Send an empty password response packet leading to an error at database side.
	 * TODO (janand): Add tests related to this case.
	 */
	msg = kiwi_fe_write_password(NULL, NULL, 0);
	if (od_write(&server->io, msg) == -1) {
		od_error(&instance->logger, "auth passthrough", NULL, server,
			 "write error in sever: %s", od_io_error(&server->io));
		/* TODOD (janand): close this server connection. */
	}

	return -1;
}

server_failed : {
	od_log(&instance->logger, "auth passthrough", client, server,
	       "database connection failed");
	/* TODOD (janand): close this server connection. */
	/* Client authentication failed. */
	return -1;
}
}

bool yb_is_valid_get_client_id_msg(char *data)
{
	if (data != NULL)
		return strncmp(data, YB_SHMEM_KEY_FORMAT, strlen(YB_SHMEM_KEY_FORMAT)) == 0;
	return false;
}

int yb_auth_frontend_passthrough(od_client_t *client, od_server_t *server)
{
	od_global_t *global = client->global;
	kiwi_var_t *user = &client->startup.user;
	kiwi_password_t *password = &client->password;
	od_instance_t *instance = global->instance;
	od_router_t *router = global->router;

	int rc = yb_forward_auth_packets(server, client);
	int rc_auth = rc;
	bool received_ready_for_query = false;
	kiwi_be_type_t type;
	machine_msg_t *msg;

	/* Wait till the `READY_FOR_QUERY` packet is received. */
	for (;;) {
		msg = od_read(&server->io, UINT32_MAX);
		if (msg == NULL) {
			if (!machine_timedout()) {
				od_error(&instance->logger, "auth passthrough",
					 server->client, server,
					 "read error from server: %s",
					 od_io_error(&server->io));
				return -1;
			}
		}

		type = *(char *)machine_msg_data(msg);
		od_debug(&instance->logger, "auth passthrough", server->client,
			 server, "Got a packet of type: %s",
			 kiwi_be_type_to_string(type));

		if (type == KIWI_BE_NOTICE_RESPONSE) {
			/* 
			 * Received a NOTICE packet, it can be the HINT containing the
			 * client id 
			 */
			kiwi_fe_error_t hint;
			rc = kiwi_fe_read_notice(machine_msg_data(msg),
						 machine_msg_size(msg), &hint);
			if (rc == -1) {
				od_debug(
					&instance->logger, "get client_id",
					client, server,
					"failed to parse error message from server");
			} else if (client->client_id == 0 &&
				yb_is_valid_get_client_id_msg(hint.hint)) {
				client->client_id = atoi(hint.hint + strlen(YB_SHMEM_KEY_FORMAT));
			}
		}
		else if (type == KIWI_BE_READY_FOR_QUERY) {
			if (client->client_id == 0)
				return -1;
			return rc_auth;
		} else if (type == KIWI_BE_ERROR_RESPONSE) {
			return -1;
		}
	}
	return rc_auth;
}

#endif

int od_auth_frontend(od_client_t *client)
{
	od_instance_t *instance = client->global->instance;

	int rc;

#ifdef YB_SUPPORT_FOUND
	rc = yb_execute_on_control_connection(client,
					      yb_auth_frontend_passthrough);
	if (rc == -1)
		return -1;
#else

	switch (client->rule->auth_mode) {
	case OD_RULE_AUTH_CLEAR_TEXT:
		rc = od_auth_frontend_cleartext(client);
		if (rc == -1)
			return -1;
		break;
	case OD_RULE_AUTH_MD5:
		rc = od_auth_frontend_md5(client);
		if (rc == -1)
			return -1;
		break;
#ifdef USE_SCRAM
	case OD_RULE_AUTH_SCRAM_SHA_256:
		rc = od_auth_frontend_scram_sha_256(client);
		if (rc == -1)
			return -1;
		break;
#endif
	case OD_RULE_AUTH_CERT:
		rc = od_auth_frontend_cert(client);
		if (rc == -1)
			return -1;
		break;
	case OD_RULE_AUTH_BLOCK:
		od_auth_frontend_block(client);
		return -1;
	case OD_RULE_AUTH_NONE:
		break;
	default:
		assert(0);
		break;
	}
#endif

	/* pass */
	machine_msg_t *msg;
	msg = kiwi_be_write_authentication_ok(NULL);
	if (msg == NULL)
		return -1;
	rc = od_write(&client->io, msg);
	if (rc == -1) {
		od_error(&instance->logger, "auth", client, NULL,
			 "write error: %s", od_io_error(&client->io));
		return -1;
	}
	return 0;
}

static inline int od_auth_backend_cleartext(od_server_t *server,
					    od_client_t *client)
{
	od_instance_t *instance = server->global->instance;
	od_route_t *route = server->route;
	assert(route != NULL);

	od_debug(&instance->logger, "auth", NULL, server,
		 "requested clear-text authentication");

	/* use storage or user password */
	char *password;
	int password_len;

	if (client != NULL && client->password.password != NULL) {
		password = client->password.password;
		password_len = client->password.password_len - /* NULL */ 1;
	} else if (route->rule->storage_password) {
		password = route->rule->storage_password;
		password_len = route->rule->storage_password_len;
	} else if (route->rule->password) {
		password = route->rule->password;
		password_len = route->rule->password_len;
	} else if (client != NULL &&
		   client->received_password.password != NULL) {
		password = client->received_password.password;
		password_len = client->received_password.password_len - 1;
	} else {
		od_error(&instance->logger, "auth", NULL, server,
			 "password required for route '%s.%s'",
			 route->rule->db_name, route->rule->user_name);
		return -1;
	}
#ifdef LDAP_FOUND
	if (client->rule->ldap_storage_credentials_attr) {
		password = client->ldap_storage_password;
		password_len = client->ldap_storage_password_len;
	}
#endif
	/* PasswordMessage */
	machine_msg_t *msg;
	msg = kiwi_fe_write_password(NULL, password, password_len + 1);
	if (msg == NULL) {
		od_error(&instance->logger, "auth", NULL, server,
			 "memory allocation error");
		return -1;
	}
	int rc;
	rc = od_write(&server->io, msg);
	if (rc == -1) {
		od_error(&instance->logger, "auth", NULL, server,
			 "write error: %s", od_io_error(&server->io));
		return -1;
	}
	return 0;
}

static inline int od_auth_backend_md5(od_server_t *server, char salt[4],
				      od_client_t *client)
{
	od_instance_t *instance = server->global->instance;
	od_route_t *route = server->route;
	assert(route != NULL);

	od_debug(&instance->logger, "auth", NULL, server,
		 "requested md5 authentication");

	/* use storage user or route user */
	char *user;
	int user_len;
	if (route->rule->storage_user) {
		user = route->rule->storage_user;
		user_len = route->rule->storage_user_len;
	} else {
		user = route->rule->user_name;
		user_len = route->rule->user_name_len;
	}

	/* use storage or user password */
	char *password;
	int password_len;
	if (client != NULL && client->password.password != NULL) {
		password = client->password.password;
		password_len = client->password.password_len - /* NULL */ 1;
	} else if (route->rule->storage_password) {
		password = route->rule->storage_password;
		password_len = route->rule->storage_password_len;
	} else if (route->rule->password) {
		password = route->rule->password;
		password_len = route->rule->password_len;
	} else if (client != NULL &&
		   client->received_password.password != NULL) {
		password = client->received_password.password;
		password_len = client->received_password.password_len - 1;
	} else {
		od_error(&instance->logger, "auth", NULL, server,
			 "password required for route '%s.%s'",
			 route->rule->db_name, route->rule->user_name);
		return -1;
	}
#ifdef LDAP_FOUND
	if (client->rule->ldap_storage_credentials_attr) {
		user = client->ldap_storage_username;
		user_len = client->ldap_storage_username_len;
		password = client->ldap_storage_password;
		password_len = client->ldap_storage_password_len;
	}
#endif
	/* prepare md5 password using server supplied salt */
	kiwi_password_t client_password;
	kiwi_password_init(&client_password);
	int rc;
	rc = kiwi_password_md5(&client_password, user, user_len, password,
			       password_len, salt);
	if (rc == -1) {
		od_error(&instance->logger, "auth", NULL, server,
			 "memory allocation error");
		kiwi_password_free(&client_password);
		return -1;
	}

	/* PasswordMessage */
	machine_msg_t *msg;
	msg = kiwi_fe_write_password(NULL, client_password.password,
				     client_password.password_len);
	kiwi_password_free(&client_password);
	if (msg == NULL) {
		od_error(&instance->logger, "auth", NULL, server,
			 "memory allocation error");
		return -1;
	}
	rc = od_write(&server->io, msg);
	if (rc == -1) {
		od_error(&instance->logger, "auth", NULL, server,
			 "write error: %s", od_io_error(&server->io));
		return -1;
	}
	return 0;
}

#ifdef USE_SCRAM

static inline int od_auth_backend_sasl(od_server_t *server, od_client_t *client)
{
	od_instance_t *instance = server->global->instance;
	od_route_t *route = server->route;

	assert(route != NULL);

	if (server->scram_state.client_nonce != NULL) {
		od_error(
			&instance->logger, "auth", NULL, server,
			"unexpected message: AuthenticationSASL was already received");

		return -1;
	}

	od_debug(&instance->logger, "auth", NULL, server,
		 "requested SASL authentication");

	if (!route->rule->storage_password && !route->rule->password &&
	    (client == NULL || client->password.password == NULL) &&
	    client->received_password.password == NULL) {
		od_error(&instance->logger, "auth", NULL, server,
			 "password required for route '%s.%s'",
			 route->rule->db_name, route->rule->user_name);

		return -1;
	}

	/* SASLInitialResponse Message */
	machine_msg_t *msg =
		od_scram_create_client_first_message(&server->scram_state);
	if (msg == NULL) {
		od_error(&instance->logger, "auth", NULL, server,
			 "memory allocation error");

		return -1;
	}

	int rc = od_write(&server->io, msg);
	if (rc == -1) {
		od_error(&instance->logger, "auth", NULL, server,
			 "write error: %s", od_io_error(&server->io));

		return -1;
	}

	return 0;
}

static inline int od_auth_backend_sasl_continue(od_server_t *server,
						char *auth_data,
						size_t auth_data_size,
						od_client_t *client)
{
	od_instance_t *instance = server->global->instance;
	od_route_t *route = server->route;

	assert(route != NULL);

	if (server->scram_state.client_nonce == NULL) {
		od_error(&instance->logger, "auth", NULL, server,
			 "unexpected message: AuthenticationSASL is missing");

		return -1;
	}

	if (server->scram_state.server_first_message != NULL) {
		od_error(
			&instance->logger, "auth", NULL, server,
			"unexpected message: AuthenticationSASLContinue was already "
			"received");

		return -1;
	}

	/* use storage or user password */
	char *password;

	if (client != NULL && client->password.password != NULL) {
		od_error(
			&instance->logger, "auth", NULL, server,
			"cannot authenticate with SCRAM secret from auth_query",
			route->rule->db_name, route->rule->user_name);

		return -1;
	} else if (route->rule->storage_password) {
		password = route->rule->storage_password;
	} else if (route->rule->password) {
		password = route->rule->password;
	} else if (client->received_password.password) {
		password = client->received_password.password;
	} else {
		od_error(&instance->logger, "auth", NULL, server,
			 "password required for route '%s.%s'",
			 route->rule->db_name, route->rule->user_name);

		return -1;
	}
#ifdef LDAP_FOUND
	if (client->rule->ldap_storage_credentials_attr) {
		password = client->ldap_storage_password;
	}
#endif
	od_debug(&instance->logger, "auth", NULL, server,
		 "continue SASL authentication");

	/* SASLResponse Message */
	machine_msg_t *msg = od_scram_create_client_final_message(
		&server->scram_state, password, auth_data, auth_data_size);
	if (msg == NULL) {
		od_error(&instance->logger, "auth", NULL, server,
			 "malformed SASLResponse message");

		return -1;
	}

	int rc = od_write(&server->io, msg);
	if (rc == -1) {
		od_error(&instance->logger, "auth", NULL, server,
			 "write error: %s", od_io_error(&server->io));

		return -1;
	}

	return 0;
}

static inline int od_auth_backend_sasl_final(od_server_t *server,
					     char *auth_data,
					     size_t auth_data_size)
{
	od_instance_t *instance = server->global->instance;

	assert(server->route);

	if (server->scram_state.server_first_message == NULL) {
		od_error(
			&instance->logger, "auth", NULL, server,
			"unexpected message: AuthenticationSASLContinue is missing");

		return -1;
	}

	od_debug(&instance->logger, "auth", NULL, server,
		 "finishing SASL authentication");

	int rc = od_scram_verify_server_signature(&server->scram_state,
						  auth_data, auth_data_size);
	if (rc == -1) {
		od_error(&instance->logger, "auth", NULL, server,
			 "server verify failed: invalid signature");

		return -1;
	}

	od_scram_state_free(&server->scram_state);

	return 0;
}

#endif

int od_auth_backend(od_server_t *server, machine_msg_t *msg,
		    od_client_t *client)
{
	od_instance_t *instance = server->global->instance;
	assert(*(char *)machine_msg_data(msg) == KIWI_BE_AUTHENTICATION);

	uint32_t auth_type;
	char salt[4];
	char *auth_data = NULL;
	size_t auth_data_size = 0;
	int rc;
	rc = kiwi_fe_read_auth(machine_msg_data(msg), machine_msg_size(msg),
			       &auth_type, salt, &auth_data, &auth_data_size);
	if (rc == -1) {
		od_error(&instance->logger, "auth", NULL, server,
			 "failed to parse authentication message");
		return -1;
	}
	msg = NULL;

	switch (auth_type) {
	/* AuthenticationOk */
	case 0:
		return 0;
	/* AuthenticationCleartextPassword */
	case 3:
		rc = od_auth_backend_cleartext(server, client);
		if (rc == -1)
			return -1;
		break;
	/* AuthenticationMD5Password */
	case 5:
		rc = od_auth_backend_md5(server, salt, client);
		if (rc == -1)
			return -1;
		break;
#ifdef USE_SCRAM
	/* AuthenticationSASL */
	case 10:
		return od_auth_backend_sasl(server, client);
	/* AuthenticationSASLContinue */
	case 11:
		return od_auth_backend_sasl_continue(server, auth_data,
						     auth_data_size, client);
	/* AuthenticationSASLContinue */
	case 12:
		return od_auth_backend_sasl_final(server, auth_data,
						  auth_data_size);
#endif
	/* unsupported */
	default:
		od_error(&instance->logger, "auth", NULL, server,
			 "unsupported authentication method");
		return -1;
	}

	/* wait for authentication response */
	while (1) {
		msg = od_read(&server->io, UINT32_MAX);
		if (msg == NULL) {
			od_error(&instance->logger, "auth", NULL, server,
				 "read error: %s", od_io_error(&server->io));
			return -1;
		}
		kiwi_be_type_t type = *(char *)machine_msg_data(msg);
		od_debug(&instance->logger, "auth", NULL, server, "%s",
			 kiwi_be_type_to_string(type));

		switch (type) {
		case KIWI_BE_AUTHENTICATION:
			rc = kiwi_fe_read_auth(machine_msg_data(msg),
					       machine_msg_size(msg),
					       &auth_type, salt, NULL, NULL);
			machine_msg_free(msg);
			if (rc == -1) {
				od_error(
					&instance->logger, "auth", NULL, server,
					"failed to parse authentication message");
				return -1;
			}
			if (auth_type != 0) {
				od_error(&instance->logger, "auth", NULL,
					 server,
					 "incorrect authentication flow");
				return 0;
			}
			return 0;
		case KIWI_BE_ERROR_RESPONSE:
			od_backend_error(server, "auth", machine_msg_data(msg),
					 machine_msg_size(msg));
			/* save error to fwd it to client */
			server->error_connect = msg;
			return -1;
		default:
			machine_msg_free(msg);
			break;
		}
	}
	return 0;
}
