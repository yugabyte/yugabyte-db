#ifndef ODYSSEY_CRON_H
#define ODYSSEY_CRON_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

typedef struct od_cron od_cron_t;

struct od_cron {
	uint64_t stat_time_us;
	od_global_t *global;
	od_atomic_u64_t startup_errors;

#ifdef PROM_FOUND
	od_prom_metrics_t *metrics;
#endif

#ifdef YB_SUPPORT_FOUND
	atomic_bool can_be_freed;
#else
	machine_wait_flag_t *can_be_freed;
#endif

	atomic_int online;
};

od_retcode_t od_cron_init(od_cron_t *);
int od_cron_start(od_cron_t *, od_global_t *);
od_retcode_t od_cron_stop(od_cron_t *cron);

#endif /* ODYSSEY_CRON_H */
