#ifndef ODYSSEY_INTERNAL_CLIENT_H
#define ODYSSEY_INTERNAL_CLIENT_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

static inline od_client_t *od_client_allocate_internal(od_global_t *global,
						       char *context)
{
	od_client_t *internal_client;
	internal_client = od_client_allocate();
	if (internal_client == NULL) {
		return NULL;
	}
	od_instance_t *instance = global->instance;

	internal_client->global = global;
	internal_client->type = OD_POOL_CLIENT_INTERNAL;
	internal_client->yb_db_oid = YB_CTRL_CONN_OID;

	od_id_generate(&internal_client->id, "ic");

	/* create io handle */
	machine_io_t *io;
	io = machine_io_create();
	if (io == NULL) {
		od_client_free(internal_client);
		return NULL;
	}

	/* set network options */
	machine_set_nodelay(io, instance->config.nodelay);
	if (instance->config.keepalive > 0) {
		machine_set_keepalive(io, 1, instance->config.keepalive,
				      instance->config.keepalive_keep_interval,
				      instance->config.keepalive_probes,
				      instance->config.keepalive_usr_timeout);
	}

	int rc;
	rc = od_io_prepare(&internal_client->io, io,
			   instance->config.readahead);
	if (rc == -1) {
		od_error(&instance->logger, context, internal_client, NULL,
			 "failed to setup internal client io");
		machine_close(io);
		machine_io_free(io);
		od_client_free(internal_client);
		return NULL;
	}

	return internal_client;
}

#endif /* ODYSSEY_INTERNAL_CLIENT_H */
