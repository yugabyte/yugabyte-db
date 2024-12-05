---
title: auto analyze service
headerTitle: auto analyze service
linkTitle: auto analyze service
description: Using the auto analyze service in YugabyteDB
tags:
  feature: tech-preview
menu:
  preview:
    identifier: auto_analyze
    parent: explore
    weight: 20
type: docs
---

Having accurate and up-to-date statistics related to tables and their columns is important for a query planner to create the optimal plan for a given query.

Similar to PostgresSQL autovaccum feature, YugabyteDB has the auto analyze service to automate the execution of ANALYZE commands for any table with mutated rows more than the analyze threshold calculated for the table. This ensures table statistics are up-to-date.


## Enable auto analyze service

Auto analyze service in YugabyteDB is {{<tags/feature/tp>}}. Before you can use the feature, you must enable it by setting `ysql_enable_auto_analyze_service` to true on all YB-Masters and both `ysql_enable_auto_analyze_service` and `ysql_enable_table_mutation_counter` to true on all YB-Tservers.

For example, to create a single-node [yugabyted](../../../../reference/configuration/yugabyted/) cluster with auto analyze service enabled, use the following command:

```sh
./bin/yugabyted start --master_flags "ysql_enable_auto_analyze_service=true" --tserver_flags "ysql_enable_auto_analyze_service=true,ysql_enable_table_mutation_counter=true"
```

To enable auto analyze service on an existing cluster, a rolling restart is required for setting `ysql_enable_auto_analyze_service` and `ysql_enable_table_mutation_counter` to true.


## Configure auto analyze service statistics update frequency

Auto analyze service has a few configurable flags. Refer to [Auto analyze service flags](../../preview/reference/configuration/yb-tserver#auto-analyze-service-flags).

The most important configurable flags are: `ysql_auto_analyze_threshold` and `ysql_auto_analyze_scale_factor` which control how frequently auto analyze service updates table statistics. Increasing either of these two flags will reduce the frequency of auto analyze service statistics update.

If the total number of mutations (INSERT, UPDATE, and DELETE) for a table is greater than its analyze threshold, then auto analyze service will run ANALYZE on the table. The analyze threshold of a table is calculated as:

```
analyze_threshold = ysql_auto_analyze_threshold + ysql_auto_analyze_scale_factor * <table_size>
```

where `<table_size>` is the current `reltuples` column value stored in `pg_class` catalog.

The default value of `ysql_auto_analyze_threshold` is 50 and the default value of `ysql_auto_analyze_scale_factor` is 0.1.
`ysql_auto_analyze_threshold` is important for small tables. If a table has 100 rows and 20 are mutated, ANALYZE won’t run as `ysql_auto_analyze_threshold` is not met even though 20% of the rows are mutated.

On the other hand, `ysql_auto_analyze_scale_factor` is especially important for big tables. If a table has 1,000,000,000 rows, then 10% of that, or 100,000,000 rows, would have to be mutated before ANALYZE runs. `ysql_auto_analyze_scale_factor` needs to be set to a lower value to allow for more frequent statistics collections for such large tables.

In addition, `ysql_auto_analyze_batch_size` controls the the max number of tables the auto analyze service tries to analyze in a single ANALYZE statement. Its default value is 10. Setting this flag to a larger value can potentially reduce the number of YSQL catalog cache refresh if auto analyze service decides to ANALYZE many tables within the same database at the same time.


## Example
With auto analyze service enabled, try the following SQL statements.
```sql
CREATE TABLE test (k INT PRIMARY KEY, v INT);
SELECT reltuples FROM pg_class WHERE relname = 'test';
 reltuples 
-----------
        -1
(1 row)
INSERT INTO test SELECT i, i FROM generate_series(1, 100) i;
-- Wait for few seconds
SELECT reltuples FROM pg_class WHERE relname = 'test';
 reltuples
-----------
       100
(1 row)
```

## Limitations
ANALYZE command is a DDL statment. It can cause DDL conflicts when running concurrently with other DDL statements.
Since auto analyze service runs ANALYZEs in background, turn off auto analyze service if you want to execute DDL statements.
Setting `ysql_enable_auto_analyze_service` to false on all YB-Tservers at runtime is sufficient to turn off auto analyze service.
