/*-------------------------------------------------------------------------
 *
 * guc.c
 *
 * Copyright (c) 2008-2018, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  contrib/pg_stat_monitor/guc.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "pg_stat_monitor.h"

/*
 * Define (or redefine) custom GUC variables.
 */
void
init_guc(void)
{
	DefineCustomIntVariable("pg_stat_monitor.max",
							"Sets the maximum number of statements tracked by pg_stat_monitor.",
							NULL,
							&pgsm_max,
							5000,
							5000,
							INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pg_stat_monitor.query_max_len",
							"Sets the maximum length of query",
							NULL,
							&pgsm_query_max_len,
							1024,
							1024,
							INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomEnumVariable("pg_stat_monitor.track",
							 "Selects which statements are tracked by pg_stat_monitor.",
							 NULL,
							 &pgsm_track,
							 pgsm_track_TOP,
							 track_options,
							 PGC_SUSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomBoolVariable("pg_stat_monitor.track_utility",
							 "Selects whether utility commands are tracked by pg_stat_monitor.",
							 NULL,
							 &pgsm_track_utility,
							 true,
							 PGC_SUSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomBoolVariable("pg_stat_monitor.normalized_query",
							 "Selects whether save query in normalized format.",
							 NULL,
							 &pgsm_normalized_query,
							 true,
							 PGC_SUSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomIntVariable("pg_stat_monitor.pgsm_max_buckets ",
							"Sets the maximum number of buckets.",
							NULL,
							&pgsm_max_buckets,
							10,
							1,
							10,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pg_stat_monitor.bucket_time",
							"Sets the time in seconds per bucket.",
							NULL,
							&pgsm_bucket_time,
							60,
							1,
							INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);


	DefineCustomIntVariable("pg_stat_monitor.pgsm_object_cache ",
							"Sets the maximum number of object cache",
							NULL,
							&pgsm_object_cache,
							5,
							5,
							10,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomRealVariable("pg_stat_monitor.pgsm_respose_time_lower_bound",
							"Sets the time in millisecond.",
							NULL,
							&pgsm_respose_time_lower_bound,
							.1,
							.1,
							INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomRealVariable("pg_stat_monitor.pgsm_respose_time_step",
							"Sets the respose time steps in millisecond.",
							NULL,
							&pgsm_respose_time_step,
							.1,
							.1,
							INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pg_stat_monitor.shared_buffer",
							"Sets the shared_buffer size",
							NULL,
							&pgsm_query_buf_size,
							500000,
							500000,
							INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

}

