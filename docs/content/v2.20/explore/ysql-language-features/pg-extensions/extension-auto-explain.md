---
title: auto_explain extension
headerTitle: auto_explain extension
linkTitle: auto_explain
description: Using the auto_explain extension in YugabyteDB
menu:
  v2.20:
    identifier: extension-auto-explain
    parent: pg-extensions
    weight: 20
type: docs
---

The [auto_explain](https://www.postgresql.org/docs/11/auto-explain.html) PostgreSQL module provides a means for logging execution plans of slow statements automatically, without having to run EXPLAIN by hand. This is especially helpful for tracking down un-optimized queries in large applications.

## Enable auto_explain

To enable the auto_explain extension, add `auto_explain` to `shared_preload_libraries` in the PostgreSQL server configuration parameters using the YB-TServer [--ysql_pg_conf_csv](../../../../reference/configuration/yb-tserver/#ysql-pg-conf-csv) flag:

```sh
--ysql_pg_conf_csv="shared_preload_libraries=auto_explain"
```

Note that modifying `shared_preload_libraries` requires restarting the YB-TServer.

## Customize auto_explain

You can customize the following auto_explain parameters:

| Parameter | Description | Default |
| :--- | :--- | :--- |
| `log_min_duration` | Minimum statement execution time, in milliseconds, that will cause the statement's plan to be logged. Setting this to zero logs all plans. Minus-one (the default) disables logging. For example, if you set it to 250ms then all statements that run 250ms or longer will be logged. Only superusers can change this setting. | -1 |
| `log_analyze` | Print EXPLAIN ANALYZE output, rather than just EXPLAIN output when an execution plan is logged. When on, per-plan-node timing occurs for all statements executed, whether or not they run long enough to actually get logged. This can have an extremely negative impact on performance. Turning off `log_timing` ameliorates the performance cost, at the price of obtaining less information. Only superusers can change this setting. | false |
| `log_buffers` | Print buffer usage statistics when an execution plan is logged; equivalent to the BUFFERS option of EXPLAIN. Has no effect unless `log_analyze` is enabled. Only superusers can change this setting. | false |
| `log_timing` | Print per-node timing information when an execution plan is logged; equivalent to the TIMING option of EXPLAIN. The overhead of repeatedly reading the system clock can slow down queries significantly on some systems, so it may be beneficial to set this parameter to off when only actual row counts, and not exact times, are needed. Has no effect unless `log_analyze` is enabled. Only superusers can change this setting. | true |
| `log_triggers` | Include trigger execution statistics when an execution plan is logged. Has no effect unless `log_analyze` is enabled. Only superusers can change this setting. | false |
| `log_verbose` | Print verbose details when an execution plan is logged; equivalent to the VERBOSE option of EXPLAIN. Only superusers can change this setting. | false |
| `log_format` | The format of the EXPLAIN output. Allowed values are `text`, `xml`, `json`, and `yaml`. Only superusers can change this setting. | text |
| `log_nested_statements` | Consider nested statements (statements executed inside a function) for logging. When off, only top-level query plans are logged. Only superusers can change this setting. | false |
| `sample_rate` | Explain only a set fraction of the statements in each session. The default 1 means explain all the queries. In case of nested statements, either all will be explained or none. Only superusers can change this setting. | 1 |

Note that the default behavior is to do nothing, so you must set at least `auto_explain.log_min_duration` if you want any results.

## Example

To change auto_explain parameters, use the SET statement. For example:

```sql
SET auto_explain.log_min_duration = 0;
SET auto_explain.log_analyze = true;
SELECT count(*)
    FROM pg_class, pg_index
    WHERE oid = indrelid AND indisunique;
```

This produces log output similar to the following in the PostgreSQL log file in the `tserver/logs` directory:

```output
LOG:  duration: 316.556 ms  plan:
        Query Text: SELECT count(*)
                   FROM pg_class, pg_index
                   WHERE oid = indrelid AND indisunique;
        Aggregate  (cost=216.39..216.40 rows=1 width=8) (actual time=316.489..316.489 rows=1 loops=1)
          ->  Nested Loop  (cost=0.00..213.89 rows=1000 width=0) (actual time=10.828..316.200 rows=110 loops=1)
                ->  Seq Scan on pg_index  (cost=0.00..100.00 rows=1000 width=4) (actual time=7.465..8.068 rows=110 loops=1)
                      Remote Filter: indisunique
                ->  Index Scan using pg_class_oid_index on pg_class  (cost=0.00..0.11 rows=1 width=4) (actual time=2.673..2.673 rows=1 loops=110)
                      Index Cond: (oid = pg_index.indrelid)
```
