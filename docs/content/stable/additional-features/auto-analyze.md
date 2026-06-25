---
title: Auto Analyze service
headerTitle: Auto Analyze service
linkTitle: Auto Analyze
description: Use the Auto Analyze service to keep table statistics up to date
headcontent: Keep table statistics up to date automatically
menu:
  stable:
    identifier: auto-analyze
    parent: additional-features
    weight: 100
aliases:
  - /stable/explore/query-1-performance/auto-analyze/
type: docs
---

To create optimal plans for queries, the query planner needs accurate and up-to-date statistics related to tables and their columns. These statistics are also used by the YugabyteDB [cost-based optimizer](../../reference/configuration/yb-tserver/#yb-enable-base-scans-cost-model) (CBO) to create optimal execution plans for queries. To generate the statistics, you run the [ANALYZE](../../api/ysql/the-sql-language/statements/cmd_analyze/) command. ANALYZE collects statistics about the contents of tables in the database, and stores the results in the `pg_statistic` system catalog.

Similar to [PostgreSQL autovacuum](https://www.postgresql.org/docs/current/routine-vacuuming.html#AUTOVACUUM), the YugabyteDB Auto Analyze service automates the execution of ANALYZE commands for any table where rows have changed more than a configurable threshold for the table. This ensures table statistics are always up-to-date.

## Enable Auto Analyze

For new universes running v2025.2 or later, Auto Analyze is enabled by default when you deploy using yugabyted, YugabyteDB Anywhere, or YugabyteDB Aeon.

In addition, when upgrading a deployment to v2025.2 or later, if the universe has the cost-based optimizer enabled (`on`), YugabyteDB will enable Auto Analyze.

You can explicitly enable or disable auto analyze by setting `ysql_enable_auto_analyze` on all yb-tservers.

For example, to create a single-node [yugabyted](../../reference/configuration/yugabyted/) cluster with Auto Analyze explicitly enabled, use the following command:

```sh
./bin/yugabyted start \
    --tserver_flags "ysql_enable_auto_analyze=true"
```

## Configure Auto Analyze

The auto analyze service counts the number of mutations (INSERT, UPDATE, and DELETE) to a table and triggers ANALYZE on the table automatically when certain thresholds are reached. You can configure this behavior using the following settings.

A table needs to accumulate a minimum number of mutations before it is considered for ANALYZE. This minimum is the sum of:

- A fraction of the table size. This is controlled by [ysql_auto_analyze_scale_factor](../../reference/configuration/yb-tserver/#ysql-auto-analyze-scale-factor). This setting defaults to 0.1, which translates to 10% of the current table size. Current table size is determined by the [reltuples](https://www.postgresql.org/docs/15/catalog-pg-class.html#:~:text=CREATE%20INDEX.-,reltuples,-float4) column value stored in the `pg_class` catalog entry for that table.
- A static count of [ysql_auto_analyze_threshold](../../reference/configuration/yb-tserver/#ysql-auto-analyze-threshold) (default 50) mutations. This setting ensures that small tables are not aggressively ANALYZED because the scale factor requirement is easily met.

Separately, Auto Analyze also considers cooldown settings for a table so as to not trigger ANALYZE aggressively. After every run of ANALYZE on a table, a cooldown period is enforced before the next run of ANALYZE on that table, even if the mutation thresholds are met. The cooldown period starts from [ysql_auto_analyze_min_cooldown_per_table](../../reference/configuration/yb-tserver/#ysql-auto-analyze-min-cooldown-per-table) (default: 10 seconds) and exponentially increases to  [ysql_auto_analyze_max_cooldown_per_table](../../reference/configuration/yb-tserver/#ysql-auto-analyze-max-cooldown-per-table) (default: 24 hours). Cooldown values for a table do not reset. This means that in most cases, it is expected that, after a while, a frequently updated table is only analyzed once every `ysql_auto_analyze_max_cooldown_per_table` period.

For more information on flags used to configure the Auto Analyze service, refer to [Auto Analyze service flags](../../reference/configuration/yb-tserver/#auto-analyze-service-flags). 

### Storage configuration parameters

You can configure these settings per-table using ALTER TABLE [storage parameters](../../api/ysql/the-sql-language/statements/ddl_alter_table/#set-param-param) (also known as relopts) (v2025.2.4.0 and later). Per-table storage parameters take precedence over the values specified via flags.

The following table shows the Auto Analyze flags and their corresponding parameter.

| Flag | Table-level storage parameter |
| :--- | :--- |
| `ysql_enable_auto_analyze` | `yb_auto_analyze_enabled` |
| `ysql_auto_analyze_threshold` | `yb_auto_analyze_threshold` |
| `ysql_auto_analyze_scale_factor` | `yb_auto_analyze_scale_factor` |
| `ysql_auto_analyze_min_cooldown_per_table` | `yb_auto_analyze_min_cooldown` |
| `ysql_auto_analyze_max_cooldown_per_table` | `yb_auto_analyze_max_cooldown` |
| `ysql_auto_analyze_cooldown_per_table_scale_factor` | `yb_auto_analyze_cooldown_scale_factor` |

For example, to change the scale factor and minimum threshold for a table, you can run the following:

```sql
ALTER TABLE tbl1 SET (
  yb_auto_analyze_threshold = 500,
  yb_auto_analyze_scale_factor = 0.05
);
```

To disable auto analyze for a specific table:

```sql
ALTER TABLE unused_table SET(
  yb_auto_analyze_enabled = false
);
```

## Example

With Auto Analyze enabled, try the following SQL statements.

```sql
CREATE TABLE test (k INT PRIMARY KEY, v INT);
SELECT reltuples FROM pg_class WHERE relname = 'test';
```

```output
 reltuples
-----------
        -1
(1 row)
```

```sql
INSERT INTO test SELECT i, i FROM generate_series(1, 100) i;
```

Wait for few seconds.

```sql
SELECT reltuples FROM pg_class WHERE relname = 'test';
```

```output
 reltuples
-----------
       100
(1 row)
```

## Observability

To inspect what the Auto Analyze service is tracking for each table, use the `yb_stat_auto_analyze()` function. This is intended for debugging and observability, with one row per table that the service is currently tracking.

```sql
SELECT * FROM yb_stat_auto_analyze();
```

The function returns the following columns.

| Column | Type | Description |
| :--- | :--- | :--- |
| `relid` | oid | Identifier of the table. This is the table's [relfilenode](https://www.postgresql.org/docs/15/catalog-pg-class.html#:~:text=relfilenode), which is equal to the table's OID (`pg_class.oid`) unless the table has been rewritten. |
| `schemaname` | text | Name of the schema that contains the table. |
| `relname` | text | Name of the table. |
| `mutations` | bigint | Number of mutations (INSERT, UPDATE, and DELETE) accumulated for the table since its last successful ANALYZE. The service triggers ANALYZE when this count crosses the configured threshold (and the table's cooldown period has elapsed). |
| `last_analyze_info` | jsonb | Recent auto analyze activity for the table, or NULL if the service has not yet run ANALYZE on the table. |

The `last_analyze_info` column contains an `analyze_history` array holding the most recent auto analyze events for the table (oldest first). Each event has the following fields:

- `timestamp` - Time at which ANALYZE ran, in microseconds since the Unix epoch.
- `cooldown` - Cooldown period (in microseconds) enforced after that run before the next ANALYZE is allowed. As described in [Configure Auto Analyze](#configure-auto-analyze), this value starts at `ysql_auto_analyze_min_cooldown_per_table` and increases on each run (by `ysql_auto_analyze_cooldown_per_table_scale_factor`) up to `ysql_auto_analyze_max_cooldown_per_table`.

### Example

Continuing from the [Example](#example) above (table `test` in the `public` schema), repeatedly inserting rows causes the service to run ANALYZE more than once. Each ANALYZE adds an entry to `analyze_history`:

```sql
SELECT * FROM yb_stat_auto_analyze() WHERE relname = 'test';
```

```output
 relid | schemaname | relname | mutations |                                              last_analyze_info
-------+------------+---------+-----------+----------------------------------------------------------------------------------------------------------------------------
 16384 | public     | test    |        25 | {"analyze_history": [{"cooldown": 10000000, "timestamp": 1768779881369060}, {"cooldown": 20000000, "timestamp": 1768779901500000}]}
(1 row)
```

In this example, ANALYZE has run twice. The first run recorded a cooldown of 10 seconds (`10000000` microseconds, the default `ysql_auto_analyze_min_cooldown_per_table`), and the second run doubled it to 20 seconds (`20000000` microseconds, using the default scale factor of 2). The `mutations` value of `25` reflects rows changed since the last ANALYZE that have not yet crossed the threshold for the next run.

## Limitations

ANALYZE is technically considered a DDL statement (schema change) and normally conflicts with other [concurrent DDLs](../../best-practices-operations/administration/#concurrent-ddl-during-a-ddl-operation). However, when run via the auto analyze service, ANALYZE can run concurrently with other DDL. In this case, ANALYZE is pre-empted by concurrent DDL and will be retried at a later point. However, when [transactional DDL](../../explore/transactions/transactional-ddl/) is enabled (off by default), certain kinds of transactions that contain DDL may face a `kConflict` error when a background ANALYZE from the auto analyze service interrupts this transaction. In such cases, it is recommended to disable the auto analyze service explicitly and trigger ANALYZE manually. Issue {{<issue 28903>}} tracks this scenario.
