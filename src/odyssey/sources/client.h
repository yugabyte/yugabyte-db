#ifndef ODYSSEY_CLIENT_H
#define ODYSSEY_CLIENT_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

typedef struct od_client_ctl od_client_ctl_t;
typedef struct od_client od_client_t;

typedef enum {
	OD_CLIENT_UNDEF,
	OD_CLIENT_PENDING,
	OD_CLIENT_ACTIVE,
	OD_CLIENT_QUEUE
} od_client_state_t;

typedef enum { OD_CLIENT_OP_NONE = 0, OD_CLIENT_OP_KILL = 1 } od_clientop_t;

struct od_client_ctl {
	od_atomic_u32_t op;
};

#define OD_CLIENT_MAX_PEERLEN 128

struct od_client {
	od_client_state_t state;
	od_pool_client_type_t type;
	od_id_t id;
	od_client_ctl_t ctl;
	uint64_t coroutine_id;
	machine_tls_t *tls;
	od_io_t io;
	machine_cond_t *cond;
	od_relay_t relay;
	machine_io_t *notify_io;
	od_rule_t *rule;
	od_config_listen_t *config_listen;

	uint64_t time_accept;
	uint64_t time_setup;
	uint64_t time_last_active;

	kiwi_be_startup_t startup;
	kiwi_vars_t vars;
	kiwi_key_t key;

	od_server_t *server;
	/* od_route_t */
	void *route;
	char peer[OD_CLIENT_MAX_PEERLEN];

	// desc preparet statements ids
	od_hashmap_t *prep_stmt_ids;

	/* passwd from config rule */
	kiwi_password_t password;

	/* user - proveded passwd, fallback to use this when no other option is available*/
	kiwi_password_t received_password;
	od_global_t *global;
	od_list_t link_pool;
	od_list_t link;

	/* storage_user & storage_password provided by ldapsearch result */
#ifdef LDAP_FOUND
	char *ldap_storage_username;
	int ldap_storage_username_len;
	char *ldap_storage_password;
	int ldap_storage_password_len;
	char *ldap_auth_dn;
#endif

	uint64_t client_id;
	int64_t yb_db_oid;
	kiwi_fe_error_t *deploy_err;
};

static const size_t OD_CLIENT_DEFAULT_HASHMAP_SZ = 420;

static inline od_retcode_t od_client_init_hm(od_client_t *client)
{
	client->prep_stmt_ids = od_hashmap_create(OD_CLIENT_DEFAULT_HASHMAP_SZ);
	if (client->prep_stmt_ids == NULL) {
		return NOT_OK_RESPONSE;
	}
	return OK_RESPONSE;
}

static inline void od_client_init(od_client_t *client)
{
	client->state = OD_CLIENT_UNDEF;
	client->type = OD_POOL_CLIENT_EXTERNAL;
	client->coroutine_id = 0;
	client->tls = NULL;
	client->cond = NULL;
	client->rule = NULL;
	client->config_listen = NULL;
	client->server = NULL;
	client->route = NULL;
	client->global = NULL;
	client->time_accept = 0;
	client->time_setup = 0;
	client->notify_io = NULL;
	client->ctl.op = OD_CLIENT_OP_NONE;
#ifdef LDAP_FOUND
	client->ldap_storage_username = NULL;
	client->ldap_storage_username_len = 0;
	client->ldap_storage_password = NULL;
	client->ldap_storage_password_len = 0;
	client->ldap_auth_dn = NULL;
#endif

	kiwi_be_startup_init(&client->startup);
	kiwi_vars_init(&client->vars);
	kiwi_key_init(&client->key);

	od_io_init(&client->io);
	od_relay_init(&client->relay, &client->io);

	kiwi_password_init(&client->password);
	kiwi_password_init(&client->received_password);

	od_list_init(&client->link_pool);
	od_list_init(&client->link);

	client->prep_stmt_ids = NULL;
	client->client_id = 0;
	client->yb_db_oid = -1;
	client->deploy_err = NULL;
}

static inline od_client_t *od_client_allocate(void)
{
	od_client_t *client = malloc(sizeof(*client));
	if (client == NULL)
		return NULL;
	od_client_init(client);
	return client;
}

static inline void od_client_free(od_client_t *client)
{
	od_relay_free(&client->relay);
	od_io_free(&client->io);
	if (client->cond)
		machine_cond_free(client->cond);
	kiwi_password_free(&client->password);
	kiwi_password_free(&client->received_password);
	if (client->prep_stmt_ids) {
		od_hashmap_free(client->prep_stmt_ids);
	}
	if (client->vars.vars) {
		free(client->vars.vars);
		client->vars.vars = NULL;
	}
	if (client->deploy_err) {
		free(client->deploy_err);
		client->deploy_err = NULL;
	}
	free(client);
}

static inline od_retcode_t od_client_notify_read(od_client_t *client)
{
	uint64_t value;
	return machine_read_raw(client->notify_io, &value, sizeof(value));
}

static inline void od_client_notify(od_client_t *client)
{
	uint64_t value = 1;
	size_t processed = 0;
	machine_write_raw(client->notify_io, &value, sizeof(value), &processed);
}

static inline uint32_t od_client_ctl_of(od_client_t *client)
{
	return od_atomic_u32_of(&client->ctl.op);
}

static inline void od_client_ctl_set(od_client_t *client, uint32_t op)
{
	od_atomic_u32_or(&client->ctl.op, op);
}

static inline void od_client_ctl_unset(od_client_t *client, uint32_t op)
{
	od_atomic_u32_xor(&client->ctl.op, op);
}

static inline void od_client_kill(od_client_t *client)
{
	od_client_ctl_set(client, OD_CLIENT_OP_KILL);
	od_client_notify(client);
}

#endif /* ODYSSEY_CLIENT_H */
