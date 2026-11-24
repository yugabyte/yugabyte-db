
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

#define YB_ENABLED TRUE

/*
 * Set the client context on a given server connection.
 * This 'server' should be attached to 'client' (given as input parameters).
 * If there is any error, it returns -1, else number of packets to be ignored
 * that are comming from the server connection.
 */
int yb_set_client_id(od_client_t *client, od_server_t *server)
{
	od_instance_t *instance = client->global->instance;
	od_route_t *route = client->route;
	machine_msg_t *msg;

	/* Create the 'SET SESSION PARAMETER' packet. */
	msg = kiwi_fe_write_set_client_id(NULL, client->client_id);
	int rc = 0;

	/* Send `SET SESSION PARAMETER` packet. */
	rc = od_write(&server->io, &msg);
	if (rc == -1) {
		od_debug(&instance->logger, "set client_id", client, server,
			 "Unable to send `SET SESSION PARAMETER` packet");
		return -1;
	} else {
		od_debug(&instance->logger, "set client_id", client, server,
			 "Sent `SET SESSION PARAMETER` packet for %" PRIu64, client->client_id);
	}

	/* 
	 * Ony a 'READYFORQUERY' packet is expected from the server so we
	 * need to ignore only one packet.
	 */
	return 1;
}

/*
 * Send a 'G' packet carrying every var in client_vars that differs from
 * server_vars. Subtype 'D' sets startup-packet defaults, 'S' session values.
 * Format: subtype key1\0value1\0key2\0value2\0...
 * Returns number of packets to ignore (1 for ReadyForQuery), or -1 on error.
 */
static int yb_send_guc_state(od_client_t *client, od_server_t *server,
			     kiwi_vars_t *client_vars, kiwi_vars_t *server_vars,
			     char subtype)
{
	od_instance_t *instance = client->global->instance;
	machine_msg_t *msg;
	int rc;

	if (client_vars->size == 0)
		return 0;

	char *data = malloc(yb_max_query_size + 1);
	if (data == NULL) {
		return -1;
	}
	int pos = 0;
	int num_vars_in_pkt = 0;

	data[pos++] = subtype;

	for (int i = 0; i < client_vars->size; i++) {
		kiwi_var_t *client_var = &client_vars->vars[i];
		kiwi_var_t *server_var;

		/* we do not support odyssey-to-backend compression yet */
		if (strcmp(client_var->name, "compression") == 0)
			continue;

		server_var = yb_kiwi_vars_get(server_vars, client_var->name);
		if (kiwi_var_compare(client_var, server_var))
			continue;

		if (pos + client_var->name_len + client_var->value_len >
		    yb_max_query_size + 1) {
			od_error(
				&instance->logger, "deploy", client, server,
				"Size of GUC state packet '%c' exceeds yb_max_query_size (= %d)",
				subtype, yb_max_query_size);
			free(data);
			return -1;
		}

		num_vars_in_pkt++;
		memcpy(data + pos, client_var->name, client_var->name_len);
		pos += client_var->name_len;
		memcpy(data + pos, client_var->value, client_var->value_len);
		pos += client_var->value_len;
	}

	if (num_vars_in_pkt == 0) {
		free(data);
		return 0; /* No ReadyForQuery packet is expected */
	}

	msg = yb_kiwi_fe_write_guc_defaults(NULL, data, pos,
					    KIWI_FE_SET_GUC_DEFAULTS);
	free(data);

	if (msg == NULL) {
		od_error(&instance->logger, "deploy", client, server,
			 "failed to create GUC state packet '%c'", subtype);
		return -1;
	}

	rc = od_write(&server->io, &msg);
	if (rc == -1) {
		od_error(&instance->logger, "deploy", client, server,
			 "failed to send GUC state packet '%c'", subtype);
		return -1;
	}

	od_debug(&instance->logger, "deploy", client, server,
		 "sent GUC state packet '%c' with %d variables", subtype,
		 num_vars_in_pkt);

	/* One ReadyForQuery packet expected */
	return 1;
}

static int yb_send_reset_query(od_server_t *server)
{
	int rc = yb_send_reset_backend_default_query(server);
	if (rc == -1)
		return -1;
	/* reset timeout */
	if (rc == -2) {
		server->reset_timeout = true;
		return -1;
	}
	/* YB: drop server if in an active transaction due to conflict errors */
	if (server->is_transaction)
		return -2;
	return rc;
}

int od_deploy(od_client_t *client, char *context)
{
	od_instance_t *instance = client->global->instance;
	od_server_t *server = client->server;
	od_route_t *route = client->route;

#if (YB_ENABLED == TRUE) && !defined(YB_GUC_SUPPORT_VIA_SHMEM)
	/* compare and set options which are differs from server */
	int query_count;
	query_count = 0;

	int rc;
	int yb_reset = 0;

	/*
	 * YB: Optimized support for session parameters combines the reset and
	 * deploy phases to happen subsequently after one another. Check if we require
	 * a reset phase due to difference in server and client GUCs.
	 */
	if (instance->config.yb_optimized_session_parameters &&
	    yb_check_reset_needed(
		    &client->yb_vars_startup, &client->yb_vars_session,
		    &server->yb_vars_default, &server->yb_vars_session)) {
		od_debug(&instance->logger, context, client, server,
			 "deploy: Reset to transaction backend default");
		rc = yb_send_reset_query(server);
		if (rc == -1) {
			od_error(
				&instance->logger, "deploy", client, server,
				"failed to send reset to transaction backend default");
			return -1;
		} else if (rc == -2) {
			/* handle drop and switch of server in frontend.c */
			od_error(
				&instance->logger, "deploy", client, server,
				"failed to send reset to transaction backend default due"
				" to active transaction");
			return -2;
		}
		yb_reset = 1;
	}

	rc = yb_send_guc_state(client, server, &client->yb_vars_startup,
			       &server->yb_vars_default, 'D');
	if (rc == -1) {
		return -1;
	}
	query_count += rc;

	rc = yb_send_guc_state(client, server, &client->yb_vars_session,
			       &server->yb_vars_session, 'S');
	if (rc == -1) {
		return -1;
	}
	query_count += rc;

	od_debug(&instance->logger, context, client, server,
		 "deploy: query_count=%d", query_count);

	if (query_count > 0)
		client->server->synced_settings = false;
	else if (!yb_reset) { /* YB: server and client already have GUC sync */
		od_debug(&instance->logger, context, client, server,
			 "deploy: nothing to do");
		client->server->synced_settings = true;
	}

	return query_count;

#else

	return yb_set_client_id(client, server);

#endif
}
