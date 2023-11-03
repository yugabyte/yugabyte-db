
#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

static size_t err_logger_required_buf_size(int sz)
{
	return sizeof(od_error_logger_t) + (sz * sizeof(od_counter_t));
}

od_error_logger_t *od_err_logger_create(size_t intervals_count)
{
	od_error_logger_t *err_logger =
		malloc(err_logger_required_buf_size(intervals_count));
	if (err_logger == NULL) {
		goto error;
	}

	err_logger->intercals_cnt = intervals_count;
	err_logger->current_interval_num = 0;

	for (size_t i = 0; i < intervals_count; ++i) {
		err_logger->interval_counters[i] = od_counter_create_default();
		if (err_logger->interval_counters[i] == NULL) {
			goto error;
		}
	}

	pthread_mutex_init(&err_logger->lock, NULL);

	return err_logger;
error:

	if (err_logger) {
		for (size_t i = 0; i < err_logger->intercals_cnt; ++i) {
			if (err_logger->interval_counters[i] == NULL)
				continue;
			od_counter_free(err_logger->interval_counters[i]);
		}

		pthread_mutex_destroy(&err_logger->lock);
		free((void *)(err_logger));
	}

	return NULL;
}

od_retcode_t od_err_logger_free(od_error_logger_t *err_logger)
{
	if (err_logger == NULL) {
		return OK_RESPONSE;
	}

	for (size_t i = 0; i < err_logger->intercals_cnt; ++i) {
		if (err_logger->interval_counters[i] == NULL) {
			continue;
		}
		int rc = od_counter_free(err_logger->interval_counters[i]);
		err_logger->interval_counters[i] = NULL;

		if (rc != OK_RESPONSE)
			return rc;
	}

	pthread_mutex_destroy(&err_logger->lock);
	free((void *)(err_logger));

	return OK_RESPONSE;
}

od_retcode_t od_error_logger_store_err(od_error_logger_t *l, size_t err_t)
{
	od_counter_inc(l->interval_counters[l->current_interval_num], err_t);
	return OK_RESPONSE;
}

od_retcode_t od_err_logger_inc_interval(od_error_logger_t *l)
{
	pthread_mutex_lock(&l->lock);
	{
		++l->current_interval_num;
		l->current_interval_num %= l->intercals_cnt;

		od_counter_reset_all(
			l->interval_counters[l->current_interval_num]);
	}
	pthread_mutex_unlock(&l->lock);

	return OK_RESPONSE;
}

size_t od_err_logger_get_aggr_errors_count(od_error_logger_t *l, size_t err_t)
{
	size_t ret_val = 0;
	for (size_t i = 0; i < l->intercals_cnt; ++i) {
		ret_val += od_counter_get_count(l->interval_counters[i], err_t);
	}
	return ret_val;
}
