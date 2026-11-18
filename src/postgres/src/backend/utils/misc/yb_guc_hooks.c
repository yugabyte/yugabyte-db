/*-------------------------------------------------------------------------
 *
 * yb_guc_hooks.c
 *	  Check, assign and show hooks for YB GUCs that have no YB subsystem
 *	  file of their own.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *	  src/backend/utils/misc/yb_guc_hooks.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/parallel.h"
#include "access/xact.h"
#include "optimizer/cost.h"
#include "pg_yb_utils.h"
#include "postmaster/postmaster.h"
#include "replication/walsender.h"
#include "tcop/pquery.h"
#include "utils/guc_hooks.h"
#include "utils/guc_tables.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"
#include "utils/varlena.h"

/*
 * Defined in guc_tables.c next to the generated table; extern'd here because
 * the transaction-priority bound check hooks below cross-read the two bounds.
 */
extern double yb_transaction_priority_lower_bound;
extern double yb_transaction_priority_upper_bound;

bool
check_default_replica_identity(char **newval, void **extra, GucSource source)
{
	char	   *rawstring;
	bool		is_valid;

	rawstring = pstrdup(*newval);
	is_valid = (strcmp(rawstring, "FULL") == 0 ||
				strcmp(rawstring, "DEFAULT") == 0 ||
				strcmp(rawstring, "NOTHING") == 0 ||
				strcmp(rawstring, "CHANGE") == 0);

	pfree(rawstring);
	return is_valid;
}

bool
check_yb_explicit_row_locking_batch_size(int *newval, void **extra, GucSource source)
{
	return *newval > 0;
}

/*
 * For YB-managed (cloud), the cloud user won't be aware of superuser.
 * When YB shows max_connections, the connections reserved for superusers (and
 * other backends) are hidden from cloud users.
 * The reference of the relations can be found in postmaster.c.
 */
const char *
yb_show_maxconnections(void)
{
	static char buf[32];

	int64		yb_adj_max_con = MaxConnections;

	if (IsYugaByteEnabled() && !superuser())
	{
		yb_adj_max_con -= (SuperuserReservedConnections + max_wal_senders);
	}

	snprintf(buf, sizeof(buf), INT64_FORMAT, yb_adj_max_con);
	return buf;
}

bool
check_transaction_priority_lower_bound(double *newval, void **extra, GucSource source)
{
	if (*newval > yb_transaction_priority_upper_bound)
	{
		GUC_check_errdetail("must be less than or equal to yb_transaction_priority_upper_bound (%f).",
							yb_transaction_priority_upper_bound);
		return false;
	}

	if (IsYBReadCommitted() || YBIsWaitQueueEnabled())
	{
		ereport(NOTICE,
				(errmsg("priorities don't exist for read committed isolation transations, the "
						"transaction will wait for conflicting transactions to commit before "
						"proceeding"),
				 errdetail("This also applies to other isolation levels if using Wait-on-Conflict "
						   "concurrency control.")));
	}
	return true;
}

bool
check_transaction_priority_upper_bound(double *newval, void **extra, GucSource source)
{
	if (*newval < yb_transaction_priority_lower_bound)
	{
		GUC_check_errdetail("must be greater than or equal to yb_transaction_priority_lower_bound (%f).",
							yb_transaction_priority_lower_bound);
		return false;
	}

	if (IsYBReadCommitted() || YBIsWaitQueueEnabled())
	{
		ereport(NOTICE,
				(errmsg("priorities don't exist for read committed isolation transations, the "
						"transaction will wait for conflicting transactions to commit before "
						"proceeding"),
				 errdetail("This also applies to other isolation levels if using Wait-on-Conflict "
						   "concurrency control.")));
	}
	return true;
}

void
assign_yb_pg_batch_detection_mechanism(int new_value, void *extra)
{
	yb_pg_batch_detection_mechanism = new_value;
}

void
assign_ysql_upgrade_mode(bool newval, void *extra)
{
	/*
	 * YSQL upgrade mode also enables/disables allowSystemTableMods.
	 * Note that PG doesn't allow user to change it at runtime.
	 *
	 * While we reuse allowSystemTableMods to cut some corners for
	 * YSQL upgrade, we do alter the semantics of it to imitate tables
	 * created by initdb rather than by user.
	 */
	allowSystemTableMods = newval;
}

