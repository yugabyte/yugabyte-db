//
// Created by ein-krebs on 8/12/21.
//

#ifndef ODYSSEY_PROM_METRICS_H
#define ODYSSEY_PROM_METRICS_H
#include <prom.h>

typedef struct od_prom_metrics od_prom_metrics_t;

struct od_prom_metrics {
	prom_collector_registry_t *stat_general_metrics;
	prom_gauge_t *database_len;
	prom_gauge_t *user_len;
	prom_gauge_t *server_pool_active;
	prom_gauge_t *server_pool_idle;
	prom_gauge_t *msg_allocated;
	prom_gauge_t *msg_cache_count;
	prom_gauge_t *msg_cache_gc_count;
	prom_gauge_t *msg_cache_size;
	prom_gauge_t *count_coroutine;
	prom_gauge_t *count_coroutine_cache;
	prom_gauge_t *clients_processed;

	prom_collector_registry_t *stat_route_metrics;
	prom_gauge_t *client_pool_total;
	prom_gauge_t *avg_tx_count;
	prom_gauge_t *avg_tx_time;
	prom_gauge_t *avg_query_count;
	prom_gauge_t *avg_query_time;
	prom_gauge_t *avg_recv_client;
	prom_gauge_t *avg_recv_server;

	struct MHD_Daemon *http_server;
	int port;
};

extern int od_prom_metrics_init(od_prom_metrics_t *self);

int od_prom_set_port(int port, od_prom_metrics_t *self);

/* Activate metrics delivery via http*/
int od_prom_activate_general_metrics(od_prom_metrics_t *self);
int od_prom_activate_route_metrics(od_prom_metrics_t *self);

extern int od_prom_metrics_write_stat(od_prom_metrics_t *self,
				      u_int64_t msg_allocated,
				      u_int64_t msg_cache_count,
				      u_int64_t msg_cache_gc_count,
				      u_int64_t msg_cache_size,
				      u_int64_t count_coroutine,
				      u_int64_t count_coroutine_cache);

extern int od_prom_metrics_write_worker_stat(
	struct od_prom_metrics *self, int worker_id, u_int64_t msg_allocated,
	u_int64_t msg_cache_count, u_int64_t msg_cache_gc_count,
	u_int64_t msg_cache_size, u_int64_t count_coroutine,
	u_int64_t count_coroutine_cache, u_int64_t clients_processed);

extern const char *od_prom_metrics_get_stat(od_prom_metrics_t *self);

extern int od_prom_metrics_write_stat_cb(
	od_prom_metrics_t *self, const char *user, const char *database,
	u_int64_t database_len, u_int64_t user_len, u_int64_t client_pool_total,
	u_int64_t server_pool_active, u_int64_t server_pool_idle,
	u_int64_t avg_tx_count, u_int64_t avg_tx_time,
	u_int64_t avg_query_count, u_int64_t avg_query_time,
	u_int64_t avg_recv_client, u_int64_t avg_recv_server);

extern const char *od_prom_metrics_get_stat_cb(od_prom_metrics_t *self);

extern int od_prom_metrics_destroy(od_prom_metrics_t *self);

#endif //ODYSSEY_PROM_METRICS_H
