#ifndef ODYSSEY_ROUTE_POOL_H
#define ODYSSEY_ROUTE_POOL_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

typedef int (*od_route_pool_stat_cb_t)(od_route_t *route, od_stat_t *current,
				       od_stat_t *avg,
#ifdef PROM_FOUND
				       od_prom_metrics_t *metrics,
#endif
				       void **argv);

typedef int (*od_route_pool_stat_database_cb_t)(char *database,
						int database_len,
						od_stat_t *total,
						od_stat_t *avg, void **argv);

typedef od_retcode_t (*od_route_pool_stat_route_error_cb_t)(
	od_error_logger_t *l, void **argv);

typedef int (*od_route_pool_cb_t)(od_route_t *, void **);

typedef struct od_route_pool od_route_pool_t;

struct od_route_pool {
	/* used for counting error for client without concrete route
	 * like default_db.usr1, db1.default, etc
	 * */
	od_error_logger_t *err_logger;
	int count;
	pthread_mutex_t lock;

	od_list_t list;
};

#define od_route_pool_lock(route_pool) pthread_mutex_lock(&route_pool.lock);
#define od_route_pool_unlock(route_pool) pthread_mutex_unlock(&route_pool.lock);

typedef od_retcode_t (*od_route_pool_stat_frontend_error_cb_t)(
	od_route_pool_t *pool, void **argv);

static inline void od_route_pool_init(od_route_pool_t *pool)
{
	od_list_init(&pool->list);
	pool->err_logger = od_err_logger_create_default();
	pool->count = 0;
	pthread_mutex_init(&pool->lock, NULL);
}

static inline void od_route_pool_free(od_route_pool_t *pool)
{
	if (pool == NULL)
		return;

	pthread_mutex_destroy(&pool->lock);

	od_err_logger_free(pool->err_logger);
	od_list_t *i, *n;
	od_list_foreach_safe(&pool->list, i, n)
	{
		od_route_t *route;
		route = od_container_of(i, od_route_t, link);
		od_route_free(route);
	}
}

static inline od_route_t *od_route_pool_new(od_route_pool_t *pool,
					    od_route_id_t *id, od_rule_t *rule)
{
	od_route_t *route = od_route_allocate();
	if (route == NULL)
		return NULL;
	int rc;
	rc = od_route_id_copy(&route->id, id);
	if (rc == -1) {
		od_route_free(route);
		return NULL;
	}

	route->id.yb_stats_index = -1;

	route->rule = rule;
	if (rule->quantiles_count) {
		route->stats.enable_quantiles = true;
		for (size_t i = 0; i < QUANTILES_WINDOW; ++i) {
			route->stats.transaction_hgram[i] =
				td_new(QUANTILES_COMPRESSION);
			route->stats.query_hgram[i] =
				td_new(QUANTILES_COMPRESSION);
		}
	}
	od_list_append(&pool->list, &route->link);
	pool->count++;
	return route;
}

static inline int od_route_pool_foreach(od_route_pool_t *pool,
					od_route_pool_cb_t callback,
					void **argv)
{
	od_list_t *i, *n;
	od_list_foreach_safe(&pool->list, i, n)
	{
		od_route_t *route;
		route = od_container_of(i, od_route_t, link);
		int rc;
		rc = callback(route, argv);
		if (rc == -1)
			return -1;
		if (rc)
			return 1;
	}
	return 0;
}

static inline od_route_t *
od_route_pool_match(od_route_pool_t *pool, od_route_id_t *key, od_rule_t *rule)
{
	od_list_t *i;
	od_list_foreach(&pool->list, i)
	{
		od_route_t *route;
		route = od_container_of(i, od_route_t, link);
		if (route->rule == rule &&
		    od_route_id_compare(&route->id, key)) {
			return route;
		}
	}
	return NULL;
}

static inline void od_route_pool_stat(od_route_pool_t *pool,
				      uint64_t prev_time_us,
#ifdef PROM_FOUND
				      od_prom_metrics_t *metrics,
#endif
				      od_route_pool_stat_cb_t callback,
				      void **argv)
{
	od_list_t *i;
	od_list_foreach(&pool->list, i)
	{
		od_route_t *route;
		uint8_t next_tdigest;
		route = od_container_of(i, od_route_t, link);

		od_stat_t current;
		od_stat_init(&current);
		od_stat_copy(&current, &route->stats);

		/* calculate average */
		od_stat_t avg;
		od_stat_init(&avg);
		if (route->stats.enable_quantiles) {
			uint8_t current_tdigest = route->stats.current_tdigest;
			next_tdigest = (current_tdigest + 1) % QUANTILES_WINDOW;
			td_reset(route->stats.transaction_hgram[next_tdigest]);
			td_reset(route->stats.query_hgram[next_tdigest]);
			route->stats.current_tdigest = next_tdigest;
		}

		od_stat_average(&avg, &current, &route->stats_prev,
				prev_time_us);

		/* update route stats */
		od_stat_update(&route->stats_prev, &current);

		if (callback) {
			callback(route, &current, &avg,
#ifdef PROM_FOUND
				 metrics,
#endif
				 argv);
		}
	}
}

static inline void od_route_pool_stat_database_mark(od_route_pool_t *pool,
						    char *database,
						    int database_len,
						    od_stat_t *current,
						    od_stat_t *prev)
{
	od_list_t *i;
	od_list_foreach(&pool->list, i)
	{
		od_route_t *route;
		route = od_container_of(i, od_route_t, link);
		if (route->stats_mark_db)
			continue;
		if (route->id.database_len != database_len)
			continue;
		if (memcmp(route->id.database, database, database_len) != 0)
			continue;

		od_stat_sum(current, &route->stats);
		od_stat_sum(prev, &route->stats_prev);

		route->stats_mark_db = true;
	}
}

static inline void od_route_pool_stat_unmark_db(od_route_pool_t *pool)
{
	od_route_t *route;
	od_list_t *i;
	od_list_foreach(&pool->list, i)
	{
		route = od_container_of(i, od_route_t, link);
		route->stats_mark_db = false;
	}
}

static inline int
od_route_pool_stat_database(od_route_pool_t *pool,
			    od_route_pool_stat_database_cb_t callback,
			    uint64_t prev_time_us, void **argv)
{
	od_route_t *route;
	od_list_t *i;
	od_list_foreach(&pool->list, i)
	{
		route = od_container_of(i, od_route_t, link);
		if (route->stats_mark_db)
			continue;

		/* gather current and previous cron stats */
		od_stat_t current;
		od_stat_t prev;
		od_stat_init(&current);
		od_stat_init(&prev);
		od_route_pool_stat_database_mark(pool, route->id.database,
						 route->id.database_len,
						 &current, &prev);

		/* calculate average */
		od_stat_t avg;
		od_stat_init(&avg);
		od_stat_average(&avg, &current, &prev, prev_time_us);

		int rc;
		rc = callback(route->id.database, route->id.database_len - 1,
			      &current, &avg, argv);
		if (rc == -1) {
			od_route_pool_stat_unmark_db(pool);
			return -1;
		}
	}

	od_route_pool_stat_unmark_db(pool);
	return 0;
}

static inline od_retcode_t
od_route_pool_stat_err_frontend(od_route_pool_t *pool,
				od_route_pool_stat_frontend_error_cb_t callback,
				void **argv)
{
	return callback(pool, argv);
}

#endif /* ODYSSEY_ROUTE_POOL_H */