void
assign_yb_enable_cbo(int new_value, void *extra)
{
	yb_enable_base_scans_cost_model = false;
	yb_enable_optimizer_statistics = false;
	yb_ignore_stats = false;
	yb_legacy_bnl_cost = false;
	yb_use_cluster_config_for_geolocation_costing = false;

	switch (new_value)
	{
		case YB_COST_MODEL_OFF:
			yb_ignore_stats = true;
			break;

		case YB_COST_MODEL_ON:
			yb_enable_base_scans_cost_model = true;
			break;

		case YB_COST_MODEL_LEGACY:
			break;

		case YB_COST_MODEL_LEGACY_STATS:
			yb_enable_optimizer_statistics = true;
			break;

		case YB_COST_MODEL_LEGACY_BNL:
			yb_legacy_bnl_cost = true;
			break;

		case YB_COST_MODEL_LEGACY_STATS_BNL:
			yb_enable_optimizer_statistics = true;
			yb_legacy_bnl_cost = true;
			break;

		case YB_COST_MODEL_LEGACY_IGNORE_STATS_BNL:
			yb_legacy_bnl_cost = true;
			yb_ignore_stats = true;
			break;
	}

	/*
	 * When enabling CBO, also set:
	 *  - yb_enable_bitmapscan to on
	 *  - yb_parallel_range_rows to 10000
	 *  - yb_enable_update_reltuples_after_create_index to on
	 *  - yb_use_cluster_config_for_geolocation_costing to on
	 */
	if (new_value == YB_COST_MODEL_ON)
	{
		SetConfigOption("yb_enable_bitmapscan", "on",
						PGC_INTERNAL, PGC_S_DYNAMIC_DEFAULT);
		SetConfigOption("yb_parallel_range_rows", "10000",
						PGC_INTERNAL, PGC_S_DYNAMIC_DEFAULT);
		SetConfigOption("yb_enable_update_reltuples_after_create_index", "on",
						PGC_INTERNAL, PGC_S_DYNAMIC_DEFAULT);
		SetConfigOption("yb_use_cluster_config_for_geolocation_costing", "on",
						PGC_INTERNAL, PGC_S_DYNAMIC_DEFAULT);
	}
	/*
	 * When disabling CBO, also reset:
	 *  - yb_enable_bitmapscan
	 *  - yb_parallel_range_rows
	 *  - yb_enable_update_reltuples_after_create_index
	 *  - yb_use_cluster_config_for_geolocation_costing
	 */
	else
	{
		SetConfigOption("yb_enable_bitmapscan", "off",
						PGC_INTERNAL, PGC_S_DYNAMIC_DEFAULT);
		SetConfigOption("yb_parallel_range_rows", "0",
						PGC_INTERNAL, PGC_S_DYNAMIC_DEFAULT);
		SetConfigOption("yb_enable_update_reltuples_after_create_index", "off",
						PGC_INTERNAL, PGC_S_DYNAMIC_DEFAULT);
		SetConfigOption("yb_use_cluster_config_for_geolocation_costing", "off",
						PGC_INTERNAL, PGC_S_DYNAMIC_DEFAULT);
	}
}

void
assign_yb_enable_optimizer_statistics(bool new_value, void *extra)
{
	yb_enable_optimizer_statistics = new_value;

	yb_enable_cbo = (yb_enable_base_scans_cost_model ? YB_COST_MODEL_ON :
					 (new_value ?
					  YB_COST_MODEL_LEGACY_STATS : YB_COST_MODEL_LEGACY));

	yb_ignore_stats = false;
	yb_legacy_bnl_cost = false;
}

void
assign_yb_enable_base_scans_cost_model(bool new_value, void *extra)
{
	yb_enable_base_scans_cost_model = new_value;
	yb_enable_cbo = (new_value ? YB_COST_MODEL_ON :
					 (yb_enable_optimizer_statistics ?
					  YB_COST_MODEL_LEGACY_STATS :
					  YB_COST_MODEL_LEGACY));
	yb_ignore_stats = false;
	yb_legacy_bnl_cost = false;
}

