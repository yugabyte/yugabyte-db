/*-------------------------------------------------------------------------
 *
 * guc.c: guc variable handling of pg_stat_monitor
 *
 * Portions Copyright © 2018-2020, Percona LLC and/or its affiliates
 *
 * Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group
 *
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  contrib/pg_stat_monitor/guc.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "pg_stat_monitor.h"

/* GUC variables */
int			pgsm_max;
int			pgsm_query_max_len;
int			pgsm_bucket_time;
int			pgsm_max_buckets;
int			pgsm_histogram_buckets;
double		pgsm_histogram_min;
double		pgsm_histogram_max;
int			pgsm_query_shared_buffer;
bool		pgsm_track_planning;
bool		pgsm_extract_comments;
bool		pgsm_enable_query_plan;
bool		pgsm_enable_overflow;
bool		pgsm_normalized_query;
bool		pgsm_track_utility;
bool		pgsm_enable_pgsm_query_id;
int			pgsm_track;
static int	pgsm_overflow_target;	/* Not used since 2.0 */

/* Check hooks to ensure histogram_min < histogram_max */
static bool check_histogram_min(double *newval, void **extra, GucSource source);
static bool check_histogram_max(double *newval, void **extra, GucSource source);
static bool check_overflow_targer(int *newval, void **extra, GucSource source);

/*
 * Define (or redefine) custom GUC variables.
 */
