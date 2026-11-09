/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

static inline void od_server_free_now(od_server_t *server)
{
	if (server->is_allocated) {
		od_relay_free(&server->relay);
		od_io_free(&server->io);
		yb_od_parse_queue_free(&server->parse_queue);
		if (server->prep_stmts) {
			od_hashmap_free(server->prep_stmts);
		}
		yb_kiwi_vars_free(&server->yb_vars_default);
		yb_kiwi_vars_free(&server->yb_vars_session);
		free(server);
	}
}

static inline int64_t od_server_ref(od_server_t *server)
{
	return atomic_fetch_add(&server->refs, 1);
}

static inline int64_t od_server_unref(od_server_t *server)
{
	int64_t prev = atomic_fetch_sub(&server->refs, 1);
	if (prev == 1) {
		od_server_free_now(server);
	}

	return prev;
}

void od_server_free(od_server_t *server)
{
	od_server_unref(server);
}

void od_server_cancel_begin(od_server_t *server)
{
	od_server_ref(server);
}

void od_server_cancel_end(od_server_t *server)
{
	int64_t prev_refs = od_server_unref(server);

	if (prev_refs == 2) {
		/*
		 * only cancels or "pool" holds the server
		 *
		 * if only cancels holds the server, it state
		 * and pool_element will be UNDEF and NULL respectively,
		 * (all cases of closing server means state=UNDEF)
		 * in that case nothing to be done - the server is closed
		 * and removed from pool by the router, and the next
		 * cancel end will free it
		 *
		 * if the cancel and the pool holds the server, it can be
		 * return to IDLE state and reused by another client
		 *
		 * the cases can de separated by server->state value
		 */

		if (server->state == OD_SERVER_ACTIVE) {
			od_pg_server_pool_set(&((od_route_t *)server->route)->server_pool,
					      server, OD_SERVER_IDLE);
		}
	} else {
		/*
		 * if prev_refs > 2, then
		 *  nothing can be done - the client is not detached
		 *  or there is another cancels for this server
		 *
		 * if prev_refs == 1, then
		 *  server was removed from pool
		 *  while the cancel was performed
		 *  ex: the connection failed and detached from client
		 *
		 *  cancel was the only one who used the server, and now
		 *  this unref freed the server - nothing to be done now
		 */
	}
}

void od_server_attach_client(od_server_t *server, od_client_t *client)
{
	assert(server->client == NULL);
	assert(server->state != OD_SERVER_ACTIVE);

	server->client = client;
	client->server = server;
	server->key_client = client->key;
	server->idle_time = 0;
	od_pg_server_pool_set(&((od_route_t *)server->route)->server_pool, server,
			      OD_SERVER_ACTIVE);
	od_server_ref(server);
}

void od_server_detach_client(od_server_t *server)
{
	od_client_t *client = server->client;

	assert(client != NULL);
	assert(server->state == OD_SERVER_ACTIVE);
	assert(server == client->server);

	client->server = NULL;
	server->client = NULL;
	kiwi_key_init(&server->key_client);

	if (od_server_unref(server) == 2) {
		/*
		 * set idle only if no cancel refs the server
		 * otherwise it will continue to be ACTIVE - no clients can
		 * acquire this server
		 */
		od_pg_server_pool_set(&((od_route_t *)server->route)->server_pool, server,
				      OD_SERVER_IDLE);
	}
}