bool
check_max_backoff(int *max_backoff_msecs, void **extra, GucSource source)
{
	if (*max_backoff_msecs < 0)
	{
		GUC_check_errdetail("must be greater than or equal to 0.");
		return false;
	}

	return true;
}

bool
check_min_backoff(int *min_backoff_msecs, void **extra, GucSource source)
{
	if (*min_backoff_msecs < 0)
	{
		GUC_check_errdetail("must be greater than or equal to 0.");
		return false;
	}

	return true;
}

bool
check_backoff_multiplier(double *multiplier, void **extra, GucSource source)
{
	if (*multiplier < 1)
	{
		GUC_check_errdetail("must be greater than or equal to 1.");
		return false;
	}

	return true;
}

bool
yb_check_toast_catcache_threshold(int *newVal, void **extra, GucSource source)
{
	if (*newVal != -1 && *newVal < 128)
	{
		GUC_check_errdetail("must greater than or equal to 128 bytes, or -1 to disable.");
		return false;
	}
	return true;
}

/*
 * yb_allow_prehashed_passwords is a cluster switch. It may be set from the
 * configuration file, for every role (ALTER ROLE ALL SET, applied as
 * PGC_S_GLOBAL), or by a superuser SET (PGC_S_SESSION). YSQL_CONN_MGR is the
 * same SET restored by the connection manager. PGC_S_TEST is the validation
 * pass; it does not say whether the target is one role or all roles, so
 * AlterSetting rejects the per-role and per-database forms.
 */
static bool
yb_prehashed_password_source_allowed(GucSource source)
{
	switch (source)
	{
		case PGC_S_DEFAULT:
		case PGC_S_FILE:
		case PGC_S_ARGV:
		case PGC_S_GLOBAL:
		case PGC_S_TEST:
		case PGC_S_SESSION:
		case YSQL_CONN_MGR:
			return true;
		default:
			return false;
	}
}

bool
check_yb_allow_prehashed_passwords(bool *newVal, void **extra, GucSource source)
{
	if (!yb_prehashed_password_source_allowed(source))
	{
		GUC_check_errdetail("This parameter can only be set via ysql_pg_conf_csv "
							"(config file), ALTER ROLE ALL SET, or SET.");
		return false;
	}

	/*
	 * ysql_pg_conf_csv is applied in file order, as PGC_S_FILE and the other
	 * sources below PGC_S_CLIENT. Rejecting a conflicting pair there keeps the
	 * postmaster from starting. CREATE and ALTER ROLE still refuse it, as do
	 * SET and the ALTER ROLE validation pass.
	 */
	if (*newVal &&
		source >= PGC_S_CLIENT &&
		(yb_password_reuse_history > 0 || yb_password_reuse_interval > 0))
	{
		GUC_check_errdetail("Pre-hashed passwords cannot be accepted while a password reuse "
							"policy is in force.");
		GUC_check_errhint("Set yb_password_reuse_history and yb_password_reuse_interval to 0 first.");
		return false;
	}
	return true;
}

/*
 * Password policy GUCs may be set for one role (ALTER ROLE ... SET, applied
 * as PGC_S_USER) or for every role (ALTER ROLE ALL SET, applied as
 * PGC_S_GLOBAL), and from the configuration file (ysql_pg_conf_csv,
 * PGC_S_FILE). Session, connection, and per-database sources are rejected.
 * PGC_S_TEST is the validation pass of ALTER ROLE ... SET.
 */
static bool
yb_password_policy_source_allowed(GucSource source)
{
	switch (source)
	{
		case PGC_S_DEFAULT:
		case PGC_S_FILE:
		case PGC_S_ARGV:
		case PGC_S_TEST:
		case PGC_S_USER:
		case PGC_S_GLOBAL:
			return true;
		default:
			return false;
	}
}

/*
 * Mutual exclusion with yb_allow_prehashed_passwords is enforced on the
 * ALTER ROLE validation pass. A conflicting value in the configuration file
 * is not rejected here, so it cannot keep a backend from starting;
 * CREATE/ALTER ROLE still refuses the combination.
 */
