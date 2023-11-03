
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

int od_reset(od_server_t *server)
{
	od_instance_t *instance = server->global->instance;
	od_route_t *route = server->route;

	/* server left in copy mode */
	if (server->is_copy) {
		od_log(&instance->logger, "reset", server->client, server,
		       "in copy, closing");
		goto drop;
	}

	/* support route rollback off */
	if (!route->rule->pool->rollback) {
		if (server->is_transaction) {
			od_log(&instance->logger, "reset", server->client,
			       server, "in active transaction, closing");
			goto drop;
		}
	}

	/* Server is not synchronized.
	 *
	 * Number of queries sent to server is not equal
	 * to the number of received replies. Do the following
	 * logic until server becomes synchronized:
	 *
	 * 1. Wait each ReadyForQuery until we receive all
	 *    replies with 1 sec timeout.
	 *
	 * 2. Send Cancel in other connection.
	 *
	 *    It is possible that client could previously
	 *    pipeline server with requests. Each request
	 *    may stall database on its own way and may require
	 *    additional Cancel request.
	 *
	 * 3. Continue with (1)
	 */
	int wait_timeout = 1000;
	int wait_try = 0;
	int wait_try_cancel = 0;
	int wait_cancel_limit = 1;
	od_retcode_t rc = 0;
	for (;;) {
		/* check that msg syncronization is not broken*/
		if (server->relay.packet > 0)
			goto error;

		while (!od_server_synchronized(server)) {
			od_debug(&instance->logger, "reset", server->client,
				 server,
				 "not synchronized, wait for %d msec (#%d)",
				 wait_timeout, wait_try);
			wait_try++;
			rc = od_backend_ready_wait(server, "reset", 1,
						   wait_timeout);
			if (rc == -1)
				break;
		}
		if (rc == -1) {
			if (!machine_timedout())
				goto error;

			/* support route cancel off */
			if (!route->rule->pool->cancel) {
				od_log(&instance->logger, "reset",
				       server->client, server,
				       "not synchronized, closing");
				goto drop;
			}

			if (wait_try_cancel == wait_cancel_limit) {
				od_error(
					&instance->logger, "reset",
					server->client, server,
					"server cancel limit reached, closing");
				goto error;
			}
			od_log(&instance->logger, "reset", server->client,
			       server, "not responded, cancel (#%d)",
			       wait_try_cancel);
			wait_try_cancel++;
			rc = od_cancel(server->global, route->rule->storage,
				       &server->key, &server->id);
			if (rc == -1)
				goto error;
			continue;
		}
		assert(od_server_synchronized(server));
		break;
	}
	od_debug(&instance->logger, "reset", server->client, server,
		 "synchronized");

	/* send rollback in case server has an active
	 * transaction running */
	if (route->rule->pool->rollback) {
		if (server->is_transaction) {
			char query_rlb[] = "ROLLBACK";
			rc = od_backend_query(server, "reset-rollback",
					      query_rlb, NULL,
					      sizeof(query_rlb), wait_timeout,
					      1);
			if (rc == -1)
				goto error;
			assert(!server->is_transaction);
		}
	}

	/* send DISCARD ALL */
	if (route->rule->pool->discard) {
		char query_discard[] = "DISCARD ALL";
		rc = od_backend_query(server, "reset-discard", query_discard,
				      NULL, sizeof(query_discard), wait_timeout,
				      1);
		if (rc == NOT_OK_RESPONSE)
			goto error;
	}

	/* send smard DISCARD ALL */
	if (route->rule->pool->smart_discard) {
		char query_discard[] =
			"SET SESSION AUTHORIZATION DEFAULT;RESET ALL;CLOSE ALL;UNLISTEN *;SELECT pg_advisory_unlock_all();DISCARD PLANS;DISCARD SEQUENCES;DISCARD TEMP;";
		rc = od_backend_query(server, "reset-discard-smart",
				      query_discard, NULL,
				      sizeof(query_discard), wait_timeout, 1);
		if (rc == NOT_OK_RESPONSE)
			goto error;
	}

	/* TODO: Add an if block */
	{
		char query_reset[] = "RESET ALL";
		rc = od_backend_query(server, "reset-resetall", query_reset,
				      NULL, sizeof(query_reset), wait_timeout, 1);
		if (rc == -1)
			goto error;
	}

	/* ready */
	return 1;
drop:
	return 0;
error:
	return -1;
}
