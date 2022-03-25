# User Guide

* [Introduction](#introduction)
* [Features](#features)
* [Views](#views)
* [Functions](#functions)
* [Configuration](#configuration)
* [Usage examples](#usage-examples)

## Introduction

This document describes the features, functions and configuration of the ``pg_stat_monitor`` extension and gives some usage examples. For how to install and set up ``pg_stat_monitor``, see [Installation in README](https://github.com/percona/pg_stat_monitor/blob/master/README.md#installation).

## Features

The following are the key features of pg_stat_monitor:

* [Time buckets](#time-buckets),
* [Table and index access statistics per statement](#table-and-index-access-statistics-per-statement),
* Query statistics:
   * [Query and client information](#query-and-client-information),
   * [Query timing information](#query-timing-information),
   * [Query execution plan information](#query-execution-plan-information),
   * [Use of actual data or parameters placeholders in queries](#use-of-actual-data-or-parameters-placeholders-in-queries),
   * [Query type filtering](#query-type-filtering),
   * [Query metadata supporting Google’s Sqlcommentor](#query-metadata),
* [Top query tracking](#top-query-tracking),
* [Relations](#relations) - showing tables involved in a query,
* [Monitoring of queries terminated with ERROR, WARNING and LOG error levels](#monitoring-of-queries-terminated-with-error-warning-and-log-error-levels),
* [Integration with Percona Monitoring and Management (PMM) tool](#integration-with-pmm),
* [Histograms](#histogram) - visual representation of query performance.


### Time buckets

Instead of supplying one set of ever-increasing counts, `pg_stat_monitor` computes stats for a configured number of time intervals; time buckets. This allows for much better data accuracy, especially in the case of high-resolution or unreliable networks. 

### Table and index access statistics per statement

`pg_stat_monitor` collects the information about what tables were accessed by a statement. This allows you to identify all queries which access a given table easily.


### Query and client information

`pg_stat_monitor` provides  additional metrics for detailed analysis of query performance from various perspectives, including client connection details like user name, application name, IP address to name a few relevant columns.
With this information, `pg_stat_monitor` enables users to track a query to the originating application. More details about the application or query may be incorporated in the SQL query in a [Google’s Sqlcommenter](https://google.github.io/sqlcommenter/) format.

To see how it works, refer to the [usage example](#query-information)

### Query timing information

Understanding query execution time stats helps you identify what affects query performance and take measures to optimize it. `pg_stat_monitor` collects the total, min, max and average (mean) time it took to execute a particular query and provides this data in separate columns. See the [Query timing information](#query-timing-information-1) example for the sample output.


### Query execution plan information

Every query has a plan that was constructed for its executing. Collecting the query plan information as well as monitoring query plan timing helps you understand how you can modify the query to optimize its execution. It also helps make communication about the query clearer when discussing query performance with other DBAs and application developers.

See the [Query execution plan](##query-execution-plan) example for the sample output.

### Use of actual data or parameters placeholders in queries

You can select whether to see queries with parameters placeholders or actual query data. The benefit of having the full query example is in being able to run the [EXPLAIN](https://www.postgresql.org/docs/current/sql-explain.html) command on it to see how its execution was planned. As a result, you can modify the query to make it run better.

### Query type filtering

`pg_stat_monitor` monitors queries per type (``SELECT``, `INSERT`, `UPDATE` or `DELETE`) and classifies them accordingly in the `cmd_type` column. This way you can separate the queries you are interested in and focus on identifying the issues and optimizing query performance.

See the [Query type filtering example](#query-type-filtering-1) for the sample output.

### Query metadata

Google’s Sqlcommenter is a useful tool that in a way bridges that gap between ORM libraries and understanding database performance. And ``pg_stat_monitor`` supports it. So, you can now put any key-value data (like what client executed a query or if it is testing vs production query) in the comments in `/* … */` syntax in your SQL statements, and the information will be parsed by `pg_stat_monitor` and made available in the comments column in the `pg_stat_monitor` view. For details on the comments’ syntax, see [Sqlcommenter documentation](https://google.github.io/sqlcommenter/).

To see how it works, see the [Query metadata](#query-metadata-1) example.

### Top query tracking

Using functions is common. While running, functions can execute queries internally. `pg_stat_monitor` not only keeps track of all executed queries within a function, but also marks that function as top query.

Top query indicates the main query. To illustrate, for the SELECT query that is invoked within a function, the top query is calling this function.

This enables you to backtrack to the originating function and thus simplifies the tracking and analysis.

Find more details in the [usage example](#top-query-tracking-1).

### Relations

`pg_stat_monitor` provides the list of tables involved in the query in the relations column. This reduces time on identifying the tables and simplifies the analysis. To learn more, see the [usage examples](#relations-1)

### Monitoring queries terminated with ERROR, WARNING and LOG error levels

Monitoring queries that terminate with ERROR, WARNING, LOG states can give useful information to debug an issue. Such messages have the error level (`elevel`), error code (`sqlcode`), and error message (`message`).  `pg_stat_monitor` collects all this information and aggregates it so that you can measure performance for successful and failed queries separately, as well as understand why a particular query failed to execute successfully.

Find details in the [usage example](#queries-terminated-with-errors)

### Integration with PMM

To timely identify and react on issues, performance should be automated and alerts should be sent when an issue occurs. There are many monitoring tools available for PostgreSQL, some of them (like Nagios) supporting custom metrics provided via extensions. Though you can integrate `pg_stat_monitor` with these tools, it natively supports integration with Percona Management and Monitoring (PMM). This integration allows you to enjoy all the features provided by both solutions: advanced statistics data provided by `pg_stat_monitor` and automated monitoring with data visualization on dashboards, security threat checks and alerting, available in PMM out of the box.

To learn how to integrate pg_stat_monitor with PMM, see [Configure pg_stat_monitor in PMM](https://www.percona.com/doc/percona-monitoring-and-management/2.x/setting-up/client/postgresql.html#pg_stat_monitor)

### Histogram

Histogram (the `resp_calls` parameter) provides a visual representation of query performance. With the help of the histogram function, you can view a timing/calling data histogram in response to an SQL query.

Learn more about using histograms from the [usage example](#histogram-1).

## Views

`pg_stat_monitor` provides the following views:
* `pg_stat_monitor` is the view where statistics data is presented.
* `pg_stat_monitor_settings` view shows available configuration options which you can change.

### `pg_stat_monitor` view

The statistics gathered by the module are made available via the view named `pg_stat_monitor`. This view contains one row for each distinct combination of metrics and whether it is a top-level statement or not (up to the maximum number of distinct statements that the module can track). For details about available counters, refer to the [`pg_stat_monitor` view reference](https://github.com/percona/pg_stat_monitor/blob/master/docs/REFERENCE.md).

The following are the primary keys for pg_stat_monitor:

* `bucket`,
* `userid`,
* `dbid`,
* `client_ip`,
* `application_name`.

A new row is created for each key in the `pg_stat_monitor` view.

`pg_stat_monitor` inherits the metrics available in `pg_stat_statements`, plus provides additional ones. See the [`pg_stat_monitor` vs `pg_stat_statements` comparison](https://github.com/percona/pg_stat_monitor/blob/master/docs/REFERENCE.md) for details.

For security reasons, only superusers and members of the `pg_read_all_stats` role are allowed to see the SQL text and `queryid` of queries executed by other users. Other users can see the statistics, however, if the view has been installed in their database.

### pg_stat_monitor_settings view

The `pg_stat_monitor_settings` view shows one row per `pg_stat_monitor` configuration parameter. It displays configuration parameter name, value, default value, description, minimum and maximum values, and whether a restart is required for a change in value to be effective.

## Functions

### pg_stat_monitor_reset()

This function resets all the statistics and clears the view. Eventually, the function will delete all the previous data.

### pg_stat_monitor_version()
This function provides the build version of `pg_stat_monitor` version.

```
postgres=# select pg_stat_monitor_version();
 pg_stat_monitor_version
-------------------------
 devel
(1 row)
```

### histogram(bucket id, query id)

It is used to generate the histogram, you can refer to histogram sections.

## Configuration

Use the following command to view available configuration parameters in the `pg_stat_monitor_settings` view:

```sql
SELECT * FROM pg_stat_monitor_settings;
```

To amend the `pg_stat_monitor` configuration, use the General Configuration Unit (GCU) system. Some configuration parameters require the server restart and should be set before the server startup. These must be set in the `postgresql.conf` file. Other parameters do not require server restart and can be set permanently either in the `postgresql.conf` or from the client (`psql`) using the SET or ALTER SYSTEM SET commands.

The following table shows setup options for each configuration parameter and whether the server restart is required to apply the parameter's value:

| Parameter Name                                |  postgresql.conf   | SET | ALTER SYSTEM SET  |  server restart   | configuration reload
| ----------------------------------------------|--------------------|-----|-------------------|-------------------|---------------------
| [pg_stat_monitor.pgsm_max](#pg_stat_monitorpgsm_max) | :heavy_check_mark: | :x:                |:x:                |:heavy_check_mark: | :x:
| [pg_stat_monitor.pgsm_query_max_len](#pg_stat_monitorpgsm_query_max_len)            | :heavy_check_mark: | :x:                |:x:                |:heavy_check_mark: | :x:
| [pg_stat_monitor.pgsm_track_utility](#pg_stat_monitorpgsm_track_utility)            | :heavy_check_mark: | :heavy_check_mark: |:heavy_check_mark: |:x: | :heavy_check_mark:
| [pg_stat_monitor.pgsm_normalized_query](#pg_stat_monitorpgsm_normalized_query)         | :heavy_check_mark: | :heavy_check_mark: |:heavy_check_mark: |:x: | :heavy_check_mark:
| [pg_stat_monitor.pgsm_max_buckets](#pg_stat_monitorpgsm_max_buckets)              | :heavy_check_mark: | :x:                |:x:                |:heavy_check_mark: | :heavy_check_mark:
| [pg_stat_monitor.pgsm_bucket_time](#pg_stat_monitorpgsm_bucket_time)              | :heavy_check_mark: | :x:                |:x:                |:heavy_check_mark: | :x:
| [pg_stat_monitor.pgsm_histogram_min](#pg_stat_monitorpgsm_histogram_min) | :heavy_check_mark: | :x:                |:x:                |:heavy_check_mark: | :x:
| [pg_stat_monitor.pgsm_histogram_max](#pg_stat_monitorpgsm_histogram_max)        | :heavy_check_mark: | :x:                |:x:                |:heavy_check_mark: | :x:
| [pg_stat_monitor.pgsm_histogram_buckets](#pg_stat_monitorpgsm_histogram_buckets)   | :heavy_check_mark: | :x:                |:x:                |:heavy_check_mark: | :x:
| [pg_stat_monitor.pgsm_query_shared_buffer](#pg_stat_monitorpgsm_query_shared_buffer)      | :heavy_check_mark: | :x:                |:x:                |:heavy_check_mark: | :x:
| [pg_stat_monitor.pgsm_overflow_target](#pg_stat_monitorpgsm_overflow_target) | :heavy_check_mark:   |  :x:  |  :x:  |   :heavy_check_mark: |  :x:  |
| [pg_stat_monitor.pgsm_enable_query_plan](#pg_stat_monitorpgsm_enable_query_plan)  | :heavy_check_mark:   |  :x:  |  :x:  |   :heavy_check_mark: |  :x:  |
| [pg_stat_monitor.track](#pg_stat_monitortrack) | :heavy_check_mark:   |  :x:  |  :x:  |   :x:  | :heavy_check_mark: |
| [pg_stat_monitor.extract_comments](#pg_stat_monitorextract_comments)| :heavy_check_mark:   |  :x:  |  :x:  |   :x:  | :heavy_check_mark: |
| [pg_stat_monitor.pgsm_track_planning](#pg_stat_monitorpgsm_track_planning) | :heavy_check_mark:   |  :x:  |  :x:  |   :heavy_check_mark: |  :x:  |

#### Parameters description:


##### pg_stat_monitor.pgsm_max

Values:
- Min: 1
- Max: 1000
- Default: 100


This parameter defines the limit of shared memory (in MB) for ``pg_stat_monitor``. This memory is used by buckets in a circular manner. The memory is divided between the buckets equally, at the start of the PostgreSQL. Requires the server restart.

##### pg_stat_monitor.pgsm_query_max_len

Values:
- Min: 1024
- Max: 2147483647
- Default: 1024

Sets the maximum size of the query. This parameter can only be set at the start of PostgreSQL. For long queries, the query is truncated to that particular length. This is to avoid unnecessary usage of shared memory. Requires the server restart.


##### pg_stat_monitor.pgsm_track_utility

Type: boolean. Default: 1

This parameter controls whether utility commands are tracked by the module. Utility commands are all those other than ``SELECT``, ``INSERT``, ``UPDATE``, and ``DELETE``.

##### pg_stat_monitor.pgsm_normalized_query

Type: boolean. Default: 1

By default, the query shows the actual parameter instead of the placeholder. It is quite useful when users want to use that query and try to run that query to check the abnormalities. But in most cases users like the queries with a placeholder. This parameter is used to toggle between the two said options.

##### pg_stat_monitor.pgsm_max_buckets

Values:
- Min: 1
- Max: 10
- Default: 10

``pg_stat_monitor`` accumulates the information in the form of buckets. All the aggregated information is bucket based. This parameter is used to set the number of buckets the system can have. For example, if this parameter is set to 2, then the system will create two buckets. First, the system will add all the information into the first bucket. After its lifetime (defined in the [pg_stat_monitor.pgsm_bucket_time](#pg-stat-monitorpgsm-bucket-time) parameter) expires, it will switch to the second bucket, reset all the counters and repeat the process.

Requires the server restart.

##### pg_stat_monitor.pgsm_bucket_time

Values:
- Min: 1
- Max: 2147483647
- Default: 60

This parameter is used to set the lifetime of the bucket. System switches between buckets on the basis of [pg_stat_monitor.pgsm_bucket_time](#pg-stat-monitorpgsm-bucket-time).

Requires the server restart.

##### pg_stat_monitor.pgsm_histogram_min

Values:
- Min: 0
- Max: 2147483647
- Default: 0

``pg_stat_monitor`` also stores the execution time histogram. This parameter is used to set the lower bound of the histogram (in ms).

Requires the server restart.

##### pg_stat_monitor.pgsm_histogram_max

Values:
- Min: 10
- Max: 2147483647
- Default: 100000

This parameter sets the upper bound of the execution time histogram (in ms). Requires the server restart.

##### pg_stat_monitor.pgsm_histogram_buckets

Values:
- Min: 2
- Max: 2147483647
- Default: 10

This parameter sets the maximum number of histogram buckets. Requires the server restart.

##### pg_stat_monitor.pgsm_query_shared_buffer

Values:
- Min: 1
- Max: 10000
- Default: 20

This parameter defines the shared memory limit (in MB) allocated for a query tracked by ``pg_stat_monitor``. Requires the server restart.

##### pg_stat_monitor.pgsm_overflow_target

Type: boolean. Default: 1

Sets the overflow target for the `pg_stat_monitor`. Requires the server restart.

##### pg_stat_monitor.pgsm_enable_query_plan

Type: boolean. Default: 1

Enables or disables query plan monitoring. When the `pgsm_enable_query_plan` is disabled (0), the query plan will not be captured by `pg_stat_monitor`. Enabling it may adversely affect the database performance. Requires the server restart.

##### pg_stat_monitor.track

This parameter controls which statements are tracked by `pg_stat_monitor`. 

Values: 

- `top`: Default, track only top level queries (those issued directly by clients) and excludes listing nested statements (those called within a function).
- `all`: Track top along with sub/nested queries. As a result, some SELECT statement may be shown as duplicates. 
- `none`: Disable query monitoring. The module is still loaded and is using shared memory, etc. It only silently ignores the capturing of data.

##### pg_stat_monitor.extract_comments

Type: boolean. Default: 0

This parameter controls whether to enable or disable extracting comments from queries.

##### pg_stat_monitor.pgsm_track_planning

Type: boolean. Default: 0

This parameter instructs ``pg_stat_monitor`` to monitor query planning statistics. Requires the server restart.

## Usage examples

Note that the column names differ depending on the PostgreSQL version you are using. The following usage examples are provided for PostgreSQL version 13.
For versions 11 and 12, please consult the [pg_stat_monitor reference](https://github.com/percona/pg_stat_monitor/blob/master/docs/REFERENCE.md).

### Querying buckets

```sql
postgres=# select bucket, bucket_start_time, query,calls from pg_stat_monitor order by bucket;
-[ RECORD 1 ]-----+------------------------------------------------------------------------------------
bucket | 0
bucket_start_time | 2021-10-22 11:10:00
query | select bucket, bucket_start_time, query,calls from pg_stat_monitor order by bucket;
calls | 1
```

The `bucket` parameter shows the number of a bucket for which a given record belongs.
The `bucket_start_time` shows the start time of the bucket.
`query` shows the actual query text.
`calls` shows how many times a given query was called.

### Query information

**Example 1: Shows the usename, database name, unique queryid hash, query, and the total number of calls of that query.**

```sql
postgres=# SELECT userid,  datname, queryid, substr(query,0, 50) AS query, calls FROM pg_stat_monitor;
 userid  | datname  |     queryid      |                       query                       | calls
---------+----------+------------------+---------------------------------------------------+-------
 vagrant | postgres | 939C2F56E1F6A174 | END                                               |   561
 vagrant | postgres | 2A4437C4905E0E23 | SELECT abalance FROM pgbench_accounts WHERE aid = |   561
 vagrant | postgres | 4EE9ED0CDF143477 | SELECT userid,  datname, queryid, substr(query,$1 |     1
 vagrant | postgres | 8867FEEB8A5388AC | vacuum pgbench_branches                           |     1
 vagrant | postgres | 41D1168FB0733CAB | select count(*) from pgbench_branches             |     1
 vagrant | postgres | E5A889A8FF37C2B1 | UPDATE pgbench_accounts SET abalance = abalance + |   561
 vagrant | postgres | 4876BBA9A8FCFCF9 | truncate pgbench_history                          |     1
 vagrant | postgres | 22B76AE84689E4DC | INSERT INTO pgbench_history (tid, bid, aid, delta |   561
 vagrant | postgres | F6DA9838660825CA | vacuum pgbench_tellers                            |     1
 vagrant | postgres | 214646CE6F9B1A85 | BEGIN                                             |   561
 vagrant | postgres | 27462943E814C5B5 | UPDATE pgbench_tellers SET tbalance = tbalance +  |   561
 vagrant | postgres | 4F66D46F3D4151E  | SELECT userid,  dbid, queryid, substr(query,0, 50 |     1
 vagrant | postgres | 6A02C123488B95DB | UPDATE pgbench_branches SET bbalance = bbalance + |   561
(13 rows)

```

**Example 2: Shows the connected application details.**

```sql
postgres=# SELECT application_name, client_ip, substr(query,0,100) as query FROM pg_stat_monitor;
 application_name | client_ip |                                                query
------------------+-----------+-----------------------------------------------------------------------------------------------------
 pgbench          | 127.0.0.1 | truncate pgbench_history
 pgbench          | 127.0.0.1 | SELECT abalance FROM pgbench_accounts WHERE aid = $1
 pgbench          | 127.0.0.1 | UPDATE pgbench_accounts SET abalance = abalance + $1 WHERE aid = $2
 pgbench          | 127.0.0.1 | BEGIN;
 pgbench          | 127.0.0.1 | INSERT INTO pgbench_history (tid, bid, aid, delta, mtime) VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP
 pgbench          | 127.0.0.1 | END;
 pgbench          | 127.0.0.1 | vacuum pgbench_branches
 pgbench          | 127.0.0.1 | UPDATE pgbench_tellers SET tbalance = tbalance + $1 WHERE tid = $2
 pgbench          | 127.0.0.1 | vacuum pgbench_tellers
 pgbench          | 127.0.0.1 | UPDATE pgbench_branches SET bbalance = bbalance + $1 WHERE bid = $2
 pgbench          | 127.0.0.1 | select o.n, p.partstrat, pg_catalog.count(i.inhparent) from pg_catalog.pg_class as c join pg_catalo
 psql             | 127.0.0.1 | SELECT application_name, client_ip, substr(query,$1,$2) as query FROM pg_stat_monitor
 pgbench          | 127.0.0.1 | select count(*) from pgbench_branches
(13 rows)

```

### Query timing information

```sql
SELECT  userid,  total_time, min_time, max_time, mean_time, query FROM pg_stat_monitor;
 userid |     total_time     |      min_time      |      max_time      |     mean_time      |                              query
--------+--------------------+--------------------+--------------------+--------------------+------------------------------------------------------------------
     10 |               0.14 |               0.14 |               0.14 |               0.14 | select * from pg_stat_monitor_reset()
     10 |               0.19 |               0.19 |               0.19 |               0.19 | select userid,  dbid, queryid, query from pg_stat_monitor
     10 |               0.30 |               0.13 |               0.16 |               0.15 | select bucket, bucket_start_time, query from pg_stat_monitor
     10 |               0.29 |               0.29 |               0.29 |               0.29 | select userid,  dbid, queryid, query, calls from pg_stat_monitor
     10 |           11277.79 |           11277.79 |           11277.79 |
```

### Query execution plan

```sql
postgres=# SELECT substr(query,0,50), query_plan from pg_stat_monitor limit 10;
                      substr                       |                                                  query_plan
---------------------------------------------------+---------------------------------------------------------------------------------------------------------------
 select o.n, p.partstrat, pg_catalog.count(i.inhpa | Limit                                                                                                        +
                                                   |   ->  GroupAggregate                                                                                         +
                                                   |         Group Key: (array_position(current_schemas(true), n.nspname)), p.partstrat                           +
                                                   |         ->  Sort                                                                                             +
                                                   |               Sort Key: (array_position(current_schemas(true), n.nspname)), p.partstrat                      +
                                                   |               ->  Nested Loop Left Join                                                                      +
                                                   |                     ->  Nested Loop Left Join                                                                +
                                                   |                           ->  Nested Loop                                                                    +
                                                   |                                 Join Filter: (c.relnamespace = n.oid)                                        +
                                                   |                                 ->  Index Scan using pg_class_relname_nsp_index on pg_class c                +
                                                   |                                       Index Cond: (relname = 'pgbench_accounts'::name)                       +
                                                   |                                 ->  Seq Scan on pg_namespace n                                               +
                                                   |                                       Filter: (array_position(current_schemas(true), nspname) IS NOT NULL)   +
                                                   |                           ->  Index Scan using pg_partitioned_table_partrelid_index on pg_partitioned_table p+
                                                   |                                 Index Cond: (partrelid = c.oid)                                              +
                                                   |                     ->  Bitmap Heap Scan on pg_inherits i                                                    +
                                                   |                           R
 SELECT abalance FROM pgbench_accounts WHERE aid = | Index Scan using pgbench_accounts_pkey on pgbench_accounts                                                   +
                                                   |   Index Cond: (aid = 102232)
 BEGIN;                                            |
 END;                                              |
 SELECT substr(query,$1,$2), query_plan from pg_st |
 SELECT substr(query,$1,$2),calls, planid,query_pl | Limit                                                                                                        +
                                                   |   ->  Subquery Scan on pg_stat_monitor                                                                       +
                                                   |         ->  Result                                                                                           +
                                                   |               ->  Sort                                                                                       +
                                                   |                     Sort Key: p.bucket_start_time                                                            +
                                                   |                     ->  Hash Join                                                                            +
                                                   |                           Hash Cond: (p.dbid = d.oid)                                                        +
                                                   |                           ->  Function Scan on pg_stat_monitor_internal p                                    +
                                                   |                           ->  Hash                                                                           +
                                                   |                                 ->  Seq Scan on pg_database d                                                +
                                                   |               SubPlan 1                                                                                      +
                                                   |                 ->  Function Scan on pg_stat_monitor_internal s                                              +
                                                   |                       Filter: (queryid = p.top_queryid)
 select count(*) from pgbench_branches             | Aggregate                                                                                                    +
                                                   |   ->  Seq Scan on pgbench_branches
 UPDATE pgbench_tellers SET tbalance = tbalance +  |
 vacuum pgbench_tellers                            |
 UPDATE pgbench_accounts SET abalance = abalance + |
(10 rows)
```

The `plan` column does not contain costing, width and other values. This is an expected behavior as each row is an accumulation of statistics based on `plan` and amongst other key columns. Plan is only available when the `pgsm_enable_query_plan` configuration parameter is enabled.

### Query type filtering

``pg_stat_monitor`` monitors queries per type (SELECT, INSERT, UPDATE OR DELETE) and classifies them accordingly in the ``cmd_type`` column thus reducing your efforts.

```sql
postgres=# SELECT bucket, substr(query,0, 50) AS query, cmd_type FROM pg_stat_monitor WHERE elevel = 0;
 bucket |                       query                       | cmd_type
--------+---------------------------------------------------+----------
      4 | END                                               |
      4 | SELECT abalance FROM pgbench_accounts WHERE aid = | SELECT
      4 | vacuum pgbench_branches                           |
      4 | select count(*) from pgbench_branches             | SELECT
      4 | UPDATE pgbench_accounts SET abalance = abalance + | UPDATE
      4 | truncate pgbench_history                          |
      4 | INSERT INTO pgbench_history (tid, bid, aid, delta | INSERT
      5 | SELECT relations query FROM pg_stat_monitor       | SELECT
      9 | SELECT bucket, substr(query,$1, $2) AS query, cmd |
      4 | vacuum pgbench_tellers                            |
      4 | BEGIN                                             |
      5 | SELECT relations,query FROM pg_stat_monitor       | SELECT
      4 | UPDATE pgbench_tellers SET tbalance = tbalance +  | UPDATE
      4 | UPDATE pgbench_branches SET bbalance = bbalance + | UPDATE
(14 rows)
```

### Query metadata

The `comments` column contains any text wrapped in `“/*”` and `“*/”` comment tags. The `pg_stat_monitor` extension picks up these comments and makes them available in the comments column. Please note that only the latest comment value is preserved per row. The comments may be put in any format that can be parsed by a tool.

```sql
CREATE EXTENSION hstore;
CREATE FUNCTION text_to_hstore(s text) RETURNS hstore AS $$
BEGIN
    RETURN hstore(s::text[]);
EXCEPTION WHEN OTHERS THEN
    RETURN NULL;
END; $$ LANGUAGE plpgsql STRICT;

postgres=# SELECT 1 AS num /* { "application", java_app, "real_ip", 192.168.1.1} */;
 num
-----
   1
(1 row)

postgres=# SELECT 1 AS num1,2 AS num2 /* { "application", java_app, "real_ip", 192.168.1.2} */;
 num1 | num2
------+------
    1 |    2
(1 row)

postgres=# SELECT 1 AS num1,2 AS num2, 3 AS num3 /* { "application", java_app, "real_ip", 192.168.1.3} */;
 num1 | num2 | num3
------+------+------
    1 |    2 |    3
(1 row)

postgres=# SELECT 1 AS num1,2 AS num2, 3 AS num3, 4 AS num4 /* { "application", psql_app, "real_ip", 192.168.1.3} */;
 num1 | num2 | num3 | num4
------+------+------+------
    1 |    2 |    3 |    4
(1 row)

postgres=# select query, text_to_hstore(comments) as comments_tags from pg_stat_monitor;
                                                     query                                                     |                    comments_tags
---------------------------------------------------------------------------------------------------------------+-----------------------------------------------------
 SELECT $1 AS num /* { "application", psql_app, "real_ip", 192.168.1.3) */                                     | "real_ip"=>"192.168.1.1", "application"=>"java_app"
 SELECT pg_stat_monitor_reset();                                                                               |
 select query, comments, text_to_hstore(comments) from pg_stat_monitor;                                        |
 SELECT $1 AS num1,$2 AS num2, $3 AS num3 /* { "application", java_app, "real_ip", 192.168.1.3} */             | "real_ip"=>"192.168.1.3", "application"=>"java_app"
 select query, text_to_hstore(comments) as comments_tags from pg_stat_monitor;                                 |
 SELECT $1 AS num1,$2 AS num2 /* { "application", java_app, "real_ip", 192.168.1.2} */                         | "real_ip"=>"192.168.1.2", "application"=>"java_app"
 SELECT $1 AS num1,$2 AS num2, $3 AS num3, $4 AS num4 /* { "application", psql_app, "real_ip", 192.168.1.3} */ | "real_ip"=>"192.168.1.3", "application"=>"psql_app"
(7 rows)

postgres=# select query, text_to_hstore(comments)->'application' as application_name from pg_stat_monitor;
                                                     query                                                     | application_name
---------------------------------------------------------------------------------------------------------------+----------
 SELECT $1 AS num /* { "application", psql_app, "real_ip", 192.168.1.3) */                                     | java_app
 SELECT pg_stat_monitor_reset();                                                                               |
 select query, text_to_hstore(comments)->"real_ip" as comments_tags from pg_stat_monitor;                      |
 select query, text_to_hstore(comments)->$1 from pg_stat_monitor                                               |
 select query, text_to_hstore(comments) as comments_tags from pg_stat_monitor;                                 |
 select query, text_to_hstore(comments)->"application" as comments_tags from pg_stat_monitor;                  |
 SELECT $1 AS num1,$2 AS num2 /* { "application", java_app, "real_ip", 192.168.1.2} */                         | java_app
 SELECT $1 AS num1,$2 AS num2, $3 AS num3 /* { "application", java_app, "real_ip", 192.168.1.3} */             | java_app
 select query, comments, text_to_hstore(comments) from pg_stat_monitor;                                        |
 SELECT $1 AS num1,$2 AS num2, $3 AS num3, $4 AS num4 /* { "application", psql_app, "real_ip", 192.168.1.3} */ | psql_app
(10 rows)

postgres=# select query, text_to_hstore(comments)->'real_ip' as real_ip from pg_stat_monitor;
                                                     query                                                     |  real_ip
---------------------------------------------------------------------------------------------------------------+-------------
 SELECT $1 AS num /* { "application", psql_app, "real_ip", 192.168.1.3) */                                     | 192.168.1.1
 SELECT pg_stat_monitor_reset();                                                                               |
 select query, text_to_hstore(comments)->"real_ip" as comments_tags from pg_stat_monitor;                      |
 select query, text_to_hstore(comments)->$1 from pg_stat_monitor                                               |
 select query, text_to_hstore(comments) as comments_tags from pg_stat_monitor;                                 |
 select query, text_to_hstore(comments)->"application" as comments_tags from pg_stat_monitor;                  |
 SELECT $1 AS num1,$2 AS num2 /* { "application", java_app, "real_ip", 192.168.1.2} */                         | 192.168.1.2
 SELECT $1 AS num1,$2 AS num2, $3 AS num3 /* { "application", java_app, "real_ip", 192.168.1.3} */             | 192.168.1.3
 select query, comments, text_to_hstore(comments) from pg_stat_monitor;                                        |
 SELECT $1 AS num1,$2 AS num2, $3 AS num3, $4 AS num4 /* { "application", psql_app, "real_ip", 192.168.1.3} */ | 192.168.1.3
(10 rows)
```

### Top query tracking

In the following example we create a function `add2` that adds one parameter value to another one and call this function to calculate 1+2.


```sql
CREATE OR REPLACE function add2(int, int) RETURNS int as
$$
BEGIN
   return (select $1 + $2);
END;
$$ language plpgsql;

SELECT add2(1,2);
 add2
-----
   3
(1 row)

The ``pg_stat_monitor`` view shows all executed queries and shows the very first query in a row - calling the `add2` function.

postgres=# SELECT queryid, top_queryid, query, top_query FROM pg_stat_monitor;
     queryid      |   top_queryid    |                       query.                           |     top_query
------------------+------------------+-------------------------------------------------------------------------+-------------------
 3408CA84B2353094 |                  | select add2($1,$2)                                     |
 762B99349F6C7F31 | 3408CA84B2353094 | SELECT (select $1 + $2)                                | select add2($1,$2)
(2 rows)
```

### Relations

**Example 1: List all the table names involved in the query.**

```sql
postgres=# SELECT relations,query FROM pg_stat_monitor;
           relations                  |                                                query
-------------------------------+------------------------------------------------------------------------------------------------------
                                      | END
 {public.pgbench_accounts}            | SELECT abalance FROM pgbench_accounts WHERE aid = $1
                                      | vacuum pgbench_branches
 {public.pgbench_branches}            | select count(*) from pgbench_branches
 {public.pgbench_accounts}            | UPDATE pgbench_accounts SET abalance = abalance + $1 WHERE aid = $2
                                      | truncate pgbench_history
 {public.pgbench_history}             | INSERT INTO pgbench_history (tid, bid, aid, delta, mtime) VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP)
 {public.pg_stat_monitor,pg_catalog.pg_database} | SELECT relations query FROM pg_stat_monitor
                                      | vacuum pgbench_tellers
                                      | BEGIN
 {public.pgbench_tellers}             | UPDATE pgbench_tellers SET tbalance = tbalance + $1 WHERE tid = $2
 {public.pgbench_branches}            | UPDATE pgbench_branches SET bbalance = bbalance + $1 WHERE bid = $2
(12 rows)
```

**Example 2: List all the views and the name of the table in the view. Here we have a view "test_view"**

```sql
\d+ test_view
                          View "public.test_view"
 Column |  Type   | Collation | Nullable | Default | Storage | Description 
--------+---------+-----------+----------+---------+---------+-------------
 foo_a  | integer |           |          |         | plain   | 
 bar_a  | integer |           |          |         | plain   | 
View definition:
 SELECT f.a AS foo_a,
    b.a AS bar_a
   FROM foo f,
    bar b;
```

Now when we query the ``pg_stat_monitor``, it will show the view name and also all the table names in the view. Note that the view name is followed by an asterisk (*).

```sql
SELECT relations, query FROM pg_stat_monitor;
      relations      |    query                                                 
---------------------+----------------------------------------------------
 {test_view*,foo,bar} | select * from test_view
 {foo,bar}           | select * from foo,bar
(2 rows)
```

### Queries terminated with errors

```sql
SELECT substr(query,0,50) AS query, decode_error_level(elevel) AS elevel,sqlcode, calls, substr(message,0,50) message
FROM pg_stat_monitor;
                       query                       | elevel | sqlcode | calls |                      message
---------------------------------------------------+--------+---------+-------+---------------------------------------------------
 select substr(query,$1,$2) as query, decode_error |        |       0 |     1 |
 select bucket,substr(query,$1,$2),decode_error_le |        |       0 |     3 |
                                                   | LOG    |       0 |     1 | database system is ready to accept connections
 select 1/0;                                       | ERROR  |     130 |     1 | division by zero
                                                   | LOG    |       0 |     1 | database system was shut down at 2020-11-11 11:37
 select $1/$2                                      |        |       0 |     1 |
(6 rows)
          11277.79 | SELECT * FROM foo
```

### Histogram

Histogram (the `resp_calls` parameter) provides a visual representation of query performance. With the help of the histogram function, you can view a timing/calling data histogram in response to a SQL query.


```sql
SELECT resp_calls, query FROM pg_stat_monitor;
                    resp_calls                    |                 query                                        
--------------------------------------------------+----------------------------------------------
{1," 0"," 0"," 0"," 0"," 0"," 0"," 0"," 0"," 0"} | select client_ip, query from pg_stat_monitor
{3," 0"," 0"," 0"," 0"," 0"," 0"," 0"," 0"," 1"} | select * from pg_stat_monitor_reset()
{3," 0"," 0"," 0"," 0"," 0"," 0"," 0"," 0"," 1"} | SELECT * FROM foo

postgres=# SELECT * FROM histogram(0, 'F44CD1B4B33A47AF') AS a(range TEXT, freq INT, bar TEXT);
       range        | freq |              bar
--------------------+------+--------------------------------
  (0 - 3)}          |    2 | ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
  (3 - 10)}         |    0 |
  (10 - 31)}        |    1 | ■■■■■■■■■■■■■■■
  (31 - 100)}       |    0 |
  (100 - 316)}      |    0 |
  (316 - 1000)}     |    0 |
  (1000 - 3162)}    |    0 |
  (3162 - 10000)}   |    0 |
  (10000 - 31622)}  |    0 |
  (31622 - 100000)} |    0 |
(10 rows)
```

There are 10 time based buckets of the time generated automatically based on total buckets in the field ``resp_calls``. The value in the field shows how many queries run in that period of time.