bool
check_yb_password_reuse(int *newVal, void **extra, GucSource source)
{
	if (!yb_password_policy_source_allowed(source))
	{
		GUC_check_errdetail("This parameter can only be set via ysql_pg_conf_csv "
							"(config file), ALTER ROLE ... SET, or ALTER ROLE ALL SET.");
		return false;
	}

	if (source == PGC_S_TEST &&
		*newVal > 0 && yb_allow_prehashed_passwords)
	{
		GUC_check_errdetail("A password reuse policy cannot be enforced while pre-hashed "
							"passwords are accepted.");
		GUC_check_errhint("Set yb_allow_prehashed_passwords to off first.");
		return false;
	}
	return true;
}

/*
 * YB: yb_check_no_txn
 *
 * Do not allow users to set yb_read_after_commit_visibility
 * from within a txn block.
 */
bool
yb_check_no_txn(int *newVal, void **extra, GucSource source)
{
	/* Just accept the value when restoring state in a parallel worker */
	if (InitializingParallelWorker)
		return true;

	if (IsTransactionBlock())
	{
		GUC_check_errdetail("Cannot be set within a txn block.");
		return false;
	}

	/*
	 * If YSQL Connection Manager is enabled, make the connection sticky
	 * for any variables that can only be set outside the context of an
	 * explicit transaction block.
	 */
	if (YbIsClientYsqlConnMgr())
	{
		elog(LOG, "Making connection sticky for non-transactional GUC variable");
		yb_ysql_conn_mgr_sticky_guc = true;
	}
	return true;
}

bool
yb_disable_auto_analyze_check_hook(bool *newval, void **extra, GucSource source)
{
	/*
	 * PGC_S_DEFAULT means that GUCs are being initialized during startup. PGC_S_TEST will be seen
	 * when GUCs are being tested for their setting when applying the setting on a per-database
	 * level. In both cases, we want to allow the setting to be changed.
	 */
	if (source == PGC_S_DEFAULT || source == PGC_S_TEST)
		return true;

	/*
	 * YB: This GUC is set by restore scripts generated via ysql_dump. It is used by the auto
	 * analyze service. However, when using connection manager, the GUC can become part of the
	 * deploy phase query (SET stmts are executed). So, we throw a warning instead of an error
	 * if the source is not PGC_S_DATABASE.
	 */

	if (source != PGC_S_DATABASE)
	{
		ereport(WARNING,
				(errmsg("can only be set on a database level using ALTER DATABASE SET. Current source: %s",
						GucSource_Names[source])));
	}
	return true;
}

static List *
yb_neg_catcache_ids_to_list(const char *cache_ids_str)
{
	char	   *rawstring = pstrdup(cache_ids_str);
	List	   *elemlist = NIL;

	if (!SplitIdentifierString(rawstring, ',', &elemlist) ||
		list_length(elemlist) == 0)
	{
		/* syntax error in list */
		GUC_check_errdetail("Expecting a comma separated string of syscache ids.");
		list_free(elemlist);
		pfree(rawstring);
		return NIL;
	}

	List	   *neg_cache_ids_list = NIL;
	ListCell   *l;

	foreach(l, elemlist)
	{
		char	   *endptr;
		long		cache_id = strtol((char *) lfirst(l), &endptr, 10);

		if (*endptr != '\0' || cache_id < 0 || cache_id > SysCacheSize)
		{
			GUC_check_errdetail("Expecting a comma separated string of syscache ids.");
			list_free(elemlist);
			pfree(rawstring);
			list_free(neg_cache_ids_list);
			return NIL;
		}
		neg_cache_ids_list = lappend_int(neg_cache_ids_list, cache_id);
	}
	list_free(elemlist);
	pfree(rawstring);
	return neg_cache_ids_list;
}

bool
yb_check_neg_catcache_ids(char **newval, void **extra, GucSource source)
{
	if (newval == NULL || *newval == NULL || strlen(*newval) == 0)
		return true;
	List	   *neg_cache_ids_list = yb_neg_catcache_ids_to_list(*newval);

	if (neg_cache_ids_list == NIL)
		return false;
	list_free(neg_cache_ids_list);
	return true;
}

void
yb_set_neg_catcache_ids(const char *newval, void *extra)
{
	List	   *neg_cache_ids_list = yb_neg_catcache_ids_to_list(newval);

	if (neg_cache_ids_list != NIL)
	{
		YbSetAdditionalNegCacheIds(neg_cache_ids_list);
		list_free(neg_cache_ids_list);
	}
}

