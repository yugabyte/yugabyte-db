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
	int i = 0;
	conf[i++] = (GucVariable) { 
		.guc_name = "pg_stat_monitor.pgsm_max",
		.guc_desc = "Sets the maximum number of statements tracked by pg_stat_monitor.",
		.guc_default = 5000,
		.guc_min = 5000,
		.guc_max = INT_MAX,
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
		.guc_name = "pg_stat_monitor.pgsm_track",
		.guc_desc = "Selects which statements are tracked by pg_stat_monitor.",
		.guc_default = 0,
		.guc_min = 0,
		.guc_max = 0,
		.guc_restart = false
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
		.guc_default = 0,
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
		.guc_name = "pg_stat_monitor.pgsm_object_cache",
		.guc_desc = "Sets the maximum number of object cache",
		.guc_default = 5,
		.guc_min = 5,
		.guc_max = 10,
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
		.guc_desc = "Sets the respose time steps in millisecond.",
		.guc_default = 1,
		.guc_min = 1,
		.guc_max = INT_MAX,
		.guc_restart = true
	};

	conf[i++] = (GucVariable) { 
		.guc_name = "pg_stat_monitor.shared_buffer",
		.guc_desc = "Sets the shared_buffer size.",
		.guc_default = 500000,
		.guc_min = 500000,
		.guc_max = INT_MAX,
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
	
	DefineCustomIntVariable("pg_stat_monitor.max",
							"Sets the maximum number of statements tracked by pg_stat_monitor.",
							NULL,
							&PGSM_MAX,
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
							&PGSM_QUERY_MAX_LEN,
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
							 &PGSM_TRACK,
							 PGSM_TRACK_TOP,
							 track_options,
							 PGC_SUSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomBoolVariable("pg_stat_monitor.track_utility",
							 "Selects whether utility commands are tracked by pg_stat_monitor.",
							 NULL,
							 (bool*)&PGSM_TRACK_UTILITY,
							 true,
							 PGC_SUSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomBoolVariable("pg_stat_monitor.normalized_query",
							 "Selects whether save query in normalized format.",
							 NULL,
							 (bool*)&PGSM_NORMALIZED_QUERY,
							 true,
							 PGC_SUSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	DefineCustomIntVariable("pg_stat_monitor.pgsm_max_buckets ",
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

	DefineCustomIntVariable("pg_stat_monitor.bucket_time",
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


	DefineCustomIntVariable("pg_stat_monitor.pgsm_object_cache ",
							"Sets the maximum number of object cache",
							NULL,
							&PGSM_OBJECT_CACHE,
							5,
							5,
							10,
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
							"Sets the respose time steps in millisecond.",
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

	DefineCustomIntVariable("pg_stat_monitor.pgsm_query_shared_buffer.",
							"Sets the shared_buffer size",
							NULL,
							&PGSM_QUERY_BUF_SIZE,
							500000,
							500000,
							INT_MAX,
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

