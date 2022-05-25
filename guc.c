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

GucVariable conf[MAX_SETTINGS];
static void DefineIntGUC(GucVariable *conf);
static void DefineIntGUCWithCheck(GucVariable *conf, GucIntCheckHook check);
static void DefineBoolGUC(GucVariable *conf);
static void DefineEnumGUC(GucVariable *conf, const struct config_enum_entry *options);

/* Check hooks to ensure histogram_min < histogram_max */
static bool check_histogram_min(int *newval, void **extra, GucSource source);
static bool check_histogram_max(int *newval, void **extra, GucSource source);

/*
 * Define (or redefine) custom GUC variables.
 */
void
init_guc(void)
{
	int i = 0;

	conf[i] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_max",
		.guc_desc = "Sets the maximum size of shared memory in (MB) used for statement's metadata tracked by pg_stat_monitor.",
		.guc_default = 100,
		.guc_min = 1,
		.guc_max = 1000,
		.guc_restart = true,
		.guc_unit = GUC_UNIT_MB,
		.guc_value = &PGSM_MAX
		};
	DefineIntGUC(&conf[i++]);

	conf[i] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_query_max_len",
		.guc_desc = "Sets the maximum length of query.",
		.guc_default = 2048,
		.guc_min = 1024,
		.guc_max = INT_MAX,
		.guc_unit = 0,
		.guc_restart = true,
		.guc_value = &PGSM_QUERY_MAX_LEN
	};
	DefineIntGUC(&conf[i++]);

	conf[i] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_track_utility",
		.guc_desc = "Selects whether utility commands are tracked.",
		.guc_default = 1,
		.guc_min = 0,
		.guc_max = 0,
		.guc_restart = false,
		.guc_unit = 0,
		.guc_value = &PGSM_TRACK_UTILITY
	};
	DefineBoolGUC(&conf[i++]);

	conf[i] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_normalized_query",
		.guc_desc = "Selects whether save query in normalized format.",
		.guc_default = 1,
		.guc_min = 0,
		.guc_max = 0,
		.guc_restart = false,
		.guc_unit = 0,
		.guc_value = &PGSM_NORMALIZED_QUERY
	};
	DefineBoolGUC(&conf[i++]);

	conf[i] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_max_buckets",
		.guc_desc = "Sets the maximum number of buckets.",
		.guc_default = 10,
		.guc_min = 1,
		.guc_max = 10,
		.guc_restart = true,
		.guc_unit = 0,
		.guc_value = &PGSM_MAX_BUCKETS
	};
	DefineIntGUC(&conf[i++]);

	conf[i] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_bucket_time",
		.guc_desc = "Sets the time in seconds per bucket.",
		.guc_default = 60,
		.guc_min = 1,
		.guc_max = INT_MAX,
		.guc_restart = true,
		.guc_unit = 0,
		.guc_value = &PGSM_BUCKET_TIME
	};
	DefineIntGUC(&conf[i++]);

	conf[i] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_histogram_min",
		.guc_desc = "Sets the time in millisecond.",
		.guc_default = 0,
		.guc_min = 0,
		.guc_max = INT_MAX,
		.guc_restart = true,
		.guc_unit = 0,
		.guc_value = &PGSM_HISTOGRAM_MIN
	};
	DefineIntGUCWithCheck(&conf[i++], check_histogram_min);

	conf[i] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_histogram_max",
		.guc_desc = "Sets the time in millisecond.",
		.guc_default = 100000,
		.guc_min = 10,
		.guc_max = INT_MAX,
		.guc_restart = true,
		.guc_unit = 0,
		.guc_value = &PGSM_HISTOGRAM_MAX
	};
	DefineIntGUCWithCheck(&conf[i++], check_histogram_max);

	conf[i] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_histogram_buckets",
		.guc_desc = "Sets the maximum number of histogram buckets",
		.guc_default = 10,
		.guc_min = 2,
		.guc_max = MAX_RESPONSE_BUCKET,
		.guc_restart = true,
		.guc_unit = 0,
		.guc_value = &PGSM_HISTOGRAM_BUCKETS
	};
	DefineIntGUC(&conf[i++]);

	conf[i] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_query_shared_buffer",
		.guc_desc = "Sets the maximum size of shared memory in (MB) used for query tracked by pg_stat_monitor.",
		.guc_default = 20,
		.guc_min = 1,
		.guc_max = 10000,
		.guc_restart = true,
		.guc_unit = GUC_UNIT_MB,
		.guc_value = &PGSM_QUERY_SHARED_BUFFER
	};
	DefineIntGUC(&conf[i++]);

	conf[i] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_overflow_target",
		.guc_desc = "Sets the overflow target for pg_stat_monitor",
		.guc_default = 1,
		.guc_min = 0,
		.guc_max = 1,
		.guc_restart = true,
		.guc_unit = 0,
		.guc_value = &PGSM_OVERFLOW_TARGET
	};
	DefineIntGUC(&conf[i++]);

	conf[i] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_enable_query_plan",
		.guc_desc = "Enable/Disable query plan monitoring",
		.guc_default = 0,
		.guc_min = 0,
		.guc_max = 0,
		.guc_restart = false,
		.guc_unit = 0,
		.guc_value = &PGSM_QUERY_PLAN
	};
	DefineBoolGUC(&conf[i++]);

	conf[i] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_track",
		.guc_desc = "Selects which statements are tracked by pg_stat_monitor.",
		.n_options = 3,
		.guc_default = PGSM_TRACK_TOP,
		.guc_min = PSGM_TRACK_NONE,
		.guc_max = PGSM_TRACK_ALL,
		.guc_restart = false,
		.guc_unit = 0,
		.guc_value = &PGSM_TRACK
	};
	for (int j = 0; j < conf[i].n_options; ++j) {
		strlcpy(conf[i].guc_options[j], track_options[j].name, sizeof(conf[i].guc_options[j]));
	}
	DefineEnumGUC(&conf[i++], track_options);

	conf[i] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_extract_comments",
		.guc_desc = "Enable/Disable extracting comments from queries.",
		.guc_default = 0,
		.guc_min = 0,
		.guc_max = 0,
		.guc_restart = false,
		.guc_unit = 0,
		.guc_value = &PGSM_EXTRACT_COMMENTS
	};
	DefineBoolGUC(&conf[i++]);

