#ifndef ODYSSEY_ROUTER_H
#define ODYSSEY_ROUTER_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

typedef struct od_router od_router_t;

struct od_router {
	pthread_mutex_t lock;

	od_rules_t rules;
	od_route_pool_t route_pool;
	/* clients */
	od_atomic_u32_t clients;
	od_atomic_u32_t clients_routing;
	/* servers */
	od_atomic_u32_t servers_routing;
	/* error logging */
	od_error_logger_t *router_err_logger;

	/* global */
	od_global_t *global;

	/* router has type of list */
	od_list_t servers;
};

#define od_router_lock(router) pthread_mutex_lock(&router->lock);
#define od_router_unlock(router) pthread_mutex_unlock(&router->lock);

void od_router_init(od_router_t *, od_global_t *);
void od_router_free(od_router_t *);

int od_router_reconfigure(od_router_t *, od_rules_t *);
int od_router_expire(od_router_t *, od_list_t *);
void od_router_gc(od_router_t *);
void od_router_stat(od_router_t *, uint64_t,
#ifdef PROM_FOUND
		    od_prom_metrics_t *,
#endif
		    od_route_pool_stat_cb_t, void **);

extern int od_router_foreach(od_router_t *, od_route_pool_cb_t, void **);

od_router_status_t od_router_route(od_router_t *router, od_client_t *client);

void od_router_unroute(od_router_t *, od_client_t *);

od_router_status_t od_router_attach(od_router_t *, od_client_t *, bool, od_client_t *);
void od_router_detach(od_router_t *, od_client_t *);
void od_router_close(od_router_t *, od_client_t *);

od_router_status_t od_router_cancel(od_router_t *, kiwi_key_t *,
				    od_router_cancel_t *);

void od_router_kill(od_router_t *, od_id_t *);

static inline int
od_route_pool_stat_err_router(od_router_t *router,
			      od_route_pool_stat_route_error_cb_t callback,
			      void **argv)
{
	return callback(router->router_err_logger, argv);
}

#endif /* ODYSSEY_ROUTER_H */
