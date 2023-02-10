#ifndef ODYSSEY_ERR_LOGGER_H
#define ODYSSEY_ERR_LOGGER_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

/* 300 = 5 * 60 */
#define DEFAULT_ERROR_INTERVAL_NUMBER 300

typedef struct od_error_logger od_error_logger_t;

struct od_error_logger {
	size_t intercals_cnt;

	pthread_mutex_t lock;

	size_t current_interval_num;
	// ISO C99 flexible array member
	od_counter_t *interval_counters[FLEXIBLE_ARRAY_MEMBER];
};

extern od_retcode_t od_error_logger_store_err(od_error_logger_t *l,
					      size_t err_t);

extern od_error_logger_t *od_err_logger_create(size_t intervals_count);

static inline od_error_logger_t *od_err_logger_create_default()
{
	return od_err_logger_create(DEFAULT_ERROR_INTERVAL_NUMBER);
}

od_retcode_t od_err_logger_free(od_error_logger_t *err_logger);

od_retcode_t od_err_logger_inc_interval(od_error_logger_t *l);

size_t od_err_logger_get_aggr_errors_count(od_error_logger_t *l, size_t err_t);

#endif // ODYSSEY_ERR_LOGGER_H
