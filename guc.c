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

GucVariable conf[12];

/*
 * Define (or redefine) custom GUC variables.
 */
void
init_guc(void)
{
	int i = 0;
	conf[i++] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_max",
		.guc_desc = "Sets the maximum size of shared memory in (MB) used for statement's metadata tracked by pg_stat_monitor.",
		.guc_default = 100,
		.guc_min = 1,
		.guc_max = 1000,
		.guc_restart = true
		};

	conf[i++] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_query_max_len",
		.guc_desc = "Sets the maximum length of query.",
		.guc_default = 1024,
		.guc_min = 1024,
		.guc_max = INT_MAX,
		.guc_restart = true
	};
	conf[i++] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_enable",
		.guc_desc = "Enable/Disable statistics collector.",
		.guc_default = 1,
		.guc_min = 0,
		.guc_max = 0,
		.guc_restart = true
	};
	conf[i++] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_track_utility",
		.guc_desc = "Selects whether utility commands are tracked.",
		.guc_default = 0,
		.guc_min = 0,
		.guc_max = 0,
		.guc_restart = false
	};

	conf[i++] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_normalized_query",
		.guc_desc = "Selects whether save query in normalized format.",
		.guc_default = 1,
		.guc_min = 0,
		.guc_max = 0,
		.guc_restart = false
	};
	conf[i++] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_max_buckets",
		.guc_desc = "Sets the maximum number of buckets.",
		.guc_default = 10,
		.guc_min = 1,
		.guc_max = 10,
		.guc_restart = true
	};
	conf[i++] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_bucket_time",
		.guc_desc = "Sets the time in seconds per bucket.",
		.guc_default = 60,
		.guc_min = 1,
		.guc_max = INT_MAX,
		.guc_restart = true
	};

	conf[i++] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_respose_time_lower_bound",
		.guc_desc = "Sets the time in millisecond.",
		.guc_default = 1,
		.guc_min = 1,
		.guc_max = INT_MAX,
		.guc_restart = true
	};

	conf[i++] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_respose_time_step",
		.guc_desc = "Sets the response time steps in millisecond.",
		.guc_default = 1,
		.guc_min = 1,
		.guc_max = INT_MAX,
		.guc_restart = true
	};

	conf[i++] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_query_shared_buffer",
		.guc_desc = "Sets the maximum size of shared memory in (MB) used for query tracked by pg_stat_monitor.",
		.guc_default = 20,
		.guc_min = 1,
		.guc_max = 10000,
		.guc_restart = true
	};
#if PG_VERSION_NUM >= 130000
	conf[i++] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_track_planning",
		.guc_desc = "Selects whether planning statistics are tracked.",
		.guc_default = 1,
		.guc_min = 0,
		.guc_max = 0,
		.guc_restart = false
	};
#endif

	DefineCustomIntVariable("pg_stat_monitor.pgsm_max",
							"Sets the maximum size of shared memory in (MB) used for statement's metadata tracked by pg_stat_monitor.",
							NULL,
							&PGSM_MAX,
							100,
							1,
							1000,
							PGC_POSTMASTER,
							GUC_UNIT_MB,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pg_stat_monitor.pgsm_query_max_len",
							"Sets the maximum length of query.",
							NULL,
							&PGSM_QUERY_MAX_LEN,
							1024,
							1024,
							INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("pg_stat_monitor.pgsm_enable",
							 "Enable/Disable statistics collector.",
							 NULL,
							 (bool*)&PGSM_ENABLED,
							 true,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomBoolVariable("pg_stat_monitor.pgsm_track_utility",
							 "Selects whether utility commands are tracked by pg_stat_monitor.",
							 NULL,
							 (bool*)&PGSM_TRACK_UTILITY,
							 true,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomBoolVariable("pg_stat_monitor.pgsm_normalized_query",
							 "Selects whether save query in normalized format.",
							 NULL,
							 (bool*)&PGSM_NORMALIZED_QUERY,
							 true,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomIntVariable("pg_stat_monitor.pgsm_max_buckets",
							"Sets the maximum number of buckets.",
							NULL,
							&PGSM_MAX_BUCKETS,
							10,
							1,
							10,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pg_stat_monitor.pgsm_bucket_time",
							"Sets the time in seconds per bucket.",
							NULL,
							&PGSM_BUCKET_TIME,
							60,
							1,
							INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);


	DefineCustomIntVariable("pg_stat_monitor.pgsm_respose_time_lower_bound",
							"Sets the time in millisecond.",
							NULL,
							&PGSM_RESPOSE_TIME_LOWER_BOUND,
							1,
							1,
							INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pg_stat_monitor.pgsm_respose_time_step",
							"Sets the response time steps in millisecond.",
							NULL,
							&PGSM_RESPOSE_TIME_STEP,
							1,
							1,
							INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pg_stat_monitor.pgsm_query_shared_buffer",
							"Sets the maximum size of shared memory in (MB) used for query tracked by pg_stat_monitor.",
							NULL,
							&PGSM_QUERY_BUF_SIZE,
							20,
							1,
							10000,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("pg_stat_monitor.pgsm_track_planning",
							 "Selects whether track planning statistics.",
							 NULL,
							 (bool*)&PGSM_TRACK_PLANNING,
							 true,
							 PGC_SUSET,
							 0,
							 NULL,
							 NULL,
							 NULL);
}

GucVariable*
get_conf(int i)
{
	return  &conf[i];
}