#if PG_VERSION_NUM >= 130000
	conf[i] = (GucVariable) {
		.guc_name = "pg_stat_monitor.pgsm_track_planning",
		.guc_desc = "Selects whether planning statistics are tracked.",
		.guc_default = 0,
		.guc_min = 0,
		.guc_max = 0,
		.guc_restart = false,
		.guc_unit = 0,
		.guc_value = &PGSM_TRACK_PLANNING
	};
	DefineBoolGUC(&conf[i++]);
#endif
}

static void DefineIntGUCWithCheck(GucVariable *conf, GucIntCheckHook check)
{
	conf->type = PGC_INT;
	DefineCustomIntVariable(conf->guc_name,
							conf->guc_desc,
							NULL,
							conf->guc_value,
							conf->guc_default,
							conf->guc_min,
							conf->guc_max,
							conf->guc_restart ? PGC_POSTMASTER : PGC_USERSET,
							conf->guc_unit,
							check,
							NULL,
							NULL);
}

static void
DefineIntGUC(GucVariable *conf)
{
	DefineIntGUCWithCheck(conf, NULL);
}

static void
DefineBoolGUC(GucVariable *conf)
{
	conf->type = PGC_BOOL;
	DefineCustomBoolVariable(conf->guc_name,
							conf->guc_desc,
							NULL,
							(bool*)conf->guc_value,
							conf->guc_default,
							conf->guc_restart ? PGC_POSTMASTER : PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);
}

static void
DefineEnumGUC(GucVariable *conf, const struct config_enum_entry *options)
{
	conf->type = PGC_ENUM;
	DefineCustomEnumVariable(conf->guc_name,
                             conf->guc_desc,
                             NULL,
                             conf->guc_value,
                             conf->guc_default,
                             options,
                             conf->guc_restart ? PGC_POSTMASTER : PGC_USERSET,
                             0,
                             NULL,
                             NULL,
                             NULL);
}

GucVariable*
get_conf(int i)
{
	return &conf[i];
}

static bool check_histogram_min(int *newval, void **extra, GucSource source)
{
	/*
	 * During module initialization PGSM_HISTOGRAM_MIN is initialized before
	 * PGSM_HISTOGRAM_MAX, in this case PGSM_HISTOGRAM_MAX will be zero.
	 */
	return (PGSM_HISTOGRAM_MAX == 0 || *newval < PGSM_HISTOGRAM_MAX);
}

static bool check_histogram_max(int *newval, void **extra, GucSource source)
{
	return (*newval > PGSM_HISTOGRAM_MIN);
}