/*
 * YB: Parse a comma-separated list of command tag names into a bool array
 * indexed by CommandTag. Used by yb_extra_commands_to_retry[_in_proc] check
 * hooks. On success, the caller-supplied *extra receives a guc_malloc'd bool
 * array with one entry per CommandTag which the assign hook later moves into
 * the runtime variable.
 *
 * Empty input is allowed (yields an all-false array). Embedded spaces within
 * tag names ("CREATE TABLE") are supported via SplitDirectoriesString. Tag
 * lookup is case-insensitive (GetCommandTagEnum). Unknown tags cause the
 * check to fail.
 *
 * Validates the whole list before any allocation, so error paths don't have
 * to free anything.
 */
static bool
yb_parse_command_tag_list(const char *list_str, void **extra)
{
	char	   *rawstring;
	List	   *elemlist;
	ListCell   *l;
	bool	   *arr;
	int			ntags = YbGetCommandTagCount();

	rawstring = pstrdup(list_str);

	if (!SplitDirectoriesString(rawstring, ',', &elemlist))
	{
		GUC_check_errdetail("List syntax is invalid.");
		pfree(rawstring);
		list_free_deep(elemlist);
		return false;
	}

	/* First pass: validate every tag name before allocating anything. */
	foreach(l, elemlist)
	{
		char	   *tok = (char *) lfirst(l);
		CommandTag	tag = GetCommandTagEnum(tok);

		if (tag == CMDTAG_UNKNOWN)
		{
			GUC_check_errdetail("Unrecognized command tag: \"%s\".", tok);
			pfree(rawstring);
			list_free_deep(elemlist);
			return false;
		}

		/*
		 * COPY/COPY FROM/ANALYZE are never safe to retry: re-executing
		 * COPY can double-apply rows, and ANALYZE errors out on retry.
		 * Reject the setting outright.
		 */
		if (tag == CMDTAG_COPY || tag == CMDTAG_COPY_FROM ||
			tag == CMDTAG_ANALYZE)
		{
			GUC_check_errdetail("\"%s\" cannot be retried "
								"(COPY may double-apply rows; "
								"ANALYZE errors out on retry).", tok);
			pfree(rawstring);
			list_free_deep(elemlist);
			return false;
		}
	}

	/* Second pass: allocate and populate the array now that all tags are known good. */
	arr = guc_malloc(ERROR, ntags * sizeof(bool));
	MemSet(arr, 0, ntags * sizeof(bool));
	foreach(l, elemlist)
		arr[GetCommandTagEnum((char *) lfirst(l))] = true;

	pfree(rawstring);
	list_free_deep(elemlist);

	*extra = arr;
	return true;
}

bool
yb_check_extra_commands_to_retry(char **newval, void **extra, GucSource source)
{
	return yb_parse_command_tag_list(*newval, extra);
}

void
yb_assign_extra_commands_to_retry(const char *newval, void *extra)
{
	yb_extra_commands_to_retry = (bool *) extra;
}

bool
yb_check_extra_commands_to_retry_in_proc(char **newval, void **extra,
										 GucSource source)
{
	return yb_parse_command_tag_list(*newval, extra);
}

void
yb_assign_extra_commands_to_retry_in_proc(const char *newval, void *extra)
{
	yb_extra_commands_to_retry_in_proc = (bool *) extra;
}

bool
check_yb_disable_pg_snapshot_mgmt_in_repeatable_read(bool *newval, void **extra, GucSource source)
{
	ereport(WARNING,
			(errmsg("the parameter \"yb_disable_pg_snapshot_mgmt_in_repeatable_read\" is deprecated, "
					"remove it from your configuration.")));
	return true;				/* still allow usage, but warn */
}

bool
check_yb_enable_advisory_locks(bool *newval, void **extra, GucSource source)
{
	ereport(WARNING,
			(errmsg("the parameter \"yb_enable_advisory_locks\" is deprecated, "
					"toggle the runtime flag \"ysql_yb_enable_advisory_locks\" instead.")));
	return true;				/* still allow usage, but warn */
}

