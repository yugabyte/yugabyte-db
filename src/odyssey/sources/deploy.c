
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
	rc = od_write(&server->io, msg);
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

int od_deploy(od_client_t *client, char *context)
{
	od_instance_t *instance = client->global->instance;
	od_server_t *server = client->server;
	od_route_t *route = client->route;

#if YB_ENABLED == TRUE
	/* compare and set options which are differs from server */
	int query_count;
	query_count = 0;

	char query[OD_QRY_MAX_SZ];
	int query_size;
	query_size = kiwi_vars_cas(&client->vars, &server->vars, query,
				   sizeof(query) - 1);

	if (query_size > 0) {
		query[query_size] = 0;
		query_size++;
		machine_msg_t *msg;
		msg = kiwi_fe_write_query(NULL, query, query_size);
		if (msg == NULL)
			return -1;

		int rc;
		rc = od_write(&server->io, msg);
		if (rc == -1)
			return -1;

		query_count++;
		client->server->synced_settings = false;

		od_debug(&instance->logger, context, client, server,
			 "deploy: %s", query);
	} else {
		client->server->synced_settings = true;
	}

	return query_count;

#else
	
	return yb_set_client_id(client, server);

#endif
}
