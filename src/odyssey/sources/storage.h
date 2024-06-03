#ifndef ODYSSEY_RULE_STORAGE_H
#define ODYSSEY_RULE_STORAGE_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

typedef struct od_rule_storage od_rule_storage_t;
typedef struct od_storage_watchdog od_storage_watchdog_t;

/* Storage Watchdog */
typedef enum {
	OD_RULE_STORAGE_REMOTE,
	OD_RULE_STORAGE_LOCAL,
} od_rule_storage_type_t;

struct od_storage_watchdog {
	char *route_usr;
	char *route_db;

	char *storage_user;
	char *storage_db;

	char *query;
	int interval;
	int check_retry;

	/* soft shutdown on reload */
	pthread_mutex_t mu;
	int online;

	od_global_t *global;
};

od_storage_watchdog_t *od_storage_watchdog_allocate(od_global_t *);
int od_storage_watchdog_free(od_storage_watchdog_t *watchdog);

/* */
typedef struct od_storage_endpoint od_storage_endpoint_t;

struct od_storage_endpoint {
	char *host; /* NULL - terminated */
	int port; /* TODO: support somehow */
};

typedef enum {
	OD_TARGET_SESSION_ATTRS_RW,
	OD_TARGET_SESSION_ATTRS_RO,
	OD_TARGET_SESSION_ATTRS_ANY,
} od_target_session_attrs_t;

struct od_rule_storage {
	od_tls_opts_t *tls_opts;

	char *name;
	char *type;
	od_rule_storage_type_t storage_type;
	/* round-robin atomic counter for endpoint selection */
	od_atomic_u32_t rr_counter;

	od_storage_endpoint_t *endpoints;
	size_t endpoints_count;

	char *host; /* host or host,host or [host]:port[,host...] */
	int port; /* default port */

	od_target_session_attrs_t target_session_attrs;

	int server_max_routing;
	od_storage_watchdog_t *watchdog;

	od_list_t link;
};

/* storage API */
od_rule_storage_t *od_rules_storage_allocate(void);
od_rule_storage_t *od_rules_storage_copy(od_rule_storage_t *);

void od_rules_storage_free(od_rule_storage_t *);

/* watchdog */
void od_storage_watchdog_watch(void *arg);

#endif /* ODYSSEY_RULE_STORAGE_H */
