---
title: Query diagnostics
linkTitle: Query diagnostics
headerTitle: Query diagnostics
headcontent: Export diagnostic information for analysis
tags:
  feature: tech-preview
menu:
  preview:
    identifier: query-diagnostics
    parent: query-tuning
    weight: 500
type: docs
---

Use query diagnostics to troubleshoot and analyse database query performance issues. You can capture and export detailed diagnostic information across multiple dimensions, and use this information to identify and resolve database query problems.

You can gather the following information about a particular query, while the query is in progress, over a defined interval of time:

- Bind variables and constants
- Explain plans
- PostgreSQL statement statistics
- Active Session History
- Schema details

The information is exported to the file system.

## Enable query diagnostics

Query diagnostics is {{<tags/feature/tp>}}. To use query diagnostics, enable and configure the following flags for each node of your cluster.

| Flag | Description |
| :--- | :---------- |
| allowed_preview_flags_csv | Set the value of this flag to include `ysql_yb_enable_query_diagnostics`. |
| ysql_yb_enable_query_diagnostics | Enable or disable query diagnostics. <br>Default: false. Changing this flag requires a VM restart. |
| yb_query_diagnostics_circular_buffer_size | Size (in KB) of query diagnostics circular buffer that stores statuses of bundles.<br>Default: 64. Changing this flag requires a VM restart. |

## Export diagnostics

To initiate query diagnostics, you use the `yb_query_diagnostics()` function.

For example:

```sql
SELECT yb_query_diagnostics(
    query_id => 7317833532428971166,
    explain_sample_rate => 5,
    diagnostics_interval_sec => 120,
    explain_analyze => true,
    explain_dist => true,
    bind_var_query_min_duration_ms => 10
    );
```

```output
yb_query_diagnostics
/Users/($username)/yugabyte-data/node-1/disk-1/query-diagnostics/7317833532428971166/<random-number>
(1 row)
--------------------------------------------------------------------------------
```

This exports diagnostics for the query with the specified ID, for a duration of 120 seconds, and includes explain plans for 5% of all queries observed during this timeframe. It also dumps constants/bind variables for queries that took more than 10ms.

To cancel a running query diagnostic, use the `yb_cancel_query_diagnostics()` function.

For example:

```sql
SELECT yb_cancel_query_diagnostics(query_id => 7317833532428971166);
```

### Options

| Parameter | Description | Default |
| :--- | :--- | :--- |
| query_id | Required. ID of the query to export diagnostic information for. | |
| diagnostics_interval_sec | The duration for which the bundle will run, in seconds. | 300 |
| explain_sample_rate | You can export the output of the EXPLAIN command for a randomly selected percentage of queries that are running during the diagnostics interval. | 1 |
| explain_analyze | Enhance the EXPLAIN plan output with planning and execution data. Note that this data is gathered during query execution itself, and the query is not re-executed. | false |
| explain_dist | Log distributed data in the EXPLAIN plan. explain_analyze must be set to true. | false |
| explain_debug | Log debug information to explain the plan. | false |
| bind_var_query_min_duration_ms | The minimum query duration for logging bind variables, in milliseconds. Set to -1 to turn bind variable logging off. | 10 |

### Check diagnostic status

Use the `yb_query_diagnostics_status` view to check the status of previously executed query diagnostic bundles.

| Column | Description |
| :----- | :---------- |
| query_id | Hash code to identify identical normalized queries. |
| start_time | Timestamp when the bundle was triggered. |
| diagnostics_interval_sec | The duration for which the bundle will run. |
| explain_params | Flags used to gather EXPLAIN plans. |
| bind_var_query_min_duration_ms | The minimum query duration for logging bind variables. |
| folder_path | File system path where the data for this bundle has been dumped. |
| state | The state of the diagnostic - Completed, In Progress, or Failed. |
| status | In case of a failure, a short description of the type of failure. |

## Output

Query diagnostics outputs the following information.

### Constants

Provides details of the executed bind variables when the supplied query ID corresponds to a prepared statement. Otherwise, provides the constants embedded in the query. Additionally, it includes the time of each execution along with the bind variable. Using the `bind_var_query_min_duration_ms` parameter, you can log only those bind_variables that take more than a specified duration.

Output: pg_data/query_diagnostics/query_id/<random-number>/constants_and_bind_variables.csv

For example:

| Query_id | query_time | var1 | var2 | var3 |
| 7317833532428971166 | 12.323 | 19146 | 9008 | 'text_1’ |

### pg_stat_statements

Presents aggregated PostgreSQL statement statistics (pg_stat_statements) data spanning the diagnostics interval of query diagnostics for the specified query ID.

