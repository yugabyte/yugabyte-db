
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

/*
 * Send 'g' packet to reset GUC defaults to original values.
 * Returns 0 on success, -1 on error and -2 on timeout.
 */
int yb_send_reset_backend_default_query(od_server_t *server)
{
	od_instance_t *instance = server->global->instance;
	machine_msg_t *msg;
	int rc;

	msg = yb_kiwi_fe_write_guc_defaults(
		NULL, NULL, 0, KIWI_FE_RESET_ALL_AND_RESET_GUC_DEFAULTS);
	if (msg == NULL) {
		od_error(&instance->logger, "reset all backend default",
			 server->client, server, "failed to create packet");
		return -1;
	}

	rc = od_write(&server->io, &msg);
	od_server_sync_request(server, 1);
	if (rc == -1) {
		od_error(&instance->logger, "reset all backend default",
			 server->client, server, "write to server error: %s",
			 od_io_error(&server->io));
		return -1;
	}

	/* Wait for ReadyForQuery response */
	rc = od_backend_ready_wait(server, "reset-guc-defaults", 1,
				   yb_wait_timeout);
	return rc;
}

int od_reset(od_server_t *server)
{
	od_instance_t *instance = server->global->instance;
	od_route_t *route = server->route;

	/* server left in copy mode
	 * check that number of received CopyIn/CopyOut Responses 
	 * is equal to number received CopyDone msgs.
	 * it is indeed very strange situation if this numbers diffence
	 * is more that 1 (in absolute value).
	 *
	 * However, during client relay step this diffence may be negative,
	 * if msg pipelining is used by driver.
	 * Else drop connection, to avoid complexness of state maintenance
	 */
	if (server->in_out_response_received !=
	    server->done_fail_response_received) {
		od_log(&instance->logger, "reset", server->client, server,
		       "server left in copy, closing and drop connection");
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
	/*
	 * wait_timeout is not used anywhere. It's usage has been replaced by
	 * yb_wait_timeout. Keeping it to avoid any conflicts while merging upstream
	 * odyssey code in future.
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
				 yb_wait_timeout, wait_try);
			wait_try++;
			rc = od_backend_ready_wait(server, "reset", 1,
						   yb_wait_timeout);
			/* can be -1 or -2 */
			if (rc < 0)
				break;
		}
		if (rc < 0) {
			if (!machine_timedout())
				goto error;

			/*
			 * YB: Control should only reach here when server is not
			 * responding/has timed out.
			 * While the solution for #22849 involves not sending an
			 * error to the client in case the reset query times out,
			 * here we allow forwarding an error to the client in case
			 * we timeout waiting for a sync AND the cancel also fails.
			 * However, the client would have already disconnected
			 * (hence the call to od_reset), and the error message will
			 * not really be going anywhere.
			 */

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
					      sizeof(query_rlb), yb_wait_timeout,
					      1);
			if (rc < 0)
				goto error;
			assert(!server->is_transaction);
		}
	}

	/* send DISCARD ALL */
	if (route->rule->pool->discard) {
		char query_discard[] = "DISCARD ALL";
		rc = od_backend_query(server, "reset-discard", query_discard,
				      NULL, sizeof(query_discard), yb_wait_timeout,
				      1);
		if (rc < 0)
			goto error;
	}

	/* send smard DISCARD ALL */
	if (route->rule->pool->smart_discard) {
		char query_discard[] =
			"SET SESSION AUTHORIZATION DEFAULT;RESET ALL;CLOSE ALL;UNLISTEN *;SELECT pg_advisory_unlock_all();DISCARD PLANS;DISCARD SEQUENCES;DISCARD TEMP;";
		rc = od_backend_query(server, "reset-discard-smart",
				      query_discard, NULL,
				      sizeof(query_discard), yb_wait_timeout, 1);
		if (rc < 0)
			goto error;
	}

	/*
	 * YB: Optimized support for session parameters combines the reset and
	 * deploy phases to subsequently occur one after another. There is no
	 * need for a separate reset phase when this flag is enabled.
	 */
	if (!instance->config.yb_optimized_session_parameters && !route->id.logical_rep)
	{
		rc = yb_send_reset_backend_default_query(server);
		if (rc == -1)
			goto error;
		/* reset timeout */
		if (rc == -2)
			server->reset_timeout = true;
	}

	if (server->is_transaction) {
		od_log(&instance->logger, "reset", server->client,
			       server, "still in active transaction after reset, closing the backend");
		goto drop;
	}

	/* ready */
	return 1;
drop:
	return 0;
error:
	return -1;
}
