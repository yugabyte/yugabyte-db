//
// Created by ein-krebs on 8/12/21.
//

#include <prom_metrics.h>
#include <prom.h>
#include <assert.h>
#include <odyssey.h>
#include <stdio.h>

#ifdef PROMHTTP_FOUND
#include <promhttp.h>

int od_prom_AcceptPolicyCallback(void *cls, const struct sockaddr *addr,
				 socklen_t addrlen)
{
	return MHD_YES;
}

int od_prom_switch_server_on(od_prom_metrics_t *self)
{
	if (self->http_server)
		return NOT_OK_RESPONSE;
	self->http_server = promhttp_start_daemon(
		MHD_USE_DUAL_STACK | MHD_USE_AUTO_INTERNAL_THREAD, self->port,
		od_prom_AcceptPolicyCallback, NULL);
	return self->http_server ? OK_RESPONSE : NOT_OK_RESPONSE;
}

int od_prom_set_port(int port, od_prom_metrics_t *self)
{
	if (!self)
		return NOT_OK_RESPONSE;
	if (port > 0 && !self->http_server) {
		self->port = port;
	} else {
		return port > 0 ? OK_RESPONSE : NOT_OK_RESPONSE;
	}
	return OK_RESPONSE;
}

int od_prom_activate_general_metrics(od_prom_metrics_t *self)
{
	if (!self)
		return NOT_OK_RESPONSE;
	promhttp_set_active_collector_registry(self->stat_general_metrics);
	if (!self->http_server && self->port > 0) {
		return od_prom_switch_server_on(self);
	} else if (self->port <= 0) {
		return NOT_OK_RESPONSE;
	}
	return OK_RESPONSE;
}

int od_prom_activate_route_metrics(od_prom_metrics_t *self)
{
	if (!self)
		return NOT_OK_RESPONSE;
	promhttp_set_active_collector_registry(NULL);
	if (!self->http_server && self->port > 0) {
		return od_prom_switch_server_on(self);
	} else if (self->port <= 0) {
		return NOT_OK_RESPONSE;
	}
	return OK_RESPONSE;
}
#endif

