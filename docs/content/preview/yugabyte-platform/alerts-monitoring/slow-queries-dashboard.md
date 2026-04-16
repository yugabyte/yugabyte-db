---
title: Slow queries in YugabyteDB Anywhere
headerTitle: Slow Queries dashboard
linkTitle: Slow Queries dashboard
description: Slow Queries dashboard
headcontent: View slow running queries that have run on your universe
menu:
  preview_yugabyte-platform:
    parent: alerts-monitoring
    identifier: slow-queries-dashboard
    weight: 30
type: docs
---

Use the **Slow Queries** dashboard to analyze statistics of past queries on your YugabyteDB universes. You can use this data to do the following:

- Visually identify slower running database operations.
- Evaluate query execution times over time.
- Discover potential queries for memory optimization.

All user roles can use the **Slow Queries** dashboard.

Slow queries are displayed on the universe **Queries** tab. The top slow queries are also displayed on the **Overview** tab.

Note that slow queries are not available for YCQL.

## View Slow Queries

To access slow queries for a universe, do the following:

1. Navigate to **Universes**, select your universe, then select **Queries > Slow Queries**.

    Or, on the **Overview** tab, select **Top SQL Statements**. You can also access slow queries from each node in the **Nodes** page by clicking its **Actions** button.

1. If query monitoring is not enabled, select the **Query Monitoring** option to turn it on.

    Use the **Column Display** options to display specific fields.

    ![Slow Queries](/images/yp/alerts-monitoring/slow-queries/selecting-columns.png)

The following table describes the **Slow Queries** column values.

| Column | Description |
| :----- | :---------- |
| Query | The query command.<br>For example, `select * from my_keyspace.my_table`. |
| Database | The YSQL database used by the query. |
| User | The name of role used to access YSQL database. |
| Count/ Total count | The total number of times this type of query has executed. |
| Total time | The total duration (in milliseconds) this query has taken. |
| Rows | The total number of database table rows returned across all iterations of this query. |
| Avg Exec Time | Average or mean execution time (in milliseconds) for this query. |
| Min Exec Time | Minimum execution time (in milliseconds) for this query. |
| Max Exec Time | Maximum execution time (in milliseconds) for this query. |
| Std Dev Time | Standard deviation of execution times for this query. |
| Temp Tables RAM | Memory used by temporary tables generated from query. |
| P25 Latency | Latency with its 25th percentile. |
| P50 Latency | Latency with its 50th percentile. |
| P90 Latency | Latency with its 90th percentile. |
| P95 Latency | Latency with its 95th percentile. |
| P99 Latency | Latency with its 99th percentile. |

### Filter queries

You can filter the list of queries by entering terms in the **Filter by query text** field.

![Filter slow queries](/images/yp/alerts-monitoring/slow-queries/search-dropdown-options.png)

Use filtering for comparisons on numbers columns (`Avg Time`) using `>`, `>=`, `<`, and `<=` to search for values that are greater than, greater than or equal to, less than, and less than or equal to another value. For example, `Avg Time: < 30`.

You can also use the range syntax `n..m` to search for values in a range, where the first number `n` is the lowest value and the second number `m` is the highest value. The range syntax supports tokens like the following: `n..*` which is equivalent to `>= n`. Or `*..n` which is the same as `<= n`.

### View query details

Select a row to display the **Query Details** sheet, with a full view of the query statement, along with all the column data, including the Response Time Percentile and [Latency histogram](../../../yugabyte-platform/alerts-monitoring/latency-histogram/).

![View query statement](/images/yp/alerts-monitoring/slow-queries/query-info-panel.png)

![View query details](/images/yp/alerts-monitoring/slow-queries/query-details-panel.png)
