---
title: pg_stat_statements extension
headerTitle: pg_stat_statements extension
linkTitle: pg_stat_statements
description: Using the pg_stat_statements extension in YugabyteDB
menu:
  preview:
    identifier: extension-pgstatstatements
    parent: pg-extensions
    weight: 20
type: docs
---

The [pg_stat_statements](https://www.postgresql.org/docs/11/pgstatstatements.html) module provides a means for tracking execution statistics of all SQL statements executed by a server.

```sql
CREATE EXTENSION pg_stat_statements;

SELECT query, calls, total_time, min_time, max_time, mean_time, stddev_time, rows FROM pg_stat_statements;
```

To get the output of `pg_stat_statements` in JSON format, visit `https://<yb-tserver-ip>:13000/statements` in your web browser, where `<yb-tserver-ip>` is the IP address of any YB-TServer node in your cluster.

For more information on using pg_stat_statements in YugabyteDB, refer to [Get query statistics using pg_stat_statements](../../../query-1-performance/pg-stat-statements/).
