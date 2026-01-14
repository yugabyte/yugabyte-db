
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
			 "Sent `SET SESSION PARAMETER` packet for %d", client->client_id);
	}

	/* 
	 * Ony a 'READYFORQUERY' packet is expected from the server so we
	 * need to ignore only one packet.
	 */
	return 1;
}

int yb_send_reset_query(od_server_t *server)
{
	char query_reset[] = "RESET ALL";
	/* YB: use od_backend_query to wait for server response */
	int rc = od_backend_query(server, "reset-resetall", query_reset,
			      NULL, sizeof(query_reset), yb_wait_timeout, 1);
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
		    &server->vars,
		    yb_od_instance_should_lowercase_guc_name(instance))) {
		od_debug(&instance->logger, context, client, server,
			 "deploy: RESET ALL");
		rc = yb_send_reset_query(server);
		if (rc == -1) {
			od_error(&instance->logger, "deploy", client, server,
				 "failed to send RESET ALL");
			return -1;
		} else if (rc == -2) {
			/* handle drop and switch of server in frontend.c */
			od_error(&instance->logger, "deploy", client, server,
				 "failed to send RESET ALL due to active transaction");
			return -2;
		}
		yb_reset = 1;
	}

	char *query = malloc(yb_max_query_size + 1);
	if (query == NULL) {
		return -1;
	}
	query[yb_max_query_size] = '\0';
	int query_size;
	query_size = kiwi_vars_cas(
		&client->yb_vars_startup, &client->yb_vars_session,
		&server->vars, query, yb_max_query_size,
		yb_od_instance_should_lowercase_guc_name(instance));

	if (query_size > 0) {
		query[query_size] = 0;
		query_size++;
		machine_msg_t *msg;
		msg = kiwi_fe_write_query(NULL, query, query_size);
		if (msg == NULL) {
			free(query);
			return -1;
		}

		rc = od_write(&server->io, &msg);
		if (rc == -1) {
			free(query);
			return -1;
		}

		query_count++;
		client->server->synced_settings = false;

		od_debug(&instance->logger, context, client, server,
			 "deploy: %s", query);
	} else if (!yb_reset) { /* YB: server and client already have GUC sync */
		od_debug(&instance->logger, context, client, server,
			 "deploy: nothing to do");
		client->server->synced_settings = true;
	}

	free(query);
	return query_count;

#else
	
	return yb_set_client_id(client, server);

#endif
}