Output: pg_data/query_diagnostics/query_id/<random-number>/constants_and_bind_variables.csv

For more information, see [Get query statistics using pg_stat_statements](../pg-stat-statements/).

### Schema information

Provides information about the columns and indexes associated with unique tables mentioned in the query being diagnosed.

Output: pg_data/query_diagnostics/query_id/<random-number>/schema_details.txt

For example, the following query would generate schema details as follows:

```sql
SELECT 
    e.employee_id,
    e.name AS employee_name,
    e.department_id,
    d.department_name
FROM 
    employees e
INNER JOIN 
    departments d ON e.department_id = d.department_id;
```

```output
- Table: employees
    - Columns:
        - employee_id (Type: integer, Primary Key)
        - name (Type: character varying)
        - department_id (Type: integer, Foreign Key: references departments(department_id))
        - email (Type: character varying, Unique Key)
    - Index: employees_pkey (Primary Key)

- Table: departments
    - Columns:
        - department_id (Type: integer, Primary Key)
        - department_name (Type: character varying)
    - Index: departments_pkey (Primary Key)
```

### Active session history

Outputs the data from the `yb_active_session_history` view, for the diagnostics interval. The table can later be imported in SQL and queried to get data for only the specified query ID.

Output: pg_data/query_diagnostics/query_id/<random-number>/active_session_history.csv

For more information on, see [Active Session History](../../observability/active-session-history/).

### Explain plans

Provides the output of the EXPLAIN command for a randomly selected subset of queries.

Output: pg_data/query_diagnostics/query_id/<random-number>/explain_plan.txt

For example:

```output
QUERY PLAN:
--------------------------------------------------------------------------------
Insert on n  (cost=0.00..0.01 rows=1 width=44) (actual time=9.114..9.114 rows=0 loops=1)
   ->  Result  (cost=0.00..0.01 rows=1 width=44) (actual time=0.003..0.005 rows=1 loops=1)
         Storage Table Write Requests: 1
 Planning Time: 0.142 ms
 Execution Time: 9.294 ms
 Storage Read Requests: 0
 Storage Rows Scanned: 0
 Storage Write Requests: 1
 Catalog Read Requests: 0
 Catalog Write Requests: 0
 Storage Flush Requests: 0
 Storage Execution Time: 0.000 ms
 Peak Memory Usage: 24 kB
(13 rows)
--------------------------------------------------------------------------------
Insert on n  (cost=0.00..0.01 rows=1 width=44) (actual time=9.114..9.114 rows=0 loops=1)
   ->  Result  (cost=0.00..0.01 rows=1 width=44) (actual time=0.003..0.005 rows=1 loops=1)
         Storage Table Write Requests: 1
 Planning Time: 0.142 ms
 Execution Time: 9.294 ms
 Storage Read Requests: 0
 Storage Rows Scanned: 0
 Storage Write Requests: 1
 Catalog Read Requests: 0
 Catalog Write Requests: 0
 Storage Flush Requests: 0
 Storage Execution Time: 0.000 ms
 Peak Memory Usage: 24 kB
(13 rows)
...
--------------------------------------------------------------------------------
```

For more information about using EXPLAIN, see [Analyze queries with EXPLAIN](../explain-analyze/).

## Limitations

- Query diagnostics collects data such as explain plans, bind variables, pg_stat_statements, schema details, and Active Session History, over the duration of the diagnostics interval. It also manages in-flight requests during this interval, continuously transferring the collected data to the file system for storage and future analysis. Multiple diagnostic bundles can be triggered concurrently for different queries; however, only one bundle can run at a time for a single query ID.
- Query diagnostics is available per node and is not aggregated across the cluster.
- Schema details can export a maximum 10 unique schemas in the query.
- You cannot have more than 100 consecutive query diagnostics bundles.
- The feature is designed to minimize its impact on query latency. However, operations like EXPLAIN ANALYZE and I/O tasks may affect the data being analyzed.
- Memory consumption may increase as data is stored in memory before being flushed to disk. Memory consumption while accumulating data is as follows:

    ```output
    num_queries_diagnosed * (mem_explain + mem_bind_var + mem_schema_details + mem_ash + mem_pg_stat_statements)
    ```

    Where

    - num_queries_diagnosed ≤ 100
    - mem_explain ≤ 16384
    - mem_bind_var ≤ 2048
    - mem_pg_stat_statements ≤ 1024

- Folders generated by query diagnostics are not cleaned up automatically.
- If any query has more than 16384 chars in explain_plan, then it is not dumped. See [issue 23720](https://github.com/yugabyte/yugabyte-db/issues/23720).

## Learn more

- [Cost Based Optimizer](https://www.yugabyte.com/blog/yugabytedb-cost-based-optimizer/)
