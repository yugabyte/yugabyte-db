
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

static inline int od_auth_parse_passwd_from_datarow(od_logger_t *logger,
						    machine_msg_t *msg,
						    kiwi_password_t *result)
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

	if (count != 2)
		goto error;

	/* user (not used) */
	uint32_t user_len;
	rc = kiwi_read32(&user_len, &pos, &pos_size);
	if (kiwi_unlikely(rc == -1)) {
		goto error;
	}
	char *user = pos;
	rc = kiwi_readn(user_len, &pos, &pos_size);
	if (kiwi_unlikely(rc == -1)) {
		goto error;
	}

	(void)user;
	(void)user_len;

	/* password */

	// The length of the column value, in bytes (this count does not include itself).
	// Can be zero.
	// As a special case, -1 indicates a NULL column value. No value bytes follow in the NULL case.
	uint32_t password_len;
	rc = kiwi_read32(&password_len, &pos, &pos_size);

	if (kiwi_unlikely(rc == -1)) {
		goto error;
	}
	// --1
	if (password_len == UINT_MAX) {
		result->password = NULL;
		result->password_len = password_len + 1;

		od_debug(logger, "query", NULL, NULL,
			 "auth query returned empty password for user %.*s",
			 user_len, user);
		goto success;
	}

	if (password_len > ODYSSEY_AUTH_QUERY_MAX_PASSSWORD_LEN) {
		goto error;
	}

	char *password = pos;
	rc = kiwi_readn(password_len, &pos, &pos_size);
	if (kiwi_unlikely(rc == -1)) {
		goto error;
	}

	result->password = malloc(password_len + 1);
	if (result->password == NULL) {
		goto error;
	}
	memcpy(result->password, password, password_len);
	result->password[password_len] = 0;
	result->password_len = password_len + 1;

success:
	return OK_RESPONSE;
error:
	return NOT_OK_RESPONSE;
}

int od_auth_query(od_client_t *client, char *peer)
{
	od_global_t *global = client->global;
	od_rule_t *rule = client->rule;
	kiwi_var_t *user = &client->startup.user;
	kiwi_password_t *password = &client->password;
	od_instance_t *instance = global->instance;
	od_router_t *router = global->router;

	/* create internal auth client */
	od_client_t *auth_client;

	auth_client = od_client_allocate_internal(global, "auth-query");

	if (auth_client == NULL) {
		od_debug(&instance->logger, "auth_query", auth_client, NULL,
			 "failed to allocate internal auth query client");
		return NOT_OK_RESPONSE;
	}

	/* set auth query route user and database */
	kiwi_var_set(&auth_client->startup.user, KIWI_VAR_UNDEF,
		     rule->auth_query_user, strlen(rule->auth_query_user) + 1);

	kiwi_var_set(&auth_client->startup.database, KIWI_VAR_UNDEF,
		     rule->auth_query_db, strlen(rule->auth_query_db) + 1);

	/* route */
	od_router_status_t status;
	status = od_router_route(router, auth_client);
	if (status != OD_ROUTER_OK) {
		od_debug(&instance->logger, "auth_query", auth_client, NULL,
			 "failed to route internal auth query client: %s",
			 od_router_status_to_str(status));
		od_client_free(auth_client);
		return NOT_OK_RESPONSE;
	}

	/* attach */
	status = od_router_attach(router, auth_client, false, client);
	if (status != OD_ROUTER_OK) {
		od_debug(
			&instance->logger, "auth_query", auth_client, NULL,
			"failed to attach internal auth query client to route: %s",
			od_router_status_to_str(status));
		od_router_unroute(router, auth_client);
		od_client_free(auth_client);
		return NOT_OK_RESPONSE;
	}
	od_server_t *server;
	server = auth_client->server;

	od_debug(&instance->logger, "auth_query", auth_client, server,
		 "attached to server %s%.*s", server->id.id_prefix,
		 (int)sizeof(server->id.id), server->id.id);

	/* connect to server, if necessary */
	int rc;
	if (server->io.io == NULL) {
		rc = od_backend_connect(server, "auth_query", NULL,
					auth_client);
		if (rc == NOT_OK_RESPONSE) {
			od_debug(&instance->logger, "auth_query", auth_client,
				 server,
				 "failed to acquire backend connection: %s",
				 od_io_error(&server->io));
			od_router_close(router, auth_client);
			od_router_unroute(router, auth_client);
			od_client_free(auth_client);
			return NOT_OK_RESPONSE;
		}
	}

	/* preformat and execute query */
	char query[OD_QRY_MAX_SZ];
	char *format_pos = rule->auth_query;
	char *format_end = rule->auth_query + strlen(rule->auth_query);
	od_query_format(format_pos, format_end, user, peer, query,
			sizeof(query));

	machine_msg_t *msg;
	msg = od_query_do(server, "auth query", query, user->value);
	if (msg == NULL) {
		od_debug(&instance->logger, "auth_query", auth_client, server,
			 "auth query returned empty msg");
		od_router_close(router, auth_client);
		od_router_unroute(router, auth_client);
		od_client_free(auth_client);
		return NOT_OK_RESPONSE;
	}
	if (od_auth_parse_passwd_from_datarow(&instance->logger, msg,
					      password) == NOT_OK_RESPONSE) {
		od_debug(&instance->logger, "auth_query", auth_client, server,
			 "auth query returned datarow in incompatable format");
		od_router_close(router, auth_client);
		od_router_unroute(router, auth_client);
		od_client_free(auth_client);
		return NOT_OK_RESPONSE;
	}

	/* detach and unroute */
	od_router_detach(router, auth_client);
	od_router_unroute(router, auth_client);
	od_client_free(auth_client);
	return OK_RESPONSE;
}