int od_prom_metrics_init(struct od_prom_metrics *self)
{
	if (self == NULL)
		return 0;

	self->stat_general_metrics =
		prom_collector_registry_new("stat_general_metrics");
	prom_collector_t *stat_general_metrics_collector =
		prom_collector_new("stat_general_metrics_collector");
	int err = prom_collector_registry_register_collector(
		self->stat_general_metrics, stat_general_metrics_collector);
	if (err)
		return err;
	self->database_len = prom_gauge_new("database_len",
					    "Total databases count", 0, NULL);
	prom_collector_add_metric(stat_general_metrics_collector,
				  self->database_len);
	self->server_pool_active = prom_gauge_new(
		"server_pool_active", "Active servers count", 0, NULL);
	prom_collector_add_metric(stat_general_metrics_collector,
				  self->server_pool_active);
	self->server_pool_idle = prom_gauge_new("sever_pool_idle",
						"Idle servers count", 0, NULL);
	prom_collector_add_metric(stat_general_metrics_collector,
				  self->server_pool_idle);
	self->user_len =
		prom_gauge_new("user_len", "Total users count", 0, NULL);
	prom_collector_add_metric(stat_general_metrics_collector,
				  self->user_len);

	prom_collector_t *stat_worker_metrics_collector =
		prom_collector_new("stat_metrics_collector");
	err = prom_collector_registry_register_collector(
		self->stat_general_metrics, stat_worker_metrics_collector);
	if (err)
		return err;
	const char *worker_label[1] = { "worker" };
	self->msg_allocated = prom_gauge_new(
		"msg_allocated", "Messages allocated", 1, worker_label);
	prom_collector_add_metric(stat_worker_metrics_collector,
				  self->msg_allocated);
	self->msg_cache_count = prom_gauge_new(
		"msg_cache_count", "Messages cached", 1, worker_label);
	prom_collector_add_metric(stat_worker_metrics_collector,
				  self->msg_cache_count);
	self->msg_cache_gc_count = prom_gauge_new(
		"msg_cache_gc_count", "Messages freed", 1, worker_label);
	prom_collector_add_metric(stat_worker_metrics_collector,
				  self->msg_cache_gc_count);
	self->msg_cache_size = prom_gauge_new(
		"msg_cache_size", "Messages cache size", 1, worker_label);
	prom_collector_add_metric(stat_worker_metrics_collector,
				  self->msg_cache_size);
	self->count_coroutine = prom_gauge_new(
		"count_coroutine", "Coroutines running", 1, worker_label);
	prom_collector_add_metric(stat_worker_metrics_collector,
				  self->count_coroutine);
	self->count_coroutine_cache = prom_gauge_new(
		"count_coroutine_cache", "Coroutines cached", 1, worker_label);
	prom_collector_add_metric(stat_worker_metrics_collector,
				  self->count_coroutine_cache);
	self->clients_processed =
		prom_gauge_new("clients_processed",
			       "Number of processed clients", 1, worker_label);
	prom_collector_add_metric(stat_worker_metrics_collector,
				  self->clients_processed);

	self->stat_route_metrics =
		prom_collector_registry_new("stat_route_metrics");
	prom_collector_t *stat_database_metrics_collector =
		prom_collector_new("stat_database_metrics_collector");
	err = prom_collector_registry_register_collector(
		self->stat_route_metrics, stat_database_metrics_collector);
	if (err)
		return err;
	const char *database_labels[1] = { "database" };
	self->client_pool_total = prom_gauge_new("client_pool_total",
						 "Total database clients count",
						 1, database_labels);
	prom_collector_add_metric(stat_database_metrics_collector,
				  self->client_pool_total);

	prom_collector_t *stat_route_metrics_collector =
		prom_collector_new("stat_user_metrics_collector");
	err = prom_collector_registry_register_collector(
		self->stat_route_metrics, stat_route_metrics_collector);
	if (err)
		return err;
	const char *user_labels[2] = { "user", "database" };
	self->avg_tx_count =
		prom_gauge_new("avg_tx_count",
			       "Average transactions count per second", 2,
			       user_labels);
	prom_collector_add_metric(stat_route_metrics_collector,
				  self->avg_tx_count);
	self->avg_tx_time = prom_gauge_new("avg_tx_time",
					   "Average transaction time in usec",
					   2, user_labels);
	prom_collector_add_metric(stat_route_metrics_collector,
				  self->avg_tx_time);
	self->avg_query_count = prom_gauge_new("avg_query_count",
					       "Average query count per second",
					       2, user_labels);
	prom_collector_add_metric(stat_route_metrics_collector,
				  self->avg_query_count);
	self->avg_query_time = prom_gauge_new(
		"avg_query_time", "Average query time in usec", 2, user_labels);
	prom_collector_add_metric(stat_route_metrics_collector,
				  self->avg_query_time);
	self->avg_recv_client = prom_gauge_new(
		"avg_recv_client", "Average in bytes/sec", 2, user_labels);
	prom_collector_add_metric(stat_route_metrics_collector,
				  self->avg_recv_client);
	self->avg_recv_server = prom_gauge_new(
		"avg_recv_server", "Average out bytes/sec", 2, user_labels);
	prom_collector_add_metric(stat_route_metrics_collector,
				  self->avg_recv_server);

	prom_collector_registry_default_init();
	prom_collector_registry_register_collector(
		PROM_COLLECTOR_REGISTRY_DEFAULT,
		stat_general_metrics_collector);
	prom_collector_registry_register_collector(
		PROM_COLLECTOR_REGISTRY_DEFAULT, stat_worker_metrics_collector);
	prom_collector_registry_register_collector(
		PROM_COLLECTOR_REGISTRY_DEFAULT,
		stat_database_metrics_collector);
	prom_collector_registry_register_collector(
		PROM_COLLECTOR_REGISTRY_DEFAULT, stat_route_metrics_collector);
	return 0;
}

int od_prom_metrics_write_stat(struct od_prom_metrics *self,
			       u_int64_t msg_allocated,
			       u_int64_t msg_cache_count,
			       u_int64_t msg_cache_gc_count,
			       u_int64_t msg_cache_size,
			       u_int64_t count_coroutine,
			       u_int64_t count_coroutine_cache)
{
	if (self == NULL)
		return 1;
	const char *labels[1] = { "general" };
	int err;
	err = prom_gauge_set(self->msg_allocated, (double)msg_allocated,
			     labels);
	if (err)
		return err;
	err = prom_gauge_set(self->msg_cache_count, (double)msg_cache_count,
			     labels);
	if (err)
		return err;
	err = prom_gauge_set(self->msg_cache_gc_count,
			     (double)msg_cache_gc_count, labels);
	if (err)
		return err;
	err = prom_gauge_set(self->msg_cache_size, (double)msg_cache_size,
			     labels);
	if (err)
		return err;
	err = prom_gauge_set(self->count_coroutine, (double)count_coroutine,
			     labels);
	if (err)
		return err;
	err = prom_gauge_set(self->count_coroutine_cache,
			     (double)count_coroutine_cache, labels);
	if (err)
		return err;
	return 0;
}

