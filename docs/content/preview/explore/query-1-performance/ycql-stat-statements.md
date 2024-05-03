---
title: Get query statistics using ycql_stat_statements
linkTitle: Get query statistics
description: Track planning and execution statistics for all CQL statements executed by a server.
headerTitle: Get query statistics using ycql_stat_statements
menu:
  preview:
    identifier: ycql-stat-statements
    parent: query-tuning
    weight: 200
type: docs
---

{{<tabs>}}
{{<tabitem href="../pg-stat-statements/" text="YSQL" icon="postgres" >}}
{{<tabitem href="../ycql-stat-statements/" text="YCQL" icon="cassandra" active="true" >}}
{{</tabs>}}

Databases can be resource-intensive, consuming a lot of memory CPU, IO, and network resources. Optimizing your CQL can be very helpful in minimizing resource utilization. The `ycql_stat_statements` module helps you track planning and execution statistics for all the CQL statements executed by a server.

This view provides YCQL statement metrics (similar to pg_stat_statements) that can be joined with YCQL wait events in the [yb_active_session_history](../../observability/active-session-history/#yb-active-session-history) table.

This view can also be used to extract more information from the [Active Session History](../../observability/active-session-history/) (ASH) data.

The columns of the `ycql_stat_statements` view are described in the following table.

| Column | Type | Description |
| :----- | :--- | :---------- |
| queryid | int8 | Hash code to identify identical normalized queries. |
| query | text | Text of a representative statement. |
| is_prepared  | bool | Indicates whether the statement is a prepared statement or an unprepared query. |
| calls | int8 | Number of times the statement is executed.|
| total_time | float8 | Total time spent executing the statement, in milliseconds. |
| min_time | float8 | Minimum time spent executing the statement, in milliseconds. |
| max_time | float8 | Maximum time spent executing the statement, in milliseconds. |
| mean_time | float8 | Mean time spent executing the statement, in milliseconds. |
| stddev_time | float8 | Population standard deviation of time spent executing the statement, in milliseconds. |