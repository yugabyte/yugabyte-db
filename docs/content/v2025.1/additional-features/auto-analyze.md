---
title: Auto Analyze service
headerTitle: Auto Analyze service
linkTitle: Auto Analyze
description: Use the Auto Analyze service to keep table statistics up to date
headcontent: Keep table statistics up to date automatically
tags:
  feature: early-access
menu:
  v2025.1:
    identifier: auto-analyze
    parent: additional-features
    weight: 100
type: docs
---

To create optimal plans for queries, the query planner needs accurate and up-to-date statistics related to tables and their columns. These statistics are also used by the YugabyteDB [cost-based optimizer](../../reference/configuration/yb-tserver/#yb-enable-base-scans-cost-model) (CBO) to create optimal execution plans for queries. To generate the statistics, you run the [ANALYZE](../../api/ysql/the-sql-language/statements/cmd_analyze/) command. ANALYZE collects statistics about the contents of tables in the database, and stores the results in the `pg_statistic` system catalog.

Similar to [PostgreSQL autovacuum](https://www.postgresql.org/docs/current/routine-vacuuming.html#AUTOVACUUM), the YugabyteDB Auto Analyze service automates the execution of ANALYZE commands for any table where rows have changed more than a configurable threshold for the table. This ensures table statistics are always up-to-date.

## Enable Auto Analyze

Before you can use the feature, you must enable it by setting `ysql_enable_auto_analyze_service` to true on all YB-Masters, and both `ysql_enable_auto_analyze_service` and `ysql_enable_table_mutation_counter` to true on all YB-TServers.

For example, to create a single-node [yugabyted](../../reference/configuration/yugabyted/) cluster with Auto Analyze enabled, use the following command:

```sh
./bin/yugabyted start \
    --master_flags "ysql_enable_auto_analyze_service=true" \
    --tserver_flags "ysql_enable_auto_analyze_service=true,ysql_enable_table_mutation_counter=true"
```

Enabling Auto Analyze on an existing cluster requires a rolling restart to set `ysql_enable_auto_analyze_service` and `ysql_enable_table_mutation_counter` to true.

## Configure Auto Analyze

You can control how frequently the service updates table statistics using the following YB-TServer flags:

- `ysql_auto_analyze_threshold` - the minimum number of mutations (INSERT, UPDATE, and DELETE) needed to run ANALYZE on a table. Default is 50.
- `ysql_auto_analyze_scale_factor` - a fraction that determines when enough mutations have been accumulated to run ANALYZE for a table. Default is 0.1.

Increasing either of these flags reduces the frequency of statistics updates.

If the total number of mutations for a table is greater than its analyze threshold, then the service runs ANALYZE on the table. The analyze threshold of a table is calculated as follows:

```sh
analyze_threshold = ysql_auto_analyze_threshold + (ysql_auto_analyze_scale_factor * <table_size>)
```

where `<table_size>` is the current `reltuples` column value stored in the `pg_class` catalog.

`ysql_auto_analyze_threshold` is important for small tables. With default settings, if a table has 100 rows and 20 are mutated, ANALYZE won't run as the threshold is not met, even though 20% of the rows are mutated.

On the other hand, `ysql_auto_analyze_scale_factor` is especially important for big tables. If a table has 1,000,000,000 rows, 10% (100,000,000 rows) would have to be mutated before ANALYZE runs. Set the scale factor to a lower value to allow for more frequent statistics collection for such large tables.

In addition, `ysql_auto_analyze_batch_size` controls the maximum number of tables the Auto Analyze service tries to analyze in a single ANALYZE statement. The default is 10. Setting this flag to a larger value can potentially reduce the number of YSQL catalog cache refreshes if Auto Analyze decides to ANALYZE many tables in the same database at the same time.

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

Because ANALYZE is a DDL statement, it can cause DDL conflicts when run concurrently with other DDL statements. As Auto Analyze runs ANALYZE in the background, you should turn off Auto Analyze if you want to execute DDL statements. You can do this by setting `ysql_enable_auto_analyze_service` to false on all YB-TServers at runtime.
