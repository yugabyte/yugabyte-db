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

{{< warning title="Advanced parameters" >}}
Most deployments should not need to change these parameters. The defaults are chosen to suit the majority of workloads, and changing a parameter without understanding its effect can degrade performance, correctness, or stability. Change one only when you have a specific reason to, and test the change before applying it in production.
{{< /warning >}}

YSQL supports the PostgreSQL [server configuration parameters](https://www.postgresql.org/docs/15/runtime-config.html), plus the YugabyteDB-specific parameters listed on this page. Frequently used parameters are documented in detail under [YSQL configuration parameters](../yb-tserver/#ysql-configuration-parameters) on the YB-TServer reference page; entries below link to that page where such an entry exists.

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

## Reading the entries

Each parameter below lists the following:

- **Default** - the built-in default (`pg_settings.boot_val`). A deployment can start with a different value if it is set using a flag, so check `pg_settings.reset_val` on your cluster.
- **Type** - `bool`, `integer`, `real`, `string`, or `enum`.
- **Unit** - the unit the value is interpreted in, where the parameter has one.
- **Context** - when the parameter can be set, as described in the following table.
- {{% tags/feature/restart-needed %}} - the parameter can only be set in the cluster configuration, and the YSQL process must be restarted for a change to take effect.

The context determines who can change a parameter and whether a restart is needed.

| Context | Who can set it | Takes effect |
| :--- | :--- | :--- |
| `user` | Any user, for their own session | Immediately |
| `superuser` | Superusers only | Immediately |
| `backend` | Set when the connection is established | At connection start |
| `sighup` | Cluster configuration only (yb-tserver flag) | On configuration reload; no restart needed |
| `postmaster` | Cluster configuration only (yb-tserver flag) | Requires a restart of the YSQL process |


## Query tuning and the optimizer

##### yb_bnl_batch_size

{{% tags/wrap %}}

Default: `1024`

Type: `integer`

Context: `user`
{{% /tags/wrap %}}

Batch size of nested loop joins. Set to 1 to always use simple nested loop joins.

For more detail, see [yb_bnl_batch_size](../yb-tserver/#yb-bnl-batch-size).

##### yb_bnl_enable_hashing

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enables batched nested loop joins to use hashing to process its matches.

##### yb_bnl_optimize_first_batch

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enables batched nested loop joins to predict the size of its first batch and optimize if it's smaller than yb_bnl_batch_size.

##### yb_disable_parallel_query_in_ddl

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Disables parallel query for the SELECT planned by DDLs such as CREATE TABLE AS, SELECT INTO, CREATE/REFRESH MATERIALIZED VIEW, COPY (query) TO, and EXPLAIN [ANALYZE] CREATE TABLE AS. Enabled by default because parallel query in these DDLs has not been QA tested in YugabyteDB. Set to off as an escape hatch to restore upstream PostgreSQL behavior for workloads that rely on it.

##### yb_enable_advanced_index_cond_fold

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enable advanced folding of same-column index conditions, including tightening inequality bounds across scan keys, intersecting IN arrays, and detecting additional contradictions at bind time.

For more detail, see [yb_enable_advanced_index_cond_fold](../yb-tserver/#yb-enable-advanced-index-cond-fold).

##### yb_enable_batchednl

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enables the planner's use of batched nested-loop join plans.

For more detail, see [yb_enable_batchednl](../yb-tserver/#yb-enable-batchednl).

##### yb_enable_bitmapscan

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enables the planner's use of YB bitmap-scan plans. To use YB Bitmap Scans, both yb_enable_bitmapscan and enable_bitmapscan must be true.

For more detail, see [yb_enable_bitmapscan](../yb-tserver/#yb-enable-bitmapscan).

##### yb_enable_cbo

{{% tags/wrap %}}

Default: `legacy_mode`

Type: `enum`

Context: `user`
{{% /tags/wrap %}}

Enable YB cost model. Values: `off`, `on`, `legacy_mode`, `legacy_stats_mode`, `legacy_bnl_mode`, `legacy_stats_bnl_mode`, `legacy_ignore_stats_bnl_mode`.

For more detail, see [yb_enable_cbo](../yb-tserver/#yb-enable-cbo).

##### yb_enable_derived_equalities

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enable derivation of additional equalities for generated columns and expression indexes.

For more detail, see [yb_enable_derived_equalities](../yb-tserver/#yb-enable-derived-equalities).

##### yb_enable_derived_saops

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

If true, derives additional scalar array operation conditions from table constraints and adds them to queries to improve performance. Has no impact in case yb_max_merge_scan_streams is 0.

For more detail, see [yb_enable_derived_saops](../yb-tserver/#yb-enable-derived-saops).

##### yb_enable_distinct_pushdown

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Push supported DISTINCT operations to DocDB.

##### yb_enable_expression_pushdown

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Push supported expressions down to DocDB for evaluation.

##### yb_enable_geolocation_costing

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Allow the optimizer to cost and choose between duplicate indexes based on locality.

##### yb_enable_hash_batch_in

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

GUC variable that enables batching RPCs of generated for IN queries on hash keys issued to the same tablets.

##### yb_enable_index_aggregate_pushdown

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Push supported index aggregate operations to DocDB. This affects IndexScan, not IndexOnlyScan.

##### yb_enable_index_backfill_scan_optimization

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enables index backfill scan optimizations. If true, index build/backfill reads only the columns needed for the index and pushes partial index predicates down to the base table scan.

##### yb_enable_inplace_index_update

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enables the in-place update of non-key columns of secondary indexes when key columns of the index are not updated. This is useful when updating the included columns in a covering index among others.

##### yb_enable_parallel_scan_colocated

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

When set, allows parallel scan of the colocated relations.

##### yb_enable_parallel_scan_hash_sharded

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

When set, allows parallel scan of the hash sharded relations.

##### yb_enable_parallel_scan_range_sharded

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

When set, allows parallel scan of the range sharded relations.

##### yb_enable_parallel_scan_system

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

When set, allows parallel scan of the system relations.

##### yb_enable_planner_trace

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enables planner tracing.

##### yb_enable_primary_key_decode_from_index

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Allow Index Only Scans to decode base table primary key columns from secondary index entries. When enabled, PK columns are decoded from ybidxbasectid in secondary index entries.

##### yb_enable_saop_pushdown

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Push supported scalar array operations down to DocDB for evaluation.

##### yb_enable_sequence_pushdown

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Allow nextval() to fetch the value range and advance the sequence value in a single operation.

##### yb_enable_update_reltuples_after_create_index

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enables update of reltuples in pg_class for the base table and index after creating the index. When disabled, reltuples are not updated during concurrent index creation and only index reltuples are updated during non-concurrent index creation.

##### yb_explicit_row_lock_skip_locked_max_read_ahead

{{% tags/wrap %}}

Default: `1`

Type: `integer`

Context: `user`
{{% /tags/wrap %}}

Max number of rows that are read ahead for SKIP LOCKED explicit row locking. Set to 1 to preserve original behavior, read ahead is not performed by default.

For more detail, see [yb_explicit_row_lock_skip_locked_max_read_ahead](../yb-tserver/#yb-explicit-row-lock-skip-locked-max-read-ahead).

##### yb_explicit_row_locking_batch_size

{{% tags/wrap %}}

Default: `1024`

Type: `integer`

Context: `user`
{{% /tags/wrap %}}

Batch size of explicit row locking. Set to 1 to conserve default behavior, batching is disabled by default.

##### yb_fetch_row_limit

{{% tags/wrap %}}

Default: `1024`

Type: `integer`

Context: `user`
{{% /tags/wrap %}}

Maximum number of rows to fetch per scan. 0 = No limit.

For more detail, see [yb_fetch_row_limit](../yb-tserver/#yb-fetch-row-limit).

##### yb_fetch_size_limit

{{% tags/wrap %}}

Default: `0`

Type: `integer`

Unit: `B`

Context: `user`
{{% /tags/wrap %}}

Maximum size of a fetch response. 0 = No limit.

For more detail, see [yb_fetch_size_limit](../yb-tserver/#yb-fetch-size-limit).

##### yb_hinted_uids

{{% tags/wrap %}}

Default: empty

Type: `string`

Context: `user`
{{% /tags/wrap %}}

Node UIDS to prefer in cost comparisons.

##### yb_index_state_flags_update_delay

{{% tags/wrap %}}

Default: `0`

Type: `integer`

Unit: `ms`

Context: `user`
{{% /tags/wrap %}}

Delay in milliseconds between stages of online index build. Set high to give online transactions more time to complete.

##### yb_lock_pk_single_rpc

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Use single RPC to select and lock when PK is specified. If possible (no conflicting filters in the plan), use a single RPC to select and lock, when a locking clause is provided, in isolation levels REPEATABLE READ and READ COMMITTED.

##### yb_max_merge_scan_streams

{{% tags/wrap %}}

Default: `0`

Type: `integer`

Context: `user`
{{% /tags/wrap %}}

Sets the maximum number of streams tolerated for merge scan. For YB LSM index scans, when multiple merge-scan-eligible scalar array operations are involved, they are combined until their cartesian product's cardinality reaches this limit. Merge scan is per index scan, and the limit applies per index scan, not globally. Set to 0 to disable.

For more detail, see [yb_max_merge_scan_streams](../yb-tserver/#yb-max-merge-scan-streams).

##### yb_network_fetch_cost

{{% tags/wrap %}}

Default: `4`

Type: `real`

Context: `user`
{{% /tags/wrap %}}

Sets the planner's estimate of the fixed cost of fetching a batch of rows from a YB relation.

##### yb_parallel_range_rows

{{% tags/wrap %}}

Default: `0`

Type: `integer`

Context: `user`
{{% /tags/wrap %}}

The number of rows to plan per parallel worker.

##### yb_parallel_range_size

{{% tags/wrap %}}

Default: `16777216`

Type: `integer`

Unit: `B`

Context: `user`
{{% /tags/wrap %}}

Approximate size of parallel range for DocDB relation scans.

##### yb_pg_stat_plans_plan_format

{{% tags/wrap %}}

{{<tags/feature/restart-needed>}}

Default: `json`

Type: `enum`

Context: `postmaster`
{{% /tags/wrap %}}

Plan format for QPM. Values: `text`, `xml`, `json`, `yaml`.

##### yb_pg_stat_plans_track

{{% tags/wrap %}}

Default: `all`

Type: `enum`

Context: `superuser`
{{% /tags/wrap %}}

Selects which statements are tracked by QPM. Values: `none`, `top`, `all`.

##### yb_plpgsql_disable_prefetch_in_for_query

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Disable prefetching in a PLPGSQL FOR loop over a query.

##### yb_prefer_bnl

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

If enabled, planner will force a preference of batched nested loop join plans over classic nested loop join plans.

##### yb_prefetch_column_statistics

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Prefetch a relation's column statistics in one catalog read during planning.

##### yb_sampling_algorithm

{{% tags/wrap %}}

Default: `block_based_sampling`

Type: `enum`

Context: `user`
{{% /tags/wrap %}}

Which sampling algorithm to use for YSQL. full_table_scan - scan the whole table and pick random rows, block_based_sampling - sample the table for a set of blocks, then scan selected blocks to form a final rows sample. Values: `full_table_scan`, `block_based_sampling`.

For more detail, see [yb_sampling_algorithm](../yb-tserver/#yb-sampling-algorithm).

##### yb_skip_redundant_update_ops

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `sighup`
{{% /tags/wrap %}}

Enables the comparison of old and new values of columns specified in the SET clause of YSQL UPDATE queries to skip redundant secondary index updates and redundant constraint checks.

For more detail, see [yb_skip_redundant_update_ops](../yb-tserver/#yb-skip-redundant-update-ops).

##### yb_update_optimization_infra

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `sighup`
{{% /tags/wrap %}}

Enables optimizations of YSQL UPDATE queries. This includes (but not limited to) skipping redundant secondary index updates and redundant constraint checks.

##### yb_use_cluster_config_for_geolocation_costing

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

When no tablespace is assigned to table, use cluster replication info to estimate network costs.

##### yb_use_hash_splitting_by_default

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enables hash splitting as the default method for primary key and index sorting in LSM indexes. When set to true, the default sorting for the first primary/index key column in LSM indexes is HASH, Setting this to false changes the default to ASC, making it compatible with standard PostgreSQL behavior. This setting is useful for optimizing query performance, especially for migrations from PostgreSQL or scenarios where index-based sorting and sharding behavior are critical.

For more detail, see [yb_use_hash_splitting_by_default](../yb-tserver/#yb-use-hash-splitting-by-default).

##### yb_wait_for_backends_catalog_version_timeout

{{% tags/wrap %}}

Default: `900000`

Type: `integer`

Unit: `ms`

Context: `user`
{{% /tags/wrap %}}

Timeout in milliseconds to wait for backends to reach desired catalog versions. The actual time spent may be longer than that by as much as master flag wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms. Setting to zero or less results in no timeout. Currently used by concurrent CREATE INDEX.



## Transactions and statement behavior

##### yb_default_copy_from_rows_per_transaction

{{% tags/wrap %}}

Default: `20000`

Type: `integer`

Context: `user`
{{% /tags/wrap %}}

Sets the batch number of rows to copy from the source to table.

For more detail, see [yb_default_copy_from_rows_per_transaction](../yb-tserver/#yb-default-copy-from-rows-per-transaction).

##### yb_disable_transactional_writes

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Sets the boolean flag to disable transaction writes.

##### yb_enable_docdb_tracing

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enables tracing for the commands in this session.

##### yb_enable_retry_after_non_atomic_commit

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Allow query layer retries of CALL/DO statements after an in-procedure COMMIT. When enabled, the query layer will retry CALL and DO statements on conflict or read-restart errors even if the procedure or DO block has already performed a COMMIT. This can lead to re-execution of already-committed work (e.g., duplicate inserts) and is provided only as a compatibility option to revert to the old behavior. The default (off) is the safe behavior.

##### yb_enable_upsert_mode

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Sets the boolean flag to enable or disable upsert mode for writes. When the target table has secondary indexes, triggers, or foreign key constraints, upsert mode is automatically disabled to prevent correctness issues. Consider using INSERT ... ON CONFLICT for true upsert semantics instead.

##### yb_extra_commands_to_retry

{{% tags/wrap %}}

Default: empty

Type: `string`

Context: `user`
{{% /tags/wrap %}}

Comma-separated list of command tags to additionally retry on a serialization error. By default the query layer retries SELECT/INSERT/UPDATE/DELETE, and under READ COMMITTED also any command tag on kConflict/kDeadlock/kAborted (historical) and CALL/DO whose body ran only those same four statements (or nested CALL/DO with only those same four statements) on kReadRestart. Each tag listed here joins the retriable set; tag names are case-insensitive and follow the names shown in psql command tags. COPY, COPY FROM, and ANALYZE are rejected at SET time -- they are not safe to retry. Use with caution: re-executing DDL or other utility statements may have unintended effects.

##### yb_extra_commands_to_retry_in_proc

{{% tags/wrap %}}

Default: empty

Type: `string`

Context: `user`
{{% /tags/wrap %}}

Comma-separated list of command tags that, when run inside a CALL/DO body, do not block retry of the enclosing CALL/DO. By default a CALL/DO is retried only when its body ran nothing but SELECT/INSERT/UPDATE/DELETE or nested CALL/DO, since a retry re-runs the entire body. Each tag listed here joins that set, e.g. 'LOCK TABLE'. Tag names are case-insensitive. COPY, COPY FROM, and ANALYZE are rejected at SET time -- they are not safe to retry. Use with caution: re-executing the listed statements may have unintended effects.

##### yb_fast_path_for_colocated_copy

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enable fast-path transaction for copy on colocated tables. For testing now.

##### yb_fk_references_cache_limit

{{% tags/wrap %}}

Default: `65535`

Type: `integer`

Context: `user`
{{% /tags/wrap %}}

Sets the maximum size for the FK reference cache filled by the INSERT, SELECT ... FOR KEY SHARE or similar statements.

##### yb_follower_read_staleness_ms

{{% tags/wrap %}}

Default: `30000`

Type: `integer`

Context: `user`
{{% /tags/wrap %}}

Sets the staleness (in ms) to be used for performing follower reads.

For more detail, see [yb_follower_read_staleness_ms](../yb-tserver/#yb-follower-read-staleness-ms).

##### yb_follower_reads_behavior_before_fixing_20482

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Controls whether ysql follower reads that is enabled inside a transaction block should take effect in the same transaction or not. Prior to fixing #20482 the behavior was that the change does not affect the current transaction but only affects subsequent transactions. The flag is intended to be used if there is a customer who relies on the old behavior.

##### yb_ignore_bool_cond_for_legacy_estimate

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Ignore boolean condition for row count estimate in legacy cost model. Negates the side effect on legacy mode row count estimate introduced by the fix "[#26266] YSQL: Add BOOL_LSM_FAM_OID to boolean family" for backward compatibility.

##### yb_ignore_freeze_with_copy

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Ignore the FREEZE flag on COPY FROM command.

##### yb_insert_on_conflict_read_batch_size

{{% tags/wrap %}}

Default: `1024`

Type: `integer`

Context: `superuser`
{{% /tags/wrap %}}

Maximum batch size for arbiter index reads during INSERT ON CONFLICT. A value of 0 disables this feature.

For more detail, see [yb_insert_on_conflict_read_batch_size](../yb-tserver/#yb-insert-on-conflict-read-batch-size).

##### yb_max_query_layer_retries

{{% tags/wrap %}}

Default: `60`

Type: `integer`

Context: `user`
{{% /tags/wrap %}}

Max number of internal query layer retries of a statement. Max number of query layer retries of a statement for the following errors: serialization error (40001), "Restart read required" (40001), deadlock detected (40P01). In Repeatable Read and Serializable isolation levels, the query layer only retries errors faced in the first statement of a transaction. In READ COMMITTED isolation, the query layer has the ability to do retries for any statement in a transaction. Retries are not possible if some response data has already been sent to the client while the query is still executing. This happens if the output buffer, the size of which is configurable using the TServer gflag ysql_output_buffer_size, has filled at least once and is flushed.

##### yb_pg_batch_detection_mechanism

{{% tags/wrap %}}

Default: `detect_by_peeking`

Type: `enum`

Context: `user`
{{% /tags/wrap %}}

The drivers use message protocol to communicate with PG. The driver does not inform PG in advance about a Batch execution. We need to identify a batch because in that case the single-shard optimization should be disabled. Postgres drivers pipeline messages and we exploit this to peek the message following 'Execute' to detect a batch. This may lead to some unforeseen bugs, so this GUC provides a way to disable the single-shard optimization completely or go back to the behavior before #16446 was fixed. Values: `detect_by_peeking`, `assume_all_batch_executions`, `ignore_batch_delete_and_update_may_fail`.

##### yb_planner_custom_plan_for_partition_pruning

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

If enabled, choose custom plan over generic plan for prepared statements based on the number of partition pruned.

##### yb_read_from_followers

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Allow any statement that generates a read request to go to any node.

For more detail, see [yb_read_from_followers](../yb-tserver/#yb-read-from-followers).

##### yb_read_time

{{% tags/wrap %}}

Default: `0`

Type: `string`

Context: `superuser`
{{% /tags/wrap %}}

Allows querying the database as of a point in time in the past. Takes a unix timestamp in microseconds. Zero means reading data as of current time. User should set this variable with caution. Currently, it can only read old data without schema changes. In other words, it should not be set to a timestamp before a DDL operation has been performed. Write-DML or DDL queries are not allowed while this variable is set.

For more detail, see [yb_read_time](../yb-tserver/#yb-read-time).

##### yb_transaction_priority_lower_bound

{{% tags/wrap %}}

Default: `0`

Type: `real`

Context: `user`
{{% /tags/wrap %}}

Sets lower bound for priority used by transactions of this session.

##### yb_transaction_priority_upper_bound

{{% tags/wrap %}}

Default: `1`

Type: `real`

Context: `user`
{{% /tags/wrap %}}

Sets upper bound for priority used by transactions of this session.

##### yb_xcluster_consistency_level

{{% tags/wrap %}}

Default: `database`

Type: `string`

Context: `user`
{{% /tags/wrap %}}

Controls the consistency level of xCluster replicated databases. Valid values are "database" and "tablet".



## Locking

##### yb_enable_ddl_savepoint_infra

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `sighup`
{{% /tags/wrap %}}

Allow enabling ddl savepoint support.

##### yb_enable_pg_locks

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `superuser`
{{% /tags/wrap %}}

Enable the pg_locks view. This view provides information about the locks held by active postgres sessions.

##### yb_locks_max_transactions

{{% tags/wrap %}}

Default: `16`

Type: `integer`

Context: `user`
{{% /tags/wrap %}}

Sets the maximum number of transactions for which to return rows in pg_locks.

For more detail, see [yb_locks_max_transactions](../yb-tserver/#yb-locks-max-transactions).

##### yb_locks_min_txn_age

{{% tags/wrap %}}

Default: `1000`

Type: `integer`

Unit: `ms`

Context: `user`
{{% /tags/wrap %}}

Sets the minimum transaction age for results from pg_locks.

For more detail, see [yb_locks_min_txn_age](../yb-tserver/#yb-locks-min-txn-age).

##### yb_locks_txn_locks_per_tablet

{{% tags/wrap %}}

Default: `200`

Type: `integer`

Context: `user`
{{% /tags/wrap %}}

Sets the maximum number of rows per transaction per tablet to return in pg_locks.

For more detail, see [yb_locks_txn_locks_per_tablet](../yb-tserver/#yb-locks-txn-locks-per-tablet).

##### yb_pg_locks_integrate_advisory_locks

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `sighup`
{{% /tags/wrap %}}

Enables pg_locks to integrate and display advisory locks details correctly.



## Observability and statistics

##### yb_ash_circular_buffer_size

{{% tags/wrap %}}

{{<tags/feature/restart-needed>}}

Default: `0`

Type: `integer`

Unit: `kB`

Context: `postmaster`
{{% /tags/wrap %}}

Size (in KiBs) of ASH circular buffer that stores the samples. If this is 0, the size will be calculated based on the number of cores.

##### yb_ash_sample_size

{{% tags/wrap %}}

Default: `500`

Type: `integer`

Context: `sighup`
{{% /tags/wrap %}}

Number of samples captured from each component per sampling event.

##### yb_ash_sampling_interval_ms

{{% tags/wrap %}}

Default: `1000`

Type: `integer`

Unit: `ms`

Context: `sighup`
{{% /tags/wrap %}}

Time (in milliseconds) between two consecutive sampling events.

##### yb_enable_ash

{{% tags/wrap %}}

{{<tags/feature/restart-needed>}}

Default: `on`

Type: `bool`

Context: `postmaster`
{{% /tags/wrap %}}

Enable Active Session History for sampling and instrumenting YSQL and YCQL queries, and various background activities.

##### yb_enable_pg_stat_statements_docdb_metrics

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `superuser`
{{% /tags/wrap %}}

If true, enable DocDB metrics collection for pg_stat_statements. This enables collection of the following metrics: docdb_seeks, docdb_nexts, docdb_prevs, docdb_read_time, docdb_write_time and docdb_obsolete_rows_scanned.

##### yb_enable_pg_stat_statements_rpc_stats

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `superuser`
{{% /tags/wrap %}}

If true, enable RPC execution time stats for pg_stat_statements.

##### yb_enable_query_diagnostics

{{% tags/wrap %}}

{{<tags/feature/restart-needed>}}

Default: `off`

Type: `bool`

Context: `postmaster`
{{% /tags/wrap %}}

Enables the collection of query diagnostics data for YSQL queries, facilitating the creation of diagnostic bundles.

##### yb_log_min_backtraces

{{% tags/wrap %}}

Default: `fatal`

Type: `enum`

Context: `superuser`
{{% /tags/wrap %}}

Sets the minimum message level for including a backtrace in the log. Errors at or above this level will have a call stack attached. Each level includes all the levels that follow it. Values: `debug5`, `debug4`, `debug3`, `debug2`, `debug1`, `info`, `notice`, `warning`, `error`, `log`, `fatal`, `panic`.

##### yb_pg_stat_plans_cache_replacement_algorithm

{{% tags/wrap %}}

{{<tags/feature/restart-needed>}}

Default: `simple_clock_lru`

Type: `enum`

Context: `postmaster`
{{% /tags/wrap %}}

Specifies cache replacement policy for Query Plan Management. Values: `simple_clock_lru`, `true_lru`.

##### yb_pg_stat_plans_max_cache_size

{{% tags/wrap %}}

{{<tags/feature/restart-needed>}}

Default: `5000`

Type: `integer`

Context: `postmaster`
{{% /tags/wrap %}}

Max number of query/plan pairs stored by QPM.

##### yb_pg_stat_plans_show_max_exec_params

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `superuser`
{{% /tags/wrap %}}

Show QPM maximum execution time parameter values.

##### yb_pg_stat_plans_track_catalog_queries

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `superuser`
{{% /tags/wrap %}}

When set, QPM tracks plans for queries referencing catalog tables.

##### yb_pg_stat_plans_verbose_plans

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `superuser`
{{% /tags/wrap %}}

Generate verbose plans in QPM.

##### yb_qpm_compress_text

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `superuser`
{{% /tags/wrap %}}

Compress QPM plan and hint text if necessary.

##### yb_query_diagnostics_bg_worker_interval_ms

{{% tags/wrap %}}

{{<tags/feature/restart-needed>}}

Default: `1000`

Type: `integer`

Unit: `ms`

Context: `postmaster`
{{% /tags/wrap %}}

Time (in milliseconds) for which the query diagnostic's background worker sleeps.

##### yb_query_diagnostics_circular_buffer_size

{{% tags/wrap %}}

{{<tags/feature/restart-needed>}}

Default: `64`

Type: `integer`

Unit: `kB`

Context: `postmaster`
{{% /tags/wrap %}}

Size of query diagnostics circular buffer that stores statuses of bundles. The circular buffer is filled sequentially until it reaches this size, then it wraps around and starts overwriting the oldest entries.

##### yb_query_diagnostics_disable_database_connection_bgworker

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `sighup`
{{% /tags/wrap %}}

This disables creating extra bgworker which creates database connection for query diagnostics. If this is set to true, ASH and schema details are not dumped.

##### yb_tcmalloc_sample_period

{{% tags/wrap %}}

Default: `1048576`

Type: `integer`

Unit: `B`

Context: `superuser`
{{% /tags/wrap %}}

TCMalloc sample interval in bytes, i.e. approximately how many bytes between sampling allocation call stacks.



## Replication and change data capture

##### yb_default_replica_identity

{{% tags/wrap %}}

Default: `CHANGE`

Type: `string`

Context: `superuser`
{{% /tags/wrap %}}

Default replica identity at the time of table creation.

##### yb_enable_replica_identity

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `superuser`
{{% /tags/wrap %}}

Allow changing replica identity via ALTER TABLE command.



## Maintenance and resource usage

##### yb_neg_catcache_ids

{{% tags/wrap %}}

Default: empty

Type: `string`

Context: `superuser`
{{% /tags/wrap %}}

Comma separated list of additional sys cache ids that are allowed to be negatively cached.



## Extension parameters

##### yb_pg_metrics.log_accesses

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `superuser`
{{% /tags/wrap %}}

Log each request received by the YSQL webserver.

##### yb_pg_metrics.log_tcmalloc_stats

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `superuser`
{{% /tags/wrap %}}

Log TCMalloc memory statistics with each request received by the YSQL webserver.

##### yb_pg_metrics.webserver_profiler_sample_period_bytes

{{% tags/wrap %}}

Default: `1048576`

Type: `integer`

Context: `superuser`
{{% /tags/wrap %}}

The interval at which Google TCMalloc should sample allocations in the YSQL webserver. If this is 0, sampling is disabled.

##### yb_xcluster_ddl_replication.enable_manual_ddl_replication

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Temporarily disable automatic xCluster DDL replication - DDLs will have to be manually executed on the target. DDL strings will still be captured and replicated, but will be marked with a 'manual_replication' flag.



## Other parameters

##### yb_allow_dockey_bounds

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `superuser`
{{% /tags/wrap %}}

If true, allow lower_bound/upper_bound fields of PgsqlReadRequestPB to be DocKeys. Only applicable for hash-sharded tables.

##### yb_conn_mgr_selective_deallocate

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `sighup`
{{% /tags/wrap %}}

Enables connection-manager-aware DEALLOCATE behavior.

##### yb_disable_auto_analyze

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Run 'ALTER DATABASE <name> SET yb_disable_auto_analyze=on' to disable auto analyze on that database. Set it to off to resume auto analyze. Setting this GUC via any other method will throw a WARNING message.

##### yb_disable_catalog_version_check

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `superuser`
{{% /tags/wrap %}}

Disable checking that read requests from this pg backend have the latest catalog version. User should set this variable with caution. It is under active development and is not recommended for production clusters. Currently, it is used by ysql_dump to read pg catalog as of time.

##### yb_enable_add_column_missing_default

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enable using the default value for existing rows after an ADD COLUMN ... DEFAULT operation.

##### yb_enable_alter_table_rewrite

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enable ALTER TABLE rewrite operations.

##### yb_enable_create_with_table_oid

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enables the ability to set table oids when creating tables or indexes.

##### yb_enable_extended_sql_codes

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Allow to return to the client SQL status codes defined by YugabyteDB (YBxxx). Those codes are used internally to determine if transparent retry is possible. If disabled, they are replaced with similar Postgres defined codes.

##### yb_enable_global_views

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `superuser`
{{% /tags/wrap %}}

Enables querying of global views.

##### yb_enable_nop_alter_role_optimization

{{% tags/wrap %}}

Default: `on`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enable nop alter role statement optimization to avoid catalog version increment if the alter role statement does not involve any change.

##### yb_explain_hide_non_deterministic_fields

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

If set, all fields that vary from run to run are hidden from the output of EXPLAIN.

##### yb_force_tablespace_locality

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Forces use of tablespace-based locality over region locality.

##### yb_force_tablespace_locality_oid

{{% tags/wrap %}}

Default: `0`

Type: `oid`

Context: `user`
{{% /tags/wrap %}}

Tablespace used for tablespace-based locality. Picked automatically if InvalidOid (default).

##### yb_format_funcs_include_yb_metadata

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Include DocDB metadata (such as tablet splits) in formatting functions exporting system catalog information.

##### yb_ignore_read_time_in_walsender

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

When set, walsender will fetch the publication as of current time if it encounters any failures while reading the catalog tables as of yb_read_time. This GUC should be set carefully and only till the time the process of upgrading logical replication streams is complete (i.e till the yb_restart_time of all the streams crosses the time of upgrade completion). Moreover this GUC should be set only after ensuring that no more DDLs (including ALTER PUBLICATION) will be encountered by the walsender.

##### yb_is_client_ysqlconnmgr

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `backend`
{{% /tags/wrap %}}

Identifies that connection is created by Ysql Connection Manager.

##### yb_make_next_ddl_statement_nonbreaking

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `superuser`
{{% /tags/wrap %}}

When set, the next ddl statement will not cause running transactions to abort. This only affects the next ddl statement and resets automatically.

##### yb_non_ddl_txn_for_sys_tables_allowed

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Enables the use of regular transactions for operating on system catalog tables in case a DDL transaction has not been started.

##### yb_read_after_commit_visibility

{{% tags/wrap %}}

Default: `strict`

Type: `enum`

Context: `user`
{{% /tags/wrap %}}

Control read-after-commit-visibility guarantee. This GUC is intended as a crutch for users migrating from PostgreSQL and new to read restart errors. Users can now largely avoid these errors when read-after-commit-visibility guarantee is not a strong requirement. This option cannot be set from within a transaction block. Configure one of the following options: (a) strict: Default Behavior. The read-after-commit-visibility guarantee is maintained by the database. However, users may see read restart errors that show "ERROR: Query error: Restart read required at: ...". The database attempts to retry on such errors internally but that is not always possible. (b) relaxed: With this option, the read-after-commit-visibility guarantee is relaxed. Do not see read restart errors but may miss recent updates with staleness bounded by clock skew. This mode does not apply to serializable isolation level and fast path writes. (c) deferred: Defers read point. Higher latency but read-after-commit-visibility guarantee is maintained. Values: `strict`, `relaxed`, `deferred`.

##### yb_refresh_matview_in_place

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `user`
{{% /tags/wrap %}}

Refresh materialized views in place.

##### yb_speculatively_execute_pl_statements

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `superuser`
{{% /tags/wrap %}}

If enabled, procedural language statements may be speculatively executed when it is safe to do so without waiting for the successful completion of previous statements. This allows any writes produced by triggers to be batched alongside their parent data-modifying writes such that the number of storages flushes may be minimized.

##### yb_toast_catcache_threshold

{{% tags/wrap %}}

Default: `2048`

Type: `integer`

Context: `user`
{{% /tags/wrap %}}

Size threshold in bytes for a catcache tuple to be compressed.

##### yb_update_max_cols_size_to_compare

{{% tags/wrap %}}

Default: `10240`

Type: `integer`

Unit: `B`

Context: `user`
{{% /tags/wrap %}}

Maximum size in bytes of columns whose data is to be compared while seeking to optimize updates. If set to 0, no size limit is applied.

##### yb_update_num_cols_to_compare

{{% tags/wrap %}}

Default: `50`

Type: `integer`

Context: `user`
{{% /tags/wrap %}}

Maximum number of columns whose data is to be compared while seeking to optimize updates. If set to 0, all applicable columns in the table will be compared.

##### yb_use_tserver_key_auth

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `backend`
{{% /tags/wrap %}}

If set, the client connection will be authenticated via 'yb-tserver-key' auth.

##### yb_whitelist_extra_statements_for_pl_speculative_execution

{{% tags/wrap %}}

Default: `off`

Type: `bool`

Context: `superuser`
{{% /tags/wrap %}}

If enabled, additional procedural language constructs are whitelisted for use in speculative execution.