int od_prom_metrics_write_worker_stat(
	struct od_prom_metrics *self, int worker_id, u_int64_t msg_allocated,
	u_int64_t msg_cache_count, u_int64_t msg_cache_gc_count,
	u_int64_t msg_cache_size, u_int64_t count_coroutine,
	u_int64_t count_coroutine_cache, u_int64_t clients_processed)
{
	if (self == NULL)
		return 1;
	char worker_label[12];
	sprintf(worker_label, "worker[%d]", worker_id);
	const char *labels[1] = { worker_label };
	int err;
	err = prom_gauge_set(self->msg_allocated, (double)msg_allocated,
			     labels);
	if (err)
		return err;
	err = prom_gauge_set(self->msg_cache_count, (double)msg_cache_count,
			     labels);
	if (err)
		return err;
	err = prom_gauge_set(self->msg_cache_gc_count,
			     (double)msg_cache_gc_count, labels);
	if (err)
		return err;
	err = prom_gauge_set(self->msg_cache_size, (double)msg_cache_size,
			     labels);
	if (err)
		return err;
	err = prom_gauge_set(self->count_coroutine, (double)count_coroutine,
			     labels);
	if (err)
		return err;
	err = prom_gauge_set(self->count_coroutine_cache,
			     (double)count_coroutine_cache, labels);
	if (err)
		return err;
	err = prom_gauge_set(self->clients_processed, (double)clients_processed,
			     labels);
	if (err)
		return err;
	return 0;
}

const char *od_prom_metrics_get_stat(od_prom_metrics_t *self)
{
	if (self == NULL)
		return NULL;
	return prom_collector_registry_bridge(self->stat_general_metrics);
}

int od_prom_metrics_write_stat_cb(
	od_prom_metrics_t *self, const char *user, const char *database,
	u_int64_t database_len, u_int64_t user_len, u_int64_t client_pool_total,
	u_int64_t server_pool_active, u_int64_t server_pool_idle,
	u_int64_t avg_tx_count, u_int64_t avg_tx_time,
	u_int64_t avg_query_count, u_int64_t avg_query_time,
	u_int64_t avg_recv_client, u_int64_t avg_recv_server)
{
	if (self == NULL)
		return 1;
	const char *database_label[1] = { database };
	const char *user_database_label[2] = { user, database };
	int err =
		prom_gauge_set(self->database_len, (double)database_len, NULL);
	if (err)
		return err;
	err = prom_gauge_set(self->user_len, (double)user_len, NULL);
	if (err)
		return err;
	err = prom_gauge_set(self->client_pool_total, (double)client_pool_total,
			     database_label);
	if (err)
		return err;
	err = prom_gauge_set(self->server_pool_active,
			     (double)server_pool_active, NULL);
	if (err)
		return err;
	err = prom_gauge_set(self->server_pool_idle, (double)server_pool_idle,
			     NULL);
	if (err)
		return err;
	err = prom_gauge_set(self->avg_tx_count, (double)avg_tx_count,
			     user_database_label);
	if (err)
		return err;
	err = prom_gauge_set(self->avg_tx_time, (double)avg_tx_time,
			     user_database_label);
	if (err)
		return err;
	err = prom_gauge_set(self->avg_query_count, (double)avg_query_count,
			     user_database_label);
	if (err)
		return err;
	err = prom_gauge_set(self->avg_query_time, (double)avg_query_time,
			     user_database_label);
	if (err)
		return err;
	err = prom_gauge_set(self->avg_recv_server, (double)avg_recv_server,
			     user_database_label);
	if (err)
		return err;
	err = prom_gauge_set(self->avg_recv_client, (double)avg_recv_client,
			     user_database_label);
	if (err)
		return err;
	return 0;
}

extern const char *od_prom_metrics_get_stat_cb(od_prom_metrics_t *self)
{
	if (self == NULL)
		return NULL;
	return prom_collector_registry_bridge(self->stat_route_metrics);
}

extern int od_prom_metrics_destroy(od_prom_metrics_t *self)
{
	if (self == NULL)
		return 1;
	prom_collector_registry_destroy(self->stat_general_metrics);
	self->stat_general_metrics = NULL;

	prom_collector_registry_destroy(self->stat_route_metrics);
	self->stat_route_metrics = NULL;

	free(self);
	return 0;
}