bool
check_yb_silence_advisory_locks_not_supported_error(bool *newval, void **extra,
													GucSource source)
{
	ereport(WARNING,
			(errmsg("the parameter \"yb_silence_advisory_locks_not_supported_error\" is "
					"deprecated and has no effect. ")));
	return true;
}

void
assign_yb_enable_pg_stat_statements_rpc_stats(bool newval, void *extra)
{
	YbToggleSessionStatsTimer(newval);
}

/*
 * YB: check_skip_intents_internal
 * Common logic for skip-intent GUCs to handle transaction block restrictions.
 */
static bool
check_skip_intents_internal(const char *guc_name, bool *newval, GucSource source)
{
	/* Just accept the value when restoring state in a parallel worker */
	if (InitializingParallelWorker)
		return true;

	if (IsTransactionBlock() || FirstSnapshotSet)
	{
		GUC_check_errdetail("%s cannot be changed inside a transaction block or "
							"after any query has been run in the transaction.", guc_name);
		return false;
	}

	return true;
}

bool
check_yb_enable_new_relation_fastpath_write(bool *newval, void **extra, GucSource source)
{
	return check_skip_intents_internal("yb_enable_new_relation_fastpath_write", newval, source);
}

bool
check_yb_enable_new_relation_fastpath_write_in_txn_blocks(bool *newval, void **extra,
														  GucSource source)
{
	/*
	 * yb_enable_new_relation_fastpath_write gates the optimization as a whole,
	 * and this GUC only widens it to transaction blocks, so a value of on while
	 * the parent is off is inert rather than unsafe.
	 *
	 * Only values supplied once the postmaster has read its configuration are
	 * rejected, as in the yb_ddl_transaction_block_enabled check below.
	 * pg_wrapper generates ysql_pg.conf in the data directory, writing the
	 * ysql_pg_conf_csv entries ahead of the block it derives from the PG gflags,
	 * and postgres assigns the parameters in the order they appear in that file.
	 * A cluster that turns the parent off - through ysql_pg_conf_csv, or through
	 * the ysql_yb_enable_new_relation_fastpath_write gflag, which is the kill
	 * switch for the optimization as a whole - therefore has the parent assigned
	 * off before this GUC is assigned on, and rejecting that pair here would
	 * leave the postmaster refusing to start.
	 */
	if (*newval && !yb_enable_new_relation_fastpath_write && source >= PGC_S_CLIENT)
	{
		GUC_check_errdetail("Cannot enable yb_enable_new_relation_fastpath_write_in_txn_blocks "
							"when yb_enable_new_relation_fastpath_write is disabled.");
		return false;
	}

	/*
	 * A DDL inside a transaction block can only use the fastpath if it runs in
	 * the enclosing transaction, which requires transactional DDL. Otherwise
	 * this GUC would be silently ineffective.
	 *
	 * Only values supplied once the postmaster has read its configuration are
	 * checked: SET, the validation pass of ALTER ROLE/DATABASE ... SET, and
	 * connection request options. The configuration file is applied in file
	 * order, and yb_ddl_transaction_block_enabled may be assigned after this
	 * GUC - pg_wrapper writes ysql_pg_conf_csv entries ahead of the block it
	 * generates from the PG gflags - so checking there would reject a
	 * configuration whose final values are valid. The validator on the
	 * ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks gflag rejects the
	 * cluster-level combination instead, order-independently; a GUC enabled only
	 * through ysql_pg_conf_csv is left to no-op silently.
	 */
	if (*newval && !yb_ddl_transaction_block_enabled && source >= PGC_S_CLIENT)
	{
		GUC_check_errdetail("Cannot enable yb_enable_new_relation_fastpath_write_in_txn_blocks "
							"when yb_ddl_transaction_block_enabled is disabled.");
		return false;
	}

	return check_skip_intents_internal("yb_enable_new_relation_fastpath_write_in_txn_blocks", newval, source);
}

bool
yb_check_password_validity_source(int *newVal, void **extra, GucSource source)
{
	if (!yb_password_policy_source_allowed(source))
	{
		GUC_check_errdetail("yb_password_validity can only be set via "
							"ysql_pg_conf_csv (config file), or for a "
							"specific role via ALTER ROLE ... SET.");
		return false;
	}
	return true;
}
