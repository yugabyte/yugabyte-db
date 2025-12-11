#ifndef ODYSSEY_SERVER_H
#define ODYSSEY_SERVER_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

typedef struct od_server od_server_t;

typedef enum {
	OD_SERVER_UNDEF,
	OD_SERVER_IDLE,
	OD_SERVER_ACTIVE,
} od_server_state_t;

struct od_server {
	od_server_state_t state;
#ifdef USE_SCRAM
	od_scram_state_t scram_state;
#endif
	od_id_t id;
	machine_tls_t *tls;
	od_io_t io;
	od_relay_t relay;
	int is_allocated;
	int is_transaction;
	/* Copy stmt state */
	uint64_t done_fail_response_received;
	uint64_t in_out_response_received;
	/**/
	int deploy_sync;
	od_stat_state_t stats_state;

	uint64_t sync_request;
	uint64_t sync_reply;

	int idle_time;

	kiwi_key_t key;
	kiwi_key_t key_client;
	kiwi_vars_t vars;

	machine_msg_t *error_connect;
	/* od_client_t */
	void *client;
	/* od_route_t  */
	void *route;

	/* storage endpoiunt index, which we are connected to */
	size_t endpoint_selector;

	/* allocated prepared statements ids */
	od_hashmap_t *prep_stmts;

	od_global_t *global;
	int offline;
	uint64_t init_time_us;
	bool synced_settings;

	od_list_t link;

	/* YB */
	bool yb_sticky_connection;
	bool yb_replication_connection;
	bool reset_timeout;
	/* is this an auth-backend? */
	bool yb_auth_backend;

	/* logical client version of the server. This field is populated 
	 * after backend is spawned.
	 */
	int64_t logical_client_version;

	/* If true, this server is marked for expiration to be
	 * eventually cleaned by cron job
	 */
	bool marked_for_close;

	/*
	 * The ID of the logical client that has sent parse message to the backend
	 * corresponding to this server object for an unnamed prepared statement. 
	 * This would be cleared after server gets detached.
	*/
	od_id_t yb_unnamed_prep_stmt_client_id;
};

static const size_t OD_SERVER_DEFAULT_HASHMAP_SZ = 420;

static inline void od_server_init(od_server_t *server, int reserve_prep_stmts)
{
	server->state = OD_SERVER_UNDEF;
	server->route = NULL;
	server->client = NULL;
	server->global = NULL;
	server->tls = NULL;
	server->idle_time = 0;
	server->is_allocated = 0;
	server->is_transaction = 0;
	server->done_fail_response_received = 0;
	server->in_out_response_received = 0;
	server->deploy_sync = 0;
	server->sync_request = 0;
	server->sync_reply = 0;
	server->init_time_us = machine_time_us();
	server->error_connect = NULL;
	server->offline = 0;
	server->synced_settings = false;
	server->endpoint_selector = 0;
	od_stat_state_init(&server->stats_state);
	server->yb_sticky_connection = false;
	server->yb_replication_connection = false;
	server->reset_timeout = false;
	server->yb_auth_backend = false;
	server->logical_client_version = 0;
	server->marked_for_close = false;

#ifdef USE_SCRAM
	od_scram_state_init(&server->scram_state);
#endif

	kiwi_key_init(&server->key);
	kiwi_key_init(&server->key_client);
	kiwi_vars_init(&server->vars, true);

	od_io_init(&server->io);
	od_relay_init(&server->relay, &server->io);
	od_list_init(&server->link);
	memset(&server->id, 0, sizeof(server->id));
	memset(&server->yb_unnamed_prep_stmt_client_id, 0,
	       sizeof(server->yb_unnamed_prep_stmt_client_id));

	if (reserve_prep_stmts) {
		server->prep_stmts =
			od_hashmap_create(OD_SERVER_DEFAULT_HASHMAP_SZ);
	} else {
		server->prep_stmts = NULL;
	}
}

static inline od_server_t *od_server_allocate(int reserve_prep_stmts)
{
	od_server_t *server = malloc(sizeof(*server));
	if (server == NULL)
		return NULL;
	od_server_init(server, reserve_prep_stmts);
	server->is_allocated = 1;
	return server;
}

static inline void od_server_free(od_server_t *server)
{
	if (server->is_allocated) {
		od_relay_free(&server->relay);
		od_io_free(&server->io);
		if (server->prep_stmts) {
			od_hashmap_free(server->prep_stmts);
		}
		if (server->vars.vars != NULL) {
			free(server->vars.vars);
			server->vars.vars = NULL;
		}
		free(server);
	}
}

static inline void od_server_sync_request(od_server_t *server, uint64_t count)
{
	server->sync_request += count;
}

static inline void od_server_sync_reply(od_server_t *server)
{
	server->sync_reply++;
}

static inline int od_server_in_deploy(od_server_t *server)
{
	return server->deploy_sync > 0;
}

static inline int od_server_synchronized(od_server_t *server)
{
	return server->sync_request == server->sync_reply;
}

static inline int od_server_grac_shutdown(od_server_t *server)
{
	server->offline = 1;
	return 0;
}

static inline int od_server_reload(od_attribute_unused() od_server_t *server)
{
	// TODO: set offline to 1 if storage/auth rules chaged
	return 0;
}

#endif /* ODYSSEY_SERVER_H */
