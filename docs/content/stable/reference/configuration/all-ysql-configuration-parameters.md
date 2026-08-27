---
title: All YSQL configuration parameters
headerTitle: All YSQL configuration parameters
linkTitle: All YSQL parameters
description: Reference list of all YugabyteDB-specific YSQL configuration parameters.
menu:
  stable:
    identifier: all-ysql-configuration-parameters
    parent: configuration
    weight: 2460
type: docs
showRightNav: true
---

YSQL supports the PostgreSQL [server configuration parameters](https://www.postgresql.org/docs/15/runtime-config.html), plus the YugabyteDB-specific parameters listed on this page. This page covers every `yb_` parameter that v2026.1.1.1 exposes. Parameters that need more than a one-line description also have an entry under [YSQL configuration parameters](../yb-tserver/#ysql-configuration-parameters) on the YB-TServer reference page; the parameter names below link to that entry where one exists.

To see the parameters and their current values on a running cluster, query `pg_settings`:

```sql
SELECT name, setting, unit, context, short_desc FROM pg_settings WHERE name LIKE 'yb\_%';
```

## Setting a parameter

You can set a parameter at cluster, database, role, session, or statement scope. Narrower scopes override wider ones. For the precedence rules and the equivalent yb-tserver flags, see [How to modify configuration parameters](../yb-tserver/#how-to-modify-configuration-parameters).

```sql
ALTER DATABASE mydb SET yb_fetch_row_limit = 2048;    -- per database
ALTER ROLE myrole SET yb_fetch_row_limit = 2048;      -- per role
SET yb_fetch_row_limit = 2048;                        -- current session
SET LOCAL yb_fetch_row_limit = 2048;                  -- current transaction
```

To set a parameter for the whole cluster, use the yb-tserver [--ysql_pg_conf_csv](../yb-tserver/#ysql-pg-conf-csv) flag, for example `--ysql_pg_conf_csv=yb_fetch_row_limit=2048`.

## Reading the tables

| Column | Meaning |
| :--- | :--- |
| Parameter | Parameter name, as it appears in `pg_settings`. |
| Description | Description reported by `pg_settings.short_desc` and `extra_desc`. |
| Type | `bool`, `integer`, `real`, `string`, or `enum`. |
| Default | Built-in default (`pg_settings.boot_val`). A deployment can start with a different value if it is set using a flag, so check `pg_settings.reset_val` on your cluster. |
| Unit | Unit the value is interpreted in, where the parameter has one. |
| Context | When the parameter can be set. See the following table. |

The context determines who can change a parameter and whether a restart is needed.

| Context | Who can set it | Takes effect |
| :--- | :--- | :--- |
| `user` | Any user, for their own session | Immediately |
| `superuser` | Superusers only | Immediately |
| `backend` | Set when the connection is established | At connection start |
| `sighup` | Cluster configuration only (yb-tserver flag) | On configuration reload; no restart needed |
| `postmaster` | Cluster configuration only (yb-tserver flag) | Requires a restart of the YSQL process |
| `internal` | Read-only | Cannot be changed |

Parameters whose description begins with DEPRECATED are kept so that existing configurations keep working. Don't use them in new deployments.


## Query tuning and the optimizer

| Parameter | Description | Type | Default | Unit | Context |
| :--- | :--- | :--- | :--- | :--- | :--- |
| [yb_bnl_batch_size](../yb-tserver/#yb-bnl-batch-size) | Batch size of nested loop joins. Set to 1 to always use simple nested loop joins. | integer | `1024` | n/a | user |
| yb_bnl_enable_hashing | Enables batched nested loop joins to use hashing to process its matches. | bool | `on` | n/a | user |
| yb_bnl_optimize_first_batch | Enables batched nested loop joins to predict the size of its first batch and optimize if it's smaller than yb_bnl_batch_size. | bool | `on` | n/a | user |
| yb_bypass_cond_recheck | DEPRECATED: no-op. | bool | `on` | n/a | user |
| yb_disable_parallel_query_in_ddl | Disables parallel query for the SELECT planned by DDLs such as CREATE TABLE AS, SELECT INTO, CREATE/REFRESH MATERIALIZED VIEW, COPY (query) TO, and EXPLAIN [ANALYZE] CREATE TABLE AS. Enabled by default because parallel query in these DDLs has not been QA tested in YugabyteDB. Set to off as an escape hatch to restore upstream PostgreSQL behavior for workloads that rely on it. | bool | `on` | n/a | user |
| [yb_enable_advanced_index_cond_fold](../yb-tserver/#yb-enable-advanced-index-cond-fold) | Enable advanced folding of same-column index conditions, including tightening inequality bounds across scan keys, intersecting IN arrays, and detecting additional contradictions at bind time. | bool | `on` | n/a | user |
| [yb_enable_base_scans_cost_model](../yb-tserver/#yb-enable-base-scans-cost-model) | Enables YB cost model for Sequential and Index scans. DEPRECATED: This setting is deprecated and will be removed in a future release. Use "yb_enable_cbo" instead. | bool | `off` | n/a | user |
| [yb_enable_batchednl](../yb-tserver/#yb-enable-batchednl) | Enables the planner's use of batched nested-loop join plans. | bool | `on` | n/a | user |
| [yb_enable_bitmapscan](../yb-tserver/#yb-enable-bitmapscan) | Enables the planner's use of YB bitmap-scan plans. To use YB Bitmap Scans, both yb_enable_bitmapscan and enable_bitmapscan must be true. | bool | `off` | n/a | user |
| [yb_enable_cbo](../yb-tserver/#yb-enable-cbo) | Enable YB cost model. Values: `off`, `on`, `legacy_mode`, `legacy_stats_mode`, `legacy_bnl_mode`, `legacy_stats_bnl_mode`, `legacy_ignore_stats_bnl_mode`. | enum | `legacy_mode` | n/a | user |
| [yb_enable_derived_equalities](../yb-tserver/#yb-enable-derived-equalities) | Enable derivation of additional equalities for generated columns and expression indexes. | bool | `off` | n/a | user |
| [yb_enable_derived_saops](../yb-tserver/#yb-enable-derived-saops) | If true, derives additional scalar array operation conditions from table constraints and adds them to queries to improve performance. Has no impact in case yb_max_merge_scan_streams is 0. | bool | `off` | n/a | user |
| yb_enable_distinct_pushdown | Push supported DISTINCT operations to DocDB. | bool | `on` | n/a | user |
| yb_enable_expression_pushdown | Push supported expressions down to DocDB for evaluation. | bool | `on` | n/a | user |
| yb_enable_geolocation_costing | Allow the optimizer to cost and choose between duplicate indexes based on locality. | bool | `on` | n/a | user |
| yb_enable_hash_batch_in | GUC variable that enables batching RPCs of generated for IN queries on hash keys issued to the same tablets. | bool | `on` | n/a | user |
| yb_enable_index_aggregate_pushdown | Push supported index aggregate operations to DocDB. This affects IndexScan, not IndexOnlyScan. | bool | `on` | n/a | user |
| yb_enable_index_backfill_scan_optimization | Enables index backfill scan optimizations. If true, index build/backfill reads only the columns needed for the index and pushes partial index predicates down to the base table scan. | bool | `off` | n/a | user |
| yb_enable_inplace_index_update | Enables the in-place update of non-key columns of secondary indexes when key columns of the index are not updated. This is useful when updating the included columns in a covering index among others. | bool | `on` | n/a | user |
| [yb_enable_optimizer_statistics](../yb-tserver/#yb-enable-optimizer-statistics) | Enables use of the PostgreSQL selectivity estimation which utilizes table statistics collected with ANALYZE. When disabled, a simpler heuristics based selectivity estimation is used. DEPRECATED: This settting is deprecated and will be removed in a future release. Use "yb_enable_cbo" instead. | bool | `off` | n/a | user |
| yb_enable_parallel_scan_colocated | When set, allows parallel scan of the colocated relations. | bool | `on` | n/a | user |
| yb_enable_parallel_scan_hash_sharded | When set, allows parallel scan of the hash sharded relations. | bool | `off` | n/a | user |
| yb_enable_parallel_scan_range_sharded | When set, allows parallel scan of the range sharded relations. | bool | `off` | n/a | user |
| yb_enable_parallel_scan_system | When set, allows parallel scan of the system relations. | bool | `off` | n/a | user |
| yb_enable_planner_trace | Enables planner tracing. | bool | `off` | n/a | user |
| yb_enable_primary_key_decode_from_index | Allow Index Only Scans to decode base table primary key columns from secondary index entries. When enabled, PK columns are decoded from ybidxbasectid in secondary index entries. | bool | `off` | n/a | user |
| yb_enable_saop_pushdown | Push supported scalar array operations down to DocDB for evaluation. | bool | `on` | n/a | user |
| yb_enable_sequence_pushdown | Allow nextval() to fetch the value range and advance the sequence value in a single operation. | bool | `on` | n/a | user |
| yb_enable_update_reltuples_after_create_index | Enables update of reltuples in pg_class for the base table and index after creating the index. When disabled, reltuples are not updated during concurrent index creation and only index reltuples are updated during non-concurrent index creation. | bool | `off` | n/a | user |
| [yb_explicit_row_lock_skip_locked_max_read_ahead](../yb-tserver/#yb-explicit-row-lock-skip-locked-max-read-ahead) | Max number of rows that are read ahead for SKIP LOCKED explicit row locking. Set to 1 to preserve original behavior, read ahead is not performed by default. | integer | `1` | n/a | user |
| yb_explicit_row_locking_batch_size | Batch size of explicit row locking. Set to 1 to conserve default behavior, batching is disabled by default. | integer | `1024` | n/a | user |
| [yb_fetch_row_limit](../yb-tserver/#yb-fetch-row-limit) | Maximum number of rows to fetch per scan. 0 = No limit. | integer | `1024` | n/a | user |
| [yb_fetch_size_limit](../yb-tserver/#yb-fetch-size-limit) | Maximum size of a fetch response. 0 = No limit. | integer | `0` | `B` | user |
| yb_hinted_uids | Node UIDS to prefer in cost comparisons. | string | empty | n/a | user |
| yb_index_state_flags_update_delay | Delay in milliseconds between stages of online index build. Set high to give online transactions more time to complete. | integer | `0` | `ms` | user |
| yb_lock_pk_single_rpc | Use single RPC to select and lock when PK is specified. If possible (no conflicting filters in the plan), use a single RPC to select and lock, when a locking clause is provided, in isolation levels REPEATABLE READ and READ COMMITTED. | bool | `off` | n/a | user |
| [yb_max_merge_scan_streams](../yb-tserver/#yb-max-merge-scan-streams) | Sets the maximum number of streams tolerated for merge scan. For YB LSM index scans, when multiple merge-scan-eligible scalar array operations are involved, they are combined until their cartesian product's cardinality reaches this limit. Merge scan is per index scan, and the limit applies per index scan, not globally. Set to 0 to disable. | integer | `0` | n/a | user |
| yb_network_fetch_cost | Sets the planner's estimate of the fixed cost of fetching a batch of rows from a YB relation. | real | `4` | n/a | user |
| yb_parallel_range_rows | The number of rows to plan per parallel worker. | integer | `0` | n/a | user |
| yb_parallel_range_size | Approximate size of parallel range for DocDB relation scans. | integer | `16777216` | `B` | user |
| yb_pg_stat_plans_plan_format | Plan format for QPM. Values: `text`, `xml`, `json`, `yaml`. | enum | `json` | n/a | postmaster |
| yb_pg_stat_plans_track | Selects which statements are tracked by QPM. Values: `none`, `top`, `all`. | enum | `all` | n/a | superuser |
| yb_plpgsql_disable_prefetch_in_for_query | Disable prefetching in a PLPGSQL FOR loop over a query. | bool | `off` | n/a | user |
| yb_prefer_bnl | If enabled, planner will force a preference of batched nested loop join plans over classic nested loop join plans. | bool | `on` | n/a | user |
| yb_prefetch_column_statistics | Prefetch a relation's column statistics in one catalog read during planning. | bool | `on` | n/a | user |
| [yb_sampling_algorithm](../yb-tserver/#yb-sampling-algorithm) | Which sampling algorithm to use for YSQL. full_table_scan - scan the whole table and pick random rows, block_based_sampling - sample the table for a set of blocks, then scan selected blocks to form a final rows sample. Values: `full_table_scan`, `block_based_sampling`. | enum | `block_based_sampling` | n/a | user |
| [yb_skip_redundant_update_ops](../yb-tserver/#yb-skip-redundant-update-ops) | Enables the comparison of old and new values of columns specified in the SET clause of YSQL UPDATE queries to skip redundant secondary index updates and redundant constraint checks. | bool | `on` | n/a | sighup |
| yb_update_optimization_infra | Enables optimizations of YSQL UPDATE queries. This includes (but not limited to) skipping redundant secondary index updates and redundant constraint checks. | bool | `on` | n/a | sighup |
| yb_use_cluster_config_for_geolocation_costing | When no tablespace is assigned to table, use cluster replication info to estimate network costs. | bool | `off` | n/a | user |
| [yb_use_hash_splitting_by_default](../yb-tserver/#yb-use-hash-splitting-by-default) | Enables hash splitting as the default method for primary key and index sorting in LSM indexes. When set to true, the default sorting for the first primary/index key column in LSM indexes is HASH, Setting this to false changes the default to ASC, making it compatible with standard PostgreSQL behavior. This setting is useful for optimizing query performance, especially for migrations from PostgreSQL or scenarios where index-based sorting and sharding behavior are critical. | bool | `on` | n/a | user |
| yb_wait_for_backends_catalog_version_timeout | Timeout in milliseconds to wait for backends to reach desired catalog versions. The actual time spent may be longer than that by as much as master flag wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms. Setting to zero or less results in no timeout. Currently used by concurrent CREATE INDEX. | integer | `900000` | `ms` | user |


## Transactions and statement behavior

| Parameter | Description | Type | Default | Unit | Context |
| :--- | :--- | :--- | :--- | :--- | :--- |
| [yb_default_copy_from_rows_per_transaction](../yb-tserver/#yb-default-copy-from-rows-per-transaction) | Sets the batch number of rows to copy from the source to table. | integer | `20000` | n/a | user |
| yb_disable_transactional_writes | Sets the boolean flag to disable transaction writes. | bool | `off` | n/a | user |
| yb_enable_docdb_tracing | Enables tracing for the commands in this session. | bool | `off` | n/a | user |
| yb_enable_retry_after_non_atomic_commit | Allow query layer retries of CALL/DO statements after an in-procedure COMMIT. When enabled, the query layer will retry CALL and DO statements on conflict or read-restart errors even if the procedure or DO block has already performed a COMMIT. This can lead to re-execution of already-committed work (e.g., duplicate inserts) and is provided only as a compatibility option to revert to the old behavior. The default (off) is the safe behavior. | bool | `off` | n/a | user |
| yb_enable_upsert_mode | Sets the boolean flag to enable or disable upsert mode for writes. When the target table has secondary indexes, triggers, or foreign key constraints, upsert mode is automatically disabled to prevent correctness issues. Consider using INSERT ... ON CONFLICT for true upsert semantics instead. | bool | `off` | n/a | user |
| yb_extra_commands_to_retry | Comma-separated list of command tags to additionally retry on a serialization error. By default the query layer retries SELECT/INSERT/UPDATE/DELETE, and under READ COMMITTED also any command tag on kConflict/kDeadlock/kAborted (historical) and CALL/DO whose body ran only those same four statements (or nested CALL/DO with only those same four statements) on kReadRestart. Each tag listed here joins the retriable set; tag names are case-insensitive and follow the names shown in psql command tags. COPY, COPY FROM, and ANALYZE are rejected at SET time -- they are not safe to retry. Use with caution: re-executing DDL or other utility statements may have unintended effects. | string | empty | n/a | user |
| yb_extra_commands_to_retry_in_proc | Comma-separated list of command tags that, when run inside a CALL/DO body, do not block retry of the enclosing CALL/DO. By default a CALL/DO is retried only when its body ran nothing but SELECT/INSERT/UPDATE/DELETE or nested CALL/DO, since a retry re-runs the entire body. Each tag listed here joins that set, e.g. 'LOCK TABLE'. Tag names are case-insensitive. COPY, COPY FROM, and ANALYZE are rejected at SET time -- they are not safe to retry. Use with caution: re-executing the listed statements may have unintended effects. | string | empty | n/a | user |
| yb_fast_path_for_colocated_copy | Enable fast-path transaction for copy on colocated tables. For testing now. | bool | `off` | n/a | user |
| yb_fk_references_cache_limit | Sets the maximum size for the FK reference cache filled by the INSERT, SELECT ... FOR KEY SHARE or similar statements. | integer | `65535` | n/a | user |
| [yb_follower_read_staleness_ms](../yb-tserver/#yb-follower-read-staleness-ms) | Sets the staleness (in ms) to be used for performing follower reads. | integer | `30000` | n/a | user |
| yb_follower_reads_behavior_before_fixing_20482 | Controls whether ysql follower reads that is enabled inside a transaction block should take effect in the same transaction or not. Prior to fixing #20482 the behavior was that the change does not affect the current transaction but only affects subsequent transactions. The flag is intended to be used if there is a customer who relies on the old behavior. | bool | `off` | n/a | user |
| yb_ignore_bool_cond_for_legacy_estimate | Ignore boolean condition for row count estimate in legacy cost model. Negates the side effect on legacy mode row count estimate introduced by the fix "[#26266] YSQL: Add BOOL_LSM_FAM_OID to boolean family" for backward compatibility. | bool | `off` | n/a | user |
| yb_ignore_freeze_with_copy | Ignore the FREEZE flag on COPY FROM command. | bool | `on` | n/a | user |
| [yb_insert_on_conflict_read_batch_size](../yb-tserver/#yb-insert-on-conflict-read-batch-size) | Maximum batch size for arbiter index reads during INSERT ON CONFLICT. A value of 0 disables this feature. | integer | `1024` | n/a | superuser |
| yb_max_query_layer_retries | Max number of internal query layer retries of a statement. Max number of query layer retries of a statement for the following errors: serialization error (40001), "Restart read required" (40001), deadlock detected (40P01). In Repeatable Read and Serializable isolation levels, the query layer only retries errors faced in the first statement of a transaction. In READ COMMITTED isolation, the query layer has the ability to do retries for any statement in a transaction. Retries are not possible if some response data has already been sent to the client while the query is still executing. This happens if the output buffer, the size of which is configurable using the TServer gflag ysql_output_buffer_size, has filled at least once and is flushed. | integer | `60` | n/a | user |
| yb_pg_batch_detection_mechanism | The drivers use message protocol to communicate with PG. The driver does not inform PG in advance about a Batch execution. We need to identify a batch because in that case the single-shard optimization should be disabled. Postgres drivers pipeline messages and we exploit this to peek the message following 'Execute' to detect a batch. This may lead to some unforeseen bugs, so this GUC provides a way to disable the single-shard optimization completely or go back to the behavior before #16446 was fixed. Values: `detect_by_peeking`, `assume_all_batch_executions`, `ignore_batch_delete_and_update_may_fail`. | enum | `detect_by_peeking` | n/a | user |
| yb_planner_custom_plan_for_partition_pruning | If enabled, choose custom plan over generic plan for prepared statements based on the number of partition pruned. | bool | `on` | n/a | user |
| [yb_read_from_followers](../yb-tserver/#yb-read-from-followers) | Allow any statement that generates a read request to go to any node. | bool | `off` | n/a | user |
| [yb_read_time](../yb-tserver/#yb-read-time) | Allows querying the database as of a point in time in the past. Takes a unix timestamp in microseconds. Zero means reading data as of current time. User should set this variable with caution. Currently, it can only read old data without schema changes. In other words, it should not be set to a timestamp before a DDL operation has been performed. Write-DML or DDL queries are not allowed while this variable is set. | string | `0` | n/a | superuser |
| yb_transaction_priority_lower_bound | Sets lower bound for priority used by transactions of this session. | real | `0` | n/a | user |
| yb_transaction_priority_upper_bound | Sets upper bound for priority used by transactions of this session. | real | `1` | n/a | user |
| yb_xcluster_consistency_level | Controls the consistency level of xCluster replicated databases. Valid values are "database" and "tablet". | string | `database` | n/a | user |


## Locking

| Parameter | Description | Type | Default | Unit | Context |
| :--- | :--- | :--- | :--- | :--- | :--- |
| yb_enable_advisory_locks | DEPRECATED - Enable advisory lock feature. | bool | `on` | n/a | sighup |
| yb_enable_ddl_savepoint_infra | Allow enabling ddl savepoint support. | bool | `on` | n/a | sighup |
| yb_enable_pg_locks | Enable the pg_locks view. This view provides information about the locks held by active postgres sessions. | bool | `on` | n/a | superuser |
| [yb_locks_max_transactions](../yb-tserver/#yb-locks-max-transactions) | Sets the maximum number of transactions for which to return rows in pg_locks. | integer | `16` | n/a | user |
| [yb_locks_min_txn_age](../yb-tserver/#yb-locks-min-txn-age) | Sets the minimum transaction age for results from pg_locks. | integer | `1000` | `ms` | user |
| [yb_locks_txn_locks_per_tablet](../yb-tserver/#yb-locks-txn-locks-per-tablet) | Sets the maximum number of rows per transaction per tablet to return in pg_locks. | integer | `200` | n/a | user |
| yb_pg_locks_integrate_advisory_locks | Enables pg_locks to integrate and display advisory locks details correctly. | bool | `on` | n/a | sighup |
| yb_silence_advisory_locks_not_supported_error | Deprecated. This is no-op. | bool | `off` | n/a | user |


## Observability and statistics

| Parameter | Description | Type | Default | Unit | Context |
| :--- | :--- | :--- | :--- | :--- | :--- |
| yb_ash_circular_buffer_size | Size (in KiBs) of ASH circular buffer that stores the samples. If this is 0, the size will be calculated based on the number of cores. | integer | `0` | `kB` | postmaster |
| yb_ash_sample_size | Number of samples captured from each component per sampling event. | integer | `500` | n/a | sighup |
| yb_ash_sampling_interval_ms | Time (in milliseconds) between two consecutive sampling events. | integer | `1000` | `ms` | sighup |
| yb_enable_ash | Enable Active Session History for sampling and instrumenting YSQL and YCQL queries, and various background activities. | bool | `on` | n/a | postmaster |
| yb_enable_pg_stat_statements_docdb_metrics | If true, enable DocDB metrics collection for pg_stat_statements. This enables collection of the following metrics: docdb_seeks, docdb_nexts, docdb_prevs, docdb_read_time, docdb_write_time and docdb_obsolete_rows_scanned. | bool | `on` | n/a | superuser |
| yb_enable_pg_stat_statements_rpc_stats | If true, enable RPC execution time stats for pg_stat_statements. | bool | `on` | n/a | superuser |
| yb_enable_query_diagnostics | Enables the collection of query diagnostics data for YSQL queries, facilitating the creation of diagnostic bundles. | bool | `off` | n/a | postmaster |
| yb_log_min_backtraces | Sets the minimum message level for including a backtrace in the log. Errors at or above this level will have a call stack attached. Each level includes all the levels that follow it. Values: `debug5`, `debug4`, `debug3`, `debug2`, `debug1`, `info`, `notice`, `warning`, `error`, `log`, `fatal`, `panic`. | enum | `fatal` | n/a | superuser |
| yb_pg_stat_plans_cache_replacement_algorithm | Specifies cache replacement policy for Query Plan Management. Values: `simple_clock_lru`, `true_lru`. | enum | `simple_clock_lru` | n/a | postmaster |
| yb_pg_stat_plans_max_cache_size | Max number of query/plan pairs stored by QPM. | integer | `5000` | n/a | postmaster |
| yb_pg_stat_plans_show_max_exec_params | Show QPM maximum execution time parameter values. | bool | `off` | n/a | superuser |
| yb_pg_stat_plans_track_catalog_queries | When set, QPM tracks plans for queries referencing catalog tables. | bool | `on` | n/a | superuser |
| yb_pg_stat_plans_verbose_plans | Generate verbose plans in QPM. | bool | `off` | n/a | superuser |
| yb_qpm_compress_text | Compress QPM plan and hint text if necessary. | bool | `on` | n/a | superuser |
| yb_query_diagnostics_bg_worker_interval_ms | Time (in milliseconds) for which the query diagnostic's background worker sleeps. | integer | `1000` | `ms` | postmaster |
| yb_query_diagnostics_circular_buffer_size | Size of query diagnostics circular buffer that stores statuses of bundles. The circular buffer is filled sequentially until it reaches this size, then it wraps around and starts overwriting the oldest entries. | integer | `64` | `kB` | postmaster |
| yb_query_diagnostics_disable_database_connection_bgworker | This disables creating extra bgworker which creates database connection for query diagnostics. If this is set to true, ASH and schema details are not dumped. | bool | `off` | n/a | sighup |
| yb_tcmalloc_sample_period | TCMalloc sample interval in bytes, i.e. approximately how many bytes between sampling allocation call stacks. | integer | `1048576` | `B` | superuser |


## Replication and change data capture

| Parameter | Description | Type | Default | Unit | Context |
| :--- | :--- | :--- | :--- | :--- | :--- |
| yb_default_replica_identity | Default replica identity at the time of table creation. | string | `CHANGE` | n/a | superuser |
| yb_enable_replica_identity | Allow changing replica identity via ALTER TABLE command. | bool | `on` | n/a | superuser |


## Maintenance and resource usage

| Parameter | Description | Type | Default | Unit | Context |
| :--- | :--- | :--- | :--- | :--- | :--- |
| yb_neg_catcache_ids | Comma separated list of additional sys cache ids that are allowed to be negatively cached. | string | empty | n/a | superuser |


## YSQL major version upgrade

| Parameter | Description | Type | Default | Unit | Context |
| :--- | :--- | :--- | :--- | :--- | :--- |
| yb_extension_upgrade | Set to true when upgrading extensions during a YSQL major version upgrade. | bool | `off` | n/a | superuser |
| yb_major_version_upgrade_compatibility | The compatibility level to use during a YSQL Major version upgrade. Allowed values are 0 and 11. | integer | `0` | n/a | sighup |
| yb_mixed_mode_expression_pushdown | Enables expression pushdown for queries in mixed mode of a YSQL Major version upgrade. | bool | `on` | n/a | user |
| yb_mixed_mode_saop_pushdown | Enable pushdown of scalar array operation expressions in mixed mode of a YSQL Major version upgrade. For example, IN, ANY, ALL. | bool | `off` | n/a | user |


## Extension parameters

| Parameter | Description | Type | Default | Unit | Context |
| :--- | :--- | :--- | :--- | :--- | :--- |
| yb_pg_metrics.log_accesses | Log each request received by the YSQL webserver. | bool | `off` | n/a | superuser |
| yb_pg_metrics.log_tcmalloc_stats | Log TCMalloc memory statistics with each request received by the YSQL webserver. | bool | `off` | n/a | superuser |
| yb_pg_metrics.webserver_profiler_sample_period_bytes | The interval at which Google TCMalloc should sample allocations in the YSQL webserver. If this is 0, sampling is disabled. | integer | `1048576` | n/a | superuser |
| yb_xcluster_ddl_replication.enable_manual_ddl_replication | Temporarily disable automatic xCluster DDL replication - DDLs will have to be manually executed on the target. DDL strings will still be captured and replicated, but will be marked with a 'manual_replication' flag. | bool | `off` | n/a | user |


## Other parameters

| Parameter | Description | Type | Default | Unit | Context |
| :--- | :--- | :--- | :--- | :--- | :--- |
| yb_allow_dockey_bounds | If true, allow lower_bound/upper_bound fields of PgsqlReadRequestPB to be DocKeys. Only applicable for hash-sharded tables. | bool | `on` | n/a | superuser |
| yb_conn_mgr_selective_deallocate | Enables connection-manager-aware DEALLOCATE behavior. | bool | `on` | n/a | sighup |
| yb_disable_auto_analyze | Run 'ALTER DATABASE <name> SET yb_disable_auto_analyze=on' to disable auto analyze on that database. Set it to off to resume auto analyze. Setting this GUC via any other method will throw a WARNING message. | bool | `off` | n/a | user |
| yb_disable_catalog_version_check | Disable checking that read requests from this pg backend have the latest catalog version. User should set this variable with caution. It is under active development and is not recommended for production clusters. Currently, it is used by ysql_dump to read pg catalog as of time. | bool | `off` | n/a | superuser |
| yb_disable_pg_snapshot_mgmt_in_repeatable_read | [Deprecated - This GUC is valid only in older releases. It is present here just to avoid a failure in case you forgot to remove it from your configuration.]. | bool | `off` | n/a | user |
| yb_enable_add_column_missing_default | Enable using the default value for existing rows after an ADD COLUMN ... DEFAULT operation. | bool | `on` | n/a | user |
| yb_enable_alter_table_rewrite | Enable ALTER TABLE rewrite operations. | bool | `on` | n/a | user |
| yb_enable_create_with_table_oid | Enables the ability to set table oids when creating tables or indexes. | bool | `off` | n/a | user |
| yb_enable_extended_sql_codes | Allow to return to the client SQL status codes defined by YugabyteDB (YBxxx). Those codes are used internally to determine if transparent retry is possible. If disabled, they are replaced with similar Postgres defined codes. | bool | `off` | n/a | user |
| yb_enable_global_views | Enables querying of global views. | bool | `off` | n/a | superuser |
| yb_enable_nop_alter_role_optimization | Enable nop alter role statement optimization to avoid catalog version increment if the alter role statement does not involve any change. | bool | `on` | n/a | user |
| yb_explain_hide_non_deterministic_fields | If set, all fields that vary from run to run are hidden from the output of EXPLAIN. | bool | `off` | n/a | user |
| yb_force_tablespace_locality | Forces use of tablespace-based locality over region locality. | bool | `off` | n/a | user |
| yb_force_tablespace_locality_oid | Tablespace used for tablespace-based locality. Picked automatically if InvalidOid (default). | oid | `0` | n/a | user |
| yb_format_funcs_include_yb_metadata | Include DocDB metadata (such as tablet splits) in formatting functions exporting system catalog information. | bool | `off` | n/a | user |
| yb_ignore_read_time_in_walsender | When set, walsender will fetch the publication as of current time if it encounters any failures while reading the catalog tables as of yb_read_time. This GUC should be set carefully and only till the time the process of upgrading logical replication streams is complete (i.e till the yb_restart_time of all the streams crosses the time of upgrade completion). Moreover this GUC should be set only after ensuring that no more DDLs (including ALTER PUBLICATION) will be encountered by the walsender. | bool | `off` | n/a | user |
| yb_is_client_ysqlconnmgr | Identifies that connection is created by Ysql Connection Manager. | bool | `off` | n/a | backend |
| yb_make_next_ddl_statement_nonbreaking | When set, the next ddl statement will not cause running transactions to abort. This only affects the next ddl statement and resets automatically. | bool | `off` | n/a | superuser |
| yb_make_next_ddl_statement_nonincrementing | DEPRECATED - When set, the next ddl statement will not cause catalog version to increment. This only affects the next ddl statement and resets automatically. | bool | `off` | n/a | superuser |
| yb_non_ddl_txn_for_sys_tables_allowed | Enables the use of regular transactions for operating on system catalog tables in case a DDL transaction has not been started. | bool | `off` | n/a | user |
| yb_pushdown_is_not_null | DEPRECATED: no-op. | bool | `on` | n/a | user |
| yb_pushdown_strict_inequality | DEPRECATED: no-op. | bool | `on` | n/a | user |
| yb_read_after_commit_visibility | Control read-after-commit-visibility guarantee. This GUC is intended as a crutch for users migrating from PostgreSQL and new to read restart errors. Users can now largely avoid these errors when read-after-commit-visibility guarantee is not a strong requirement. This option cannot be set from within a transaction block. Configure one of the following options: (a) strict: Default Behavior. The read-after-commit-visibility guarantee is maintained by the database. However, users may see read restart errors that show "ERROR: Query error: Restart read required at: ...". The database attempts to retry on such errors internally but that is not always possible. (b) relaxed: With this option, the read-after-commit-visibility guarantee is relaxed. Do not see read restart errors but may miss recent updates with staleness bounded by clock skew. This mode does not apply to serializable isolation level and fast path writes. (c) deferred: Defers read point. Higher latency but read-after-commit-visibility guarantee is maintained. Values: `strict`, `relaxed`, `deferred`. | enum | `strict` | n/a | user |
| yb_refresh_matview_in_place | Refresh materialized views in place. | bool | `off` | n/a | user |
| yb_speculatively_execute_pl_statements | If enabled, procedural language statements may be speculatively executed when it is safe to do so without waiting for the successful completion of previous statements. This allows any writes produced by triggers to be batched alongside their parent data-modifying writes such that the number of storages flushes may be minimized. | bool | `off` | n/a | superuser |
| yb_toast_catcache_threshold | Size threshold in bytes for a catcache tuple to be compressed. | integer | `2048` | n/a | user |
| yb_update_max_cols_size_to_compare | Maximum size in bytes of columns whose data is to be compared while seeking to optimize updates. If set to 0, no size limit is applied. | integer | `10240` | `B` | user |
| yb_update_num_cols_to_compare | Maximum number of columns whose data is to be compared while seeking to optimize updates. If set to 0, all applicable columns in the table will be compared. | integer | `50` | n/a | user |
| yb_use_tserver_key_auth | If set, the client connection will be authenticated via 'yb-tserver-key' auth. | bool | `off` | n/a | backend |
| yb_whitelist_extra_statements_for_pl_speculative_execution | If enabled, additional procedural language constructs are whitelisted for use in speculative execution. | bool | `off` | n/a | superuser |


## Internal parameters

{{< warning title="Not for production use" >}}
YugabyteDB sets these parameters itself, or reserves them for internal and upgrade workflows. Their descriptions state that they are not intended to be set by users. Set them only when Yugabyte Support asks you to.
{{< /warning >}}

| Parameter | Description | Type | Default | Unit | Context |
| :--- | :--- | :--- | :--- | :--- | :--- |
| yb_allow_block_based_sampling_algorithm | Autoflag to allow YsqlSamplingAlgorithm::BLOCK_BASED_SAMPLING. Not to be touched by users. | bool | `on` | n/a | superuser |
| yb_allow_separate_requests_for_sampling_stages | Autoflag to allow using separate requests for block-based sampling stages. Not to be touched by users. | bool | `on` | n/a | superuser |
| yb_effective_transaction_isolation_level | [DEPRECATED - instead use the yb_get_effective_transaction_isolation_level() function]. Shows the effective YugabyteDB transaction isolation level used by the current active transaction in the session. | string | `default` | n/a | internal |
| yb_enable_docdb_vector_type | Autoflag to enable using the DocDB Vector type. Not to be touched by users. | bool | `on` | n/a | superuser |
| yb_transaction_priority | [DEPRECATED - instead use the yb_get_current_transaction_priority() function]. Gets the transaction priority used by the current active distributed transaction in the session. If no distributed transaction is active, return 0. | real | `0` | n/a | internal |
| yb_upgrade_to_pg15_completed | Indicates the state of YSQL major upgrade to PostgreSQL version 15. Do not modify this manually. | bool | `on` | n/a | sighup |
| yb_use_internal_auto_analyze_service_conn | [Internal Only GUC] - Help a backend identify that this is a connection from the internal Auto-Analyze service. | bool | `off` | n/a | user |
| yb_xcluster_ddl_replication.ddl_queue_primary_key_ddl_end_time | Internal use only: Used by HandleTargetDDLEnd function. | string | empty | n/a | superuser |
| yb_xcluster_ddl_replication.ddl_queue_primary_key_query_id | Internal use only: Used by HandleTargetDDLEnd function. | string | empty | n/a | superuser |


## Developer and test parameters

{{< warning title="Not for production use" >}}
These parameters change internal behavior, exist to support testing and debugging, and can change or be removed in any release. Set them only when Yugabyte Support asks you to.
{{< /warning >}}

| Parameter | Description | Type | Default | Unit | Context |
| :--- | :--- | :--- | :--- | :--- | :--- |
| yb_allow_replication_slot_lsn_types | Allow specifying LSN type while creating replication slot. | bool | `on` | n/a | superuser |
| yb_allow_replication_slot_ordering_modes | Allow specifying ordering mode while creating replication slot. | bool | `off` | n/a | superuser |
| yb_always_increment_catalog_version_on_ddl | When set, all DDL statements will cause the catalog version to increment. Unlike yb_test_make_all_ddl_statements_incrementing, this only controls the version incrementing behavior. | bool | `on` | n/a | sighup |
| yb_binary_restore | Enter a special mode designed specifically for YSQL binary restore. | bool | `off` | n/a | superuser |
| yb_catcache_list_from_preloaded_limit | Max tuples in a preloaded catalog cache for local list building. 0 disables. | integer | `100000` | n/a | user |
| yb_cdcsdk_stream_tables_without_primary_key | Enable streaming of tables without primary key in CDC logical replication streams. | bool | `on` | n/a | superuser |
| yb_ddl_rollback_enabled | If set, any DDL that involves DocDB schema changes will have those changes rolled back upon failure. | bool | `on` | n/a | superuser |
| yb_ddl_transaction_block_enabled | If true, DDL operations in YSQL will execute within the active transaction block instead of their separate transactions. | bool | `off` | n/a | postmaster |
| yb_debug_log_catcache_events | Log details for every catalog cache event such as a cache miss or cache invalidation/refresh. | bool | `off` | n/a | user |
| yb_debug_log_docdb_error_backtrace | Append stacktrace information to errors received from DocDB. | bool | `off` | n/a | user |
| yb_debug_log_docdb_requests | Log the contents of all internal (protobuf) requests to DocDB. | bool | `off` | n/a | user |
| yb_debug_log_internal_restarts | Log details for internal restarts such as read-restarts, cache-invalidation restarts, or txn restarts. | bool | `off` | n/a | user |
| yb_debug_log_snapshot_mgmt | Log details about snapshot management such as pushing/popping a snapshot and picking a new snapshot. | bool | `off` | n/a | user |
| yb_debug_log_snapshot_mgmt_stack_trace | Log stack traces as well for lines logged by yb_debug_log_snapshot_mgmt. | bool | `off` | n/a | user |
| yb_debug_original_backtrace_format | Use original Postgres functions to create and format the stacktrace. | bool | `off` | n/a | user |
| yb_disable_ddl_transaction_block_for_read_committed | If true, DDL operations in READ COMMITTED mode will be executed in a separate DDL transaction instead of the as part of the enclosing transaction block even if ysql_yb_ddl_transaction_block_enabled is true. In other words, for Read Committed, fall back to the mode when ysql_yb_ddl_transaction_block_enabled is false. | bool | `off` | n/a | postmaster |
| yb_disable_wait_for_backends_catalog_version | Disable waiting for backends to have up-to-date pg_catalog. This could cause correctness issues. | bool | `off` | n/a | superuser |
| yb_dist_tracecontext | Sets the W3C trace context (traceparent) for distributed tracing. | string | n/a | n/a | user |
| yb_enable_consistent_replication_from_hash_range | Enable replication slot consumption of consistent changes from a hash range of table. | bool | `off` | n/a | superuser |
| yb_enable_ddl_atomicity_infra | Used along side with yb_ddl_rollback_enabled to control whether DDL atomicity is enabled. | bool | `on` | n/a | superuser |
| yb_enable_fkey_batched_docdb_lookup_when_types_mismatch | Enable batched DocDB lookup for foreign key constraint check when types mismatch. | bool | `off` | n/a | backend |
| yb_enable_fkey_catcache | Enable preloading of foreign key information into the relation cache. | bool | `on` | n/a | user |
| yb_enable_invalidate_table_cache_entry | Enable invalidation of individual table cache entry on catalog cache refresh. | bool | `on` | n/a | superuser |
| yb_enable_invalidation_messages | Enable invalidation messages. | bool | `on` | n/a | superuser |
| yb_enable_listen_notify | Enables LISTEN/NOTIFY. | bool | `off` | n/a | sighup |
| yb_enable_memory_tracking | Enables tracking of memory consumption of the PostgreSQL process. This enhances garbage collection behaviour and memory usage observability. | bool | `on` | n/a | user |
| yb_enable_negative_catcache_entries | When set, negative catcache entries are enabled. | bool | `on` | n/a | sighup |
| yb_enable_pg_export_snapshot | Enable pg_export_snapshot and SET TRANSACTION SNAPSHOT for synchronizing snapshots across transactions. | bool | `on` | n/a | sighup |
| yb_enable_replication_commands | Enable the replication commands for Publication and Replication Slots. | bool | `on` | n/a | superuser |
| yb_enable_replication_origin_shared | Enable shared replication origin write tagging. | bool | `on` | n/a | postmaster |
| yb_enable_replication_slot_consumption | Enable consumption of changes via replication slots. This feature is currently in active development and should not be enabled. | bool | `on` | n/a | user |
| yb_enable_spi_dist_tracing | Enables distributed tracing for SPI (Server Programming Interface) calls. | bool | `on` | n/a | user |
| yb_force_catalog_update_on_next_ddl | Make the next DDL update the catalog in force mode which allows it to operate even during ysql major catalog upgrades. WARNING: This is a dangerous option and should be used only for DDLs on temp tables, and other transient objects. | bool | `off` | n/a | user |
| yb_ignore_pg_class_oids | Ignores requests to set pg_class OIDs in yb_binary_restore mode. | bool | `on` | n/a | superuser |
| yb_ignore_relfilenode_ids | Ignores requests to set relfilenode IDs in yb_binary_restore mode. | bool | `on` | n/a | superuser |
| yb_invalidation_message_expiration_secs | Invalidation messages expiration time in catalog table pg_yb_invalidation_messages. The effective expiration is automatically raised to at least 10 * --heartbeat_interval_ms so that messages survive long enough for every TServer to receive them via heartbeats. | integer | `10` | n/a | superuser |
| yb_log_heap_snapshot_on_exit_threshold | When a process exits, log a peak heap snapshot showing the approximate memory usage of each malloc call stack if its peak RSS is greater than or equal to this threshold in KB. Set to -1 to disable. | integer | `-1` | `kB` | user |
| yb_max_num_invalidation_messages | Max number of invalidation messages supported for incremental catalog cache refresh. | integer | `8192` | n/a | superuser |
| yb_notifications_poll_sleep_duration_empty_ms | Time in milliseconds for which the notifications poller process waits before polling again in case the last poll returned no notifications. | integer | `100` | `ms` | sighup |
| yb_notifications_poll_sleep_duration_nonempty_ms | Time in milliseconds for which the notifications poller process waits before polling again in case the last poll returned notifications. | integer | `1` | `ms` | sighup |
| yb_reorderbuffer_max_changes_in_memory | Maximum number of changes kept in memory per transaction in reorder buffer, which is used in streaming changes via logical replication. After that, changes are spooled to disk. | integer | `4096` | n/a | user |
| yb_test_analyze_dont_reset_mutations | [Test Only GUC] - When set, a manual ANALYZE does not reset the auto-analyze mutation counters, reverting to the pre-reset behavior. | bool | `off` | n/a | user |
| yb_test_block_index_phase | Block the given index creation phase. Valid values are "indislive", "indisready", "backfill", and "postbackfill". Any other value is ignored. | string | empty | n/a | sighup |
| yb_test_collation | When set, inject code to make psql output stable across linux and mac. | bool | `off` | n/a | user |
| yb_test_delay_after_applying_inval_message_ms | When > 0, add a delay after applying invalidation messages. | integer | `0` | n/a | user |
| yb_test_delay_next_ddl | When set, the next DDL will be delayed by this many ms prior to commit. | real | `0` | `ms` | user |
| yb_test_delay_set_local_tserver_inval_message_ms | When > 0, add a delay before calling YBCPgSetTserverCatalogMessageList. | integer | `0` | n/a | user |
| yb_test_fail_all_drops | When set, all drops will fail. | bool | `off` | n/a | superuser |
| yb_test_fail_drop_after_heap_drop | Test fault injection: fail drop after heap_drop_with_catalog. | bool | `off` | n/a | superuser |
| yb_test_fail_index_state_change | Fails index backfill at given stage. Valid values are "indisready" and "postbackfill".Any other value is ignored. | string | empty | n/a | user |
| yb_test_fail_next_ddl | When set to non-zero, the next DDL will fail: 1=ERROR, 2=FATAL, 3=PANIC, 4=crash, 5=conflict. | integer | `0` | n/a | superuser |
| yb_test_fail_next_inc_catalog_version | When set, the next increment catalog version will fail right before it's done. This only works when catalog version is stored in pg_yb_catalog_version. | bool | `off` | n/a | user |
| yb_test_fail_table_rewrite_after_creation | When set, DDLs that rewrite tables/indexes will fail after the new table is created. | bool | `off` | n/a | user |
| yb_test_fatal_after_notifs_queue_write | When true, the notifications poller exits with FATAL after writing to the async queue but before the CDC ack. | bool | `off` | n/a | sighup |
| yb_test_index_check_num_batches_per_snapshot | Used to test yb_index_check(). If set to > 0, number of index rows processed per snapshot is equal to yb_test_index_check_num_batches_per_snapshot*yb_bnl_batch_size If set to 0, yb_index_check() will execute in single snapshot mode. | integer | `-1` | n/a | user |
| yb_test_inval_message_portability | When set, fill padding bytes with zeros when creating a shared invalidation message. | bool | `off` | n/a | user |
| yb_test_invalidate_relcache_in_planner | When set, the relcache entries for every base relation and its indexes will be invalidated after add_base_rels_to_query() in query_planner(). | bool | `off` | n/a | superuser |
| yb_test_make_all_ddl_statements_incrementing | When set, all DDL statements will cause the catalog version to increment. This mainly affects CREATE commands such as CREATE TABLE, CREATE VIEW, and CREATE SEQUENCE. This also enables negative catcache entries. | bool | `on` | n/a | sighup |
| yb_test_notify_queue_max_pages | When set to a positive value, artificially limits the NOTIFY queue to this many pages for testing. | integer | `0` | n/a | sighup |
| yb_test_planner_custom_plan_threshold | The number of times to force custom plan generation for prepared statements before considering a generic plan. | integer | `5` | n/a | user |
| yb_test_preload_catalog_tables | When set, force a full catalog cache refresh before executing the next top level statement. | bool | `off` | n/a | user |
| yb_test_reset_retry_counts | Restricts the number of retries for transaction conflicts. For testing purposes. | integer | `-1` | n/a | user |
| yb_test_skip_binding_scan_keys | For YB scans, skip binding scan keys to pggate. ybgin and internal scans are not affected. | bool | `off` | n/a | user |
| yb_test_sleep_before_executor_start_ms | Sleep before executing a statement. Can be used to simulate race conditions where catalog is updated between planning and execution. | integer | `0` | n/a | user |
| yb_test_slowdown_index_check | Slows down yb_index_check() by sleeping for 1s after processing every row. Used in tests to simulate long running yb_index_check(). | bool | `off` | n/a | superuser |
| yb_test_system_catalogs_creation | Relaxes some internal sanity checks for system catalogs to allow creating them. | bool | `off` | n/a | superuser |
| yb_test_table_rewrite_keep_old_table | When set, DDLs that rewrite tables/indexes will not drop the old relfilenode/DocDB table. | bool | `off` | n/a | superuser |
| yb_test_ybgin_disable_cost_factor | The multiplier to disable_cost to add when costing ybgin index scans that may not be supported. | real | `2` | n/a | user |
| yb_user_ddls_preempt_auto_analyze | If object locking is off (i.e., enable_object_locking_for_table_locks=false), concurrent DDLs might face a conflict error on the catalog version increment at the end after doing all the work. Setting this flag enables a fail-fast strategy by locking the catalog version at the start of DDLs, causing conflict errors to occur before useful work is done. This flag is only applicable without object locking. If object locking is enabled, it ensures that concurrent DDLs block on each other for serialization. Also, this flag is valid only if yb_enable_invalidation_messages is enabled. | bool | `on` | n/a | user |
| yb_walsender_poll_sleep_duration_empty_ms | Time in milliseconds for which Walsender waits before fetching the next batch of changes from the CDC service in case the last received response was empty. | integer | `10` | `ms` | user |
| yb_walsender_poll_sleep_duration_nonempty_ms | Time in milliseconds for which Walsender waits before fetching the next batch of changes from the CDC service in case the last received response was non-empty. | integer | `1` | `ms` | user |
| yb_xcluster_automatic_mode_target_ddl | Used to identify DDLs executed in Automatic xCluster mode target universe. For example, DDL operations will skip the data loading phase, including table rewrites and nonconcurrent indexes. Sequence restarts via TRUNCATE TABLE are also skipped.WARNING: Incorrect usage will result in data loss. | bool | `off` | n/a | superuser |
| yb_xcluster_ddl_replication.TEST_replication_role_override | Test override for replication role. Values: ``, `NONE`, `SOURCE`, `TARGET`. | enum | empty | n/a | superuser |