void
init_guc(void)
{
    pgsm_track = PGSM_TRACK_TOP;

	DefineCustomIntVariable("pg_stat_monitor.pgsm_max", /* name */
							"Sets the maximum size of shared memory in (MB) used for statement's metadata tracked by pg_stat_monitor.", /* short_desc */
							NULL,	/* long_desc */
							&pgsm_max,	/* value address */
							256,	/* boot value */
							10, /* min value */
							10240,	/* max value */
							PGC_POSTMASTER, /* context */
							GUC_UNIT_MB,	/* flags */
							NULL,	/* check_hook */
							NULL,	/* assign_hook */
							NULL	/* show_hook */
		);

	DefineCustomIntVariable("pg_stat_monitor.pgsm_query_max_len",	/* name */
							"Sets the maximum length of query.",	/* short_desc */
							NULL,	/* long_desc */
							&pgsm_query_max_len,	/* value address */
							2048,	/* boot value */
							1024,	/* min value */
							INT_MAX,	/* max value */
							PGC_POSTMASTER, /* context */
							0,	/* flags */
							NULL,	/* check_hook */
							NULL,	/* assign_hook */
							NULL	/* show_hook */
		);

	DefineCustomIntVariable("pg_stat_monitor.pgsm_max_buckets", /* name */
							"Sets the maximum number of buckets.",	/* short_desc */
							NULL,	/* long_desc */
							&pgsm_max_buckets,	/* value address */
							10, /* boot value */
							1,	/* min value */
							20000,	/* max value */
							PGC_POSTMASTER, /* context */
							0,	/* flags */
							NULL,	/* check_hook */
							NULL,	/* assign_hook */
							NULL	/* show_hook */
		);

	DefineCustomIntVariable("pg_stat_monitor.pgsm_bucket_time", /* name */
							"Sets the time in seconds per bucket.", /* short_desc */
							NULL,	/* long_desc */
							&pgsm_bucket_time,	/* value address */
							60, /* boot value */
							1,	/* min value */
							INT_MAX,	/* max value */
							PGC_POSTMASTER, /* context */
							GUC_UNIT_S, /* flags */
							NULL,	/* check_hook */
							NULL,	/* assign_hook */
							NULL	/* show_hook */
		);

	DefineCustomRealVariable("pg_stat_monitor.pgsm_histogram_min",	/* name */
							 "Sets the time in millisecond.",	/* short_desc */
							 NULL,	/* long_desc */
							 &pgsm_histogram_min,	/* value address */
							 1, /* boot value */
							 0, /* min value */
							 HISTOGRAM_MAX_TIME,	/* max value */
							 PGC_POSTMASTER,	/* context */
							 GUC_UNIT_MS,	/* flags */
							 check_histogram_min,	/* check_hook */
							 NULL,	/* assign_hook */
							 NULL	/* show_hook */
		);

	DefineCustomRealVariable("pg_stat_monitor.pgsm_histogram_max",	/* name */
							 "Sets the time in millisecond.",	/* short_desc */
							 NULL,	/* long_desc */
							 &pgsm_histogram_max,	/* value address */
							 100000.0,	/* boot value */
							 10.0,	/* min value */
							 HISTOGRAM_MAX_TIME,	/* max value */
							 PGC_POSTMASTER,	/* context */
							 GUC_UNIT_MS,	/* flags */
							 check_histogram_max,	/* check_hook */
							 NULL,	/* assign_hook */
							 NULL	/* show_hook */
		);

	DefineCustomIntVariable("pg_stat_monitor.pgsm_histogram_buckets",	/* name */
							"Sets the maximum number of histogram buckets.",	/* short_desc */
							NULL,	/* long_desc */
							&pgsm_histogram_buckets,	/* value address */
							20, /* boot value */
							2,	/* min value */
							MAX_RESPONSE_BUCKET,	/* max value */
							PGC_POSTMASTER, /* context */
							0,	/* flags */
							NULL,	/* check_hook */
							NULL,	/* assign_hook */
							NULL	/* show_hook */
		);

	DefineCustomIntVariable("pg_stat_monitor.pgsm_query_shared_buffer", /* name */
							"Sets the maximum size of shared memory in (MB) used for query tracked by pg_stat_monitor.",	/* short_desc */
							NULL,	/* long_desc */
							&pgsm_query_shared_buffer,	/* value address */
							20, /* boot value */
							1,	/* min value */
							10000,	/* max value */
							PGC_POSTMASTER, /* context */
							GUC_UNIT_MB,	/* flags */
							NULL,	/* check_hook */
							NULL,	/* assign_hook */
							NULL	/* show_hook */
		);

	/* deprecated in V 2.0 */
	DefineCustomIntVariable("pg_stat_monitor.pgsm_overflow_target", /* name */
							"Sets the overflow target for pg_stat_monitor. (Deprecated, use pgsm_enable_overflow)", /* short_desc */
							NULL,	/* long_desc */
							&pgsm_overflow_target,	/* value address */
							1,	/* boot value */
							0,	/* min value */
							1,	/* max value */
							PGC_POSTMASTER, /* context */
							0,	/* flags */
							check_overflow_targer,	/* check_hook */
							NULL,	/* assign_hook */
							NULL	/* show_hook */
		);


	DefineCustomBoolVariable("pg_stat_monitor.pgsm_track_utility",	/* name */
							 "Selects whether utility commands are tracked.",	/* short_desc */
							 NULL,	/* long_desc */
							 &pgsm_track_utility,	/* value address */
							 true,	/* boot value */
							 PGC_USERSET,	/* context */
							 0, /* flags */
							 NULL,	/* check_hook */
							 NULL,	/* assign_hook */
							 NULL	/* show_hook */
		);

	DefineCustomBoolVariable("pg_stat_monitor.pgsm_enable_pgsm_query_id",	/* name */
							 "Enable/disable PGSM specific query id calculation which is very useful in comparing same query across databases and clusters..",	/* short_desc */
							 NULL,	/* long_desc */
							 &pgsm_enable_pgsm_query_id,	/* value address */
							 true,	/* boot value */
							 PGC_USERSET,	/* context */
							 0, /* flags */
							 NULL,	/* check_hook */
							 NULL,	/* assign_hook */
							 NULL	/* show_hook */
		);

	DefineCustomBoolVariable("pg_stat_monitor.pgsm_normalized_query",	/* name */
							 "Selects whether save query in normalized format.",	/* short_desc */
							 NULL,	/* long_desc */
							 &pgsm_normalized_query,	/* value address */
							 false, /* boot value */
							 PGC_USERSET,	/* context */
							 0, /* flags */
							 NULL,	/* check_hook */
							 NULL,	/* assign_hook */
							 NULL	/* show_hook */
		);

	DefineCustomBoolVariable("pg_stat_monitor.pgsm_enable_overflow",	/* name */
							 "Enable/Disable pg_stat_monitor to grow beyond shared memory into swap space.",	/* short_desc */
							 NULL,	/* long_desc */
							 &pgsm_enable_overflow, /* value address */
							 true,	/* boot value */
							 PGC_POSTMASTER,	/* context */
							 0, /* flags */
							 NULL,	/* check_hook */
							 NULL,	/* assign_hook */
							 NULL	/* show_hook */
		);

	DefineCustomBoolVariable("pg_stat_monitor.pgsm_enable_query_plan",	/* name */
							 "Enable/Disable query plan monitoring.",	/* short_desc */
							 NULL,	/* long_desc */
							 &pgsm_enable_query_plan,	/* value address */
							 false, /* boot value */
							 PGC_USERSET,	/* context */
							 0, /* flags */
							 NULL,	/* check_hook */
							 NULL,	/* assign_hook */
							 NULL	/* show_hook */
		);

	DefineCustomBoolVariable("pg_stat_monitor.pgsm_extract_comments",	/* name */
							 "Enable/Disable extracting comments from queries.",	/* short_desc */
							 NULL,	/* long_desc */
							 &pgsm_extract_comments,	/* value address */
							 false, /* boot value */
							 PGC_USERSET,	/* context */
							 0, /* flags */
							 NULL,	/* check_hook */
							 NULL,	/* assign_hook */
							 NULL	/* show_hook */
		);

	DefineCustomEnumVariable("pg_stat_monitor.pgsm_track",	/* name */
							 "Selects which statements are tracked by pg_stat_monitor.",	/* short_desc */
							 NULL,	/* long_desc */
							 &pgsm_track,	/* value address */
							 PGSM_TRACK_TOP,	/* boot value */
							 track_options, /* enum options */
							 PGC_USERSET,	/* context */
							 0, /* flags */
							 NULL,	/* check_hook */
							 NULL,	/* assign_hook */
							 NULL	/* show_hook */
		);
#if PG_VERSION_NUM >= 130000
	DefineCustomBoolVariable("pg_stat_monitor.pgsm_track_planning", /* name */
							 "Selects whether planning statistics are tracked.",	/* short_desc */
							 NULL,	/* long_desc */
							 &pgsm_track_planning,	/* value address */
							 false, /* boot value */
							 PGC_USERSET,	/* context */
							 0, /* flags */
							 NULL,	/* check_hook */
							 NULL,	/* assign_hook */
							 NULL	/* show_hook */
		);
#endif

}

/* Maximum value must be greater or equal to minimum + 1.0 */
static bool
check_histogram_min(double *newval, void **extra, GucSource source)
{
	/*
	 * During module initialization PGSM_HISTOGRAM_MIN is initialized before
	 * PGSM_HISTOGRAM_MAX, in this case PGSM_HISTOGRAM_MAX will be zero.
	 */
	return (pgsm_histogram_max == 0 || (*newval + 1.0) <= pgsm_histogram_max);
}

static bool
check_histogram_max(double *newval, void **extra, GucSource source)
{
	return (*newval >= (pgsm_histogram_min + 1.0));
}

static bool
check_overflow_targer(int *newval, void **extra, GucSource source)
{
	if (source != PGC_S_DEFAULT)
		elog(WARNING, "pg_stat_monitor.pgsm_overflow_target is deprecated, use pgsm_enable_overflow");
	return true;
}
