---
title: Auto Analyze service
headerTitle: Auto Analyze service
linkTitle: Auto Analyze
description: Use the Auto Analyze service to keep table statistics up to date
headcontent: Keep table statistics up to date automatically
tags:
  feature: early-access
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

Auto analyze is automatically enabled on YugabyteDB clusters when CBO is enabled (CBO is automatically enabled when a YugabyteDB cluster is created with version >= 2025.2 through YugabyteDB Aeon, YugabyteDB Anywhere or yugabyted). If needed, you can explicitly enable or disable auto analyze by setting `ysql_enable_auto_analyze` on both yb-master and yb-tserver.  

For example, to create a single-node [yugabyted](../../reference/configuration/yugabyted/) cluster with Auto Analyze explicitly enabled, use the following command:

```sh
./bin/yugabyted start \
    --master_flags "ysql_enable_auto_analyze=true" \
    --tserver_flags "ysql_enable_auto_analyze=true"
```


## Configure Auto Analyze

The auto analyze service counts the number of mutations (INSERT, UPDATE, and DELETE) to a table and triggers ANALYZE on the table automatically when certain thresholds are reached. This behavior is determined by the following knobs.

A table needs to accumulate a minimum number of mutations before it is considered for ANALYZE. This minimum is the sum of
    * A fraction of the table size - this is controlled by [ysql_auto_analyze_scale_factor](../reference/configuration/yb-tserver/#ysql-auto-analyze-scale-factor). This setting defaults to 0.1, which translates to 10% of the current table size. Current table size is determined by the [`reltuples`].(https://www.postgresql.org/docs/15/catalog-pg-class.html#:~:text=CREATE%20INDEX.-,reltuples,-float4) column value stored in the `pg_class` catalog entry for that table.
    * A static count of [ysql_auto_analyze_threshold](../reference/configuration/yb-tserver/#ysql-auto-analyze-threshold) (default 50) mutations. This setting ensures that small tables are not aggressively ANALYZED because the scale factor requirement is easily met.

Separately, auto analyze also considers cooldown settings for a table so as to not trigger ANALYZE aggressively. After every run of ANALYZE on a table, a cooldown period is enforced before the next run of ANALYZE on that table, even if the mutation thresholds are met. The cooldown period starts from ysql_auto_analyze_min_cooldown_per_table (default: 10 secs) and exponentially increases to ysql_auto_analyze_max_cooldown_per_table (default: 24 hrs). Cooldown settings do not reset - so in most cases, it is expected that a table that changes frequently only gets ANALYZE'd once every ysql_auto_analyze_max_cooldown_per_table (default: 24 hrs).

For more information on flags used to configure the Auto Analyze service, refer to [Auto Analyze service flags](../../reference/configuration/yb-tserver/#auto-analyze-service-flags).

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

## Limitations

ANALYZE is a technically considered a DDL statement (schema change) and normally conflicts with other [concurrent DDLs](../best-practices-operations/administration/#concurrent-ddl-during-a-ddl-operation). However, when run via the auto analyze service, ANALYZE can run concurrently with other DDL. In this case, ANALYZE is pre-empted by concurrent DDL and will be retried at a later point. However, when [transactional DDL](../explore/transactions/transactional-ddl/) is enabled (off by default), certain kinds of transactions that contain DDL may face a `kConflict` error when a background ANALYZE from the auto analyze service interrupts this transaction. In such cases, it is recommended to disable the auto analyze service explicitly and trigger ANAYZE manually.
