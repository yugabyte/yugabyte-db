# User Guide

This document describes the configuration, key features and usage of ``pg_stat_monitor`` extension and compares it with ``pg_stat_statements``.

For how to install and set up ``pg_stat_monitor``, see [README](https://github.com/percona/pg_stat_monitor/blob/master/README.md).

After you've installed and enabled ``pg_stat_monitor``, create the ``pg_stat_monitor`` extension using the ``CREATE EXTENSION`` command.

```sql
CREATE EXTENSION pg_stat_monitor;
CREATE EXTENSION
```

### Configuration
Here is the complete list of configuration parameters.
```sql
SELECT * FROM pg_stat_monitor_settings;
                   name                   | value  | default_value |                                               description                                                | minimum |  maximum   | restart 
------------------------------------------+--------+---------------+----------------------------------------------------------------------------------------------------------+---------+------------+---------
 pg_stat_monitor.pgsm_max                 |    100 |           100 | Sets the maximum size of shared memory in (MB) used for statement's metadata tracked by pg_stat_monitor. |       1 |      1000  |       1
 pg_stat_monitor.pgsm_query_max_len       |   1024 |          1024 | Sets the maximum length of query.                                                                        |    1024 | 2147483647 |       1
 pg_stat_monitor.pgsm_enable              |      1 |             1 | Enable/Disable statistics collector.                                                                     |       0 |      0     |       0
 pg_stat_monitor.pgsm_track_utility       |      1 |             1 | Selects whether utility commands are tracked.                                                            |       0 |      0     |       0
 pg_stat_monitor.pgsm_normalized_query    |      1 |             1 | Selects whether save query in normalized format.                                                         |       0 |      0     |       0
 pg_stat_monitor.pgsm_max_buckets         |     10 |            10 | Sets the maximum number of buckets.                                                                      |       1 |      10    |       1
 pg_stat_monitor.pgsm_bucket_time         |    300 |           300 | Sets the time in seconds per bucket.                                                                     |       1 | 2147483647 |       1
 pg_stat_monitor.pgsm_histogram_min       |      0 |             0 | Sets the time in millisecond.                                                                            |       0 | 2147483647 |       1
 pg_stat_monitor.pgsm_histogram_max       | 100000 |        100000 | Sets the time in millisecond.                                                                            |      10 | 2147483647 |       1
 pg_stat_monitor.pgsm_histogram_buckets   |     10 |            10 | Sets the maximum number of histogram buckets                                                             |       2 | 2147483647 |       1
 pg_stat_monitor.pgsm_query_shared_buffer |     20 |            20 | Sets the maximum size of shared memory in (MB) used for query tracked by pg_stat_monitor.                |       1 |      10000 |       1
 pg_stat_monitor.pgsm_overflow_target     |      1 |             1 | Sets the overflow target for pg_stat_monitor                                                             |       0 |      1     |       1
 pg_stat_monitor.pgsm_track_planning      |      0 |             1 | Selects whether planning statistics are tracked.                                                         |       0 |      0     |       0
(13 rows)


```
Some configuration parameters require the server restart and should be set before the server startup. These must be set in the ``postgresql.conf`` file. Other parameters do not require server restart and can be set permanently either in the ``postgresql.conf`` or from the client (``psql``).

The table below shows set up options for each configuration parameter and whether the server restart is required to apply the parameter's value:


| Parameter Name                                |  postgresql.conf   | SET | ALTER SYSTEM SET  |  server restart   | configuration reload
| ----------------------------------------------|--------------------|-----|-------------------|-------------------|---------------------
| pg_stat_monitor.pgsm_max                      | :heavy_check_mark: | :x:                |:x:                |:heavy_check_mark: | :x:
| pg_stat_monitor.pgsm_query_max_len            | :heavy_check_mark: | :x:                |:x:                |:heavy_check_mark: | :x:
| pg_stat_monitor.pgsm_enable                   | :heavy_check_mark: | :x:                |:heavy_check_mark: |:x: | :x:
| pg_stat_monitor.pgsm_track_utility            | :heavy_check_mark: | :heavy_check_mark: |:heavy_check_mark: |:x: | :heavy_check_mark:
| pg_stat_monitor.pgsm_normalized_query         | :heavy_check_mark: | :heavy_check_mark: |:heavy_check_mark: |:x: | :heavy_check_mark:
| pg_stat_monitor.pgsm_max_buckets              | :heavy_check_mark: | :x:                |:x:                |:heavy_check_mark: | :heavy_check_mark:
| pg_stat_monitor.pgsm_bucket_time              | :heavy_check_mark: | :x:                |:x:                |:heavy_check_mark: | :x:
| pg_stat_monitor.pgsm_object_cache             | :heavy_check_mark: | :x:                |:x:                |:heavy_check_mark: | :x:
| pg_stat_monitor.pgsm_respose_time_lower_bound | :heavy_check_mark: | :x:                |:x:                |:heavy_check_mark: | :x:
| pg_stat_monitor.pgsm_respose_time_step        | :heavy_check_mark: | :x:                |:x:                |:heavy_check_mark: | :x:
| pg_stat_monitor.pgsm_query_shared_buffer      | :heavy_check_mark: | :x:                |:x:                |:heavy_check_mark: | :x:
  
#### Parameters description:

- **pg_stat_monitor.pgsm_max**: This parameter defines the limit of shared memory for ``pg_stat_monitor``. This memory is used by buckets in a circular manner. The memory is divided between the buckets equally, at the start of the PostgreSQL. 
- **pg_stat_monitor.pgsm_query_max_len**: Sets the maximum size of the query. This parameter can only be set at the start of PostgreSQL. For long queries, the query is truncated to that particular length. This is to avoid unnecessary usage of shared memory.
- **pg_stat_monitor.pgsm_enable**: This parameter enables or disables the monitoring. "Disable" means that ``pg_stat_monitor`` will not collect the statistics for the whole cluster.
- **pg_stat_monitor.pgsm_track_utility**: This parameter controls whether utility commands are tracked by the module. Utility commands are all those other than ``SELECT``, ``INSERT``, ``UPDATE``, and ``DELETE``. 
- **pg_stat_monitor.pgsm_normalized_query**: By default, the query shows the actual parameter instead of the placeholder. It is quite useful when users want to use that query and try to run that query to check the abnormalities. But in most cases users like the queries with a placeholder. This parameter is used to toggle between the two said options.
- **pg_stat_monitor.pgsm_max_buckets**: ``pg_stat_monitor`` accumulates the information in the form of buckets. All the aggregated information is bucket based. This parameter is used to set the number of buckets the system can have. For example, if this parameter is set to 2, then the system will create two buckets. First, the system will add all the information into the first bucket. After its lifetime (defined in the  pg_stat_monitor.pgsm_bucket_time parameter) expires, it will switch to the second bucket,  reset all the counters and repeat the process.
- **pg_stat_monitor.pgsm_bucket_time**: This parameter is used to set the lifetime of the bucket. System switches between buckets on the basis of ``pg_stat_monitor.pgsm_bucket_time``. 
- **pg_stat_monitor.pgsm_respose_time_lower_bound**: ``pg_stat_monitor`` also stores the execution time histogram. This parameter is used to set the lower bound of the histogram.
- **pg_stat_monitor.pgsm_respose_time_step:** This parameter is used to set the steps for the histogram. 


### Usage

pg_stat_monitor extension contains a view called pg_stat_monitor, which contains all the monitoring information. Find the list of columns in pg_stat_monitor view in the following table. The table also shows whether a particular column is available in pg_stat_statements.


|      Column        |           Type           | pg_stat_monitor      | pg_stat_statements
|--------------------|--------------------------|----------------------|------------------
 bucket              | integer                  | :heavy_check_mark:  | :x:
 bucket_start_time   | timestamp with time zone | :heavy_check_mark:  | :x:
 userid              | oid                      | :heavy_check_mark:  | :heavy_check_mark:
 dbid                | oid                      | :heavy_check_mark:  | :heavy_check_mark:
 client_ip           | inet                     | :heavy_check_mark:  | :x:
 queryid             | text                     | :heavy_check_mark:  | :heavy_check_mark:
 planid              | text                     | :heavy_check_mark:  | :x:
 query_plan          | text                     | :heavy_check_mark:  | :x:
 top_query           | text                     | :heavy_check_mark:  | :x:
 query               | text                     | :heavy_check_mark:  | :heavy_check_mark:
 application_name    | text                     | :heavy_check_mark:  | :x:
 relations           | text[]                   | :heavy_check_mark:  | :x:
 cmd_type            | text[]                   | :heavy_check_mark:  | :x:
 elevel              | integer                  | :heavy_check_mark:  | :x:
 sqlcode             | integer                  | :heavy_check_mark:  | :x:
 message             | text                     | :heavy_check_mark:  | :x:
 plans               | bigint                   | :heavy_check_mark:  | :heavy_check_mark:
 plan_total_time     | double precision         | :heavy_check_mark:  | :heavy_check_mark:
 plan_min_timei      | double precision         | :heavy_check_mark:  | :heavy_check_mark:
 plan_max_time       | double precision         | :heavy_check_mark:  | :heavy_check_mark: 
 plan_mean_time      | double precision         | :heavy_check_mark:  | :heavy_check_mark:
 plan_stddev_time    | double precision         | :heavy_check_mark:  | :heavy_check_mark: 
 calls               | bigint                   | :heavy_check_mark:  | :heavy_check_mark:
 total_time          | double precision         | :heavy_check_mark:  | :heavy_check_mark:
 min_time            | double precision         | :heavy_check_mark:  | :heavy_check_mark:
 max_time            | double precision         | :heavy_check_mark:  | :heavy_check_mark:
 mean_time           | double precision         | :heavy_check_mark:  | :heavy_check_mark:
 stddev_time         | double precision         | :heavy_check_mark:  | :heavy_check_mark:
 rows_retrieved      | bigint                   | :heavy_check_mark:  | :heavy_check_mark:
 shared_blks_hit     | bigint                   | :heavy_check_mark:  | :heavy_check_mark:
 shared_blks_read    | bigint                   | :heavy_check_mark:  | :heavy_check_mark:
 shared_blks_dirtied | bigint                   | :heavy_check_mark:  | :heavy_check_mark:
 shared_blks_written | bigint                   | :heavy_check_mark:  | :heavy_check_mark:
 local_blks_hit      | bigint                   | :heavy_check_mark:  | :heavy_check_mark: 
 local_blks_read     | bigint                   | :heavy_check_mark:  | :heavy_check_mark: 
 local_blks_dirtied  | bigint                   | :heavy_check_mark:  | :heavy_check_mark:
 local_blks_written  | bigint                   | :heavy_check_mark:  | :heavy_check_mark:
 temp_blks_read      | bigint                   | :heavy_check_mark:  | :heavy_check_mark:
 temp_blks_written   | bigint                   | :heavy_check_mark:  | :heavy_check_mark:
 blk_read_time       | double precision         | :heavy_check_mark:  | :heavy_check_mark:
 blk_write_time      | double precision         | :heavy_check_mark:  | :heavy_check_mark:
 resp_calls          | text[]                   | :heavy_check_mark:  | :x:
 cpu_user_time       | double precision         | :heavy_check_mark:  | :x:
 cpu_sys_time        | double precision         | :heavy_check_mark:  | :x:
 wal_records         | bigint           		| :heavy_check_mark:  | :heavy_check_mark:
 wal_fpi             | bigint           		| :heavy_check_mark:  | :heavy_check_mark:
 wal_bytes           | numeric          		| :heavy_check_mark:  | :heavy_check_mark:
 state_code          | bigint           		| :heavy_check_mark:  | :x:
 state               | text                     | :heavy_check_mark:  | :x:



The following are some key features of pg_stat_monitor and usage examples.

#### Buckets

**`bucket`**: Accumulates the statistics per bucket. All the information and aggregate reset for each bucket. The bucket will be a number showing the number of buckets for which this record belongs.

**`bucket_start_time`**: shows the start time of the bucket. 

```sql
postgres=# select bucket, bucket_start_time, query,calls from pg_stat_monitor order by bucket;

bucket |  bucket_start_time  |                                                     query                                                     | calls 
--------+---------------------+---------------------------------------------------------------------------------------------------------------+-------
      3 | 11-01-2021 17:30:45 | copy pgbench_accounts from stdin                                                                              |     1
      3 | 11-01-2021 17:30:45 | alter table pgbench_accounts add primary key (aid)                                                            |     1
      3 | 11-01-2021 17:30:45 | vacuum analyze pgbench_accounts                                                                               |     1
      3 | 11-01-2021 17:30:45 | vacuum analyze pgbench_tellers                                                                                |     1
      3 | 11-01-2021 17:30:45 | insert into pgbench_branches(bid,bbalance) values($1,$2)                                                      |   100
      5 | 11-01-2021 17:31:15 | vacuum analyze pgbench_branches                                                                               |     1
      5 | 11-01-2021 17:31:15 | copy pgbench_accounts from stdin                                                                              |     1
      5 | 11-01-2021 17:31:15 | vacuum analyze pgbench_tellers                                                                                |     1
      5 | 11-01-2021 17:31:15 | commit                                                                                                        |     1
      6 | 11-01-2021 17:31:30 | alter table pgbench_branches add primary key (bid)                                                            |     1
      6 | 11-01-2021 17:31:30 | vacuum analyze pgbench_accounts                                                                               |     1
```

#### Query Information

**`userid`**: An ID of the user to whom that query belongs. pg_stat_monitor collects queries from all the users and uses the `userid` to segregate the queries based on different users.

**`dbid`**: The database ID of the query. pg_stat_monitor accumulates queries from all the databases; therefore, this column is used to identify the database.

**`queryid`**:  pg_stat_monitor generates a unique ID for each query (queryid). 

**`query`**: The query column contains the actual text of the query. This parameter depends on the **`pg_stat_monitor.pgsm_normalized_query`** configuration parameters, in which format to show the query.

**`calls`**: Number of calls of that particular query.


##### Example 1: Shows the usename, database name, unique queryid hash, query, and the total number of calls of that query.
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

##### Example 4: Shows the connected application_name.

```sql
SELECT application_name, query FROM pg_stat_monitor;
 application_name |                                                query                                                 
------------------+------------------------------------------------------------------------------------------------------
 pgbench          | UPDATE pgbench_branches SET bbalance = bbalance + $1 WHERE bid = $2
 pgbench          | UPDATE pgbench_accounts SET abalance = abalance + $1 WHERE aid = $2
 pgbench          | vacuum pgbench_tellers
 pgbench          | SELECT abalance FROM pgbench_accounts WHERE aid = $1
 pgbench          | END
 pgbench          | select count(*) from pgbench_branches
 pgbench          | BEGIN
 pgbench          | INSERT INTO pgbench_history (tid, bid, aid, delta, mtime) VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP)
 psql             | select application_name, query from pg_stat_monitor
 pgbench          | vacuum pgbench_branches
 psql             | select application_name query from pg_stat_monitor
 pgbench          | truncate pgbench_history
 pgbench          | UPDATE pgbench_tellers SET tbalance = tbalance + $1 WHERE tid = $2
(13 rows)
```

#### Error Messages / Error Codes and Error Level

**`elevel`**, **`sqlcode`**,**`message`**,: error level / sql code and  log/warning/ error message

```sql
SELECT substr(query,0,50) AS query, decode_error_level(elevel) AS elevel,sqlcode, calls, substr(message,0,50) message 
FROM pg_stat_monitor;
                       query                       | elevel | sqlcode | calls |                      message                      
---------------------------------------------------+--------+---------+-------+---------------------------------------------------
 select substr(query,$1,$2) as query, decode_error |        |       0 |     1 | 
 select bucket,substr(query,$1,$2),decode_error_le |        |       0 |     3 | 
                                                   | LOG    |       0 |     1 | database system is ready to accept connections
 select 1/0;                                       | ERROR  |     130 |     1 | division by zero
                                                   | LOG    |       0 |     1 | database system was shut down at 2020-11-11 11:37
 select $1/$2                                      |        |       0 |     1 | 
(6 rows)
```

#### Query Timing Information

**`total_time`**,  **`min_time`**, **`max_time`**, **`mean_time`**: The total / minimum / maximum and mean time spent for the same query.


```
SELECT  userid,  total_time, min_time, max_time, mean_time, query FROM pg_stat_monitor;
 userid |     total_time     |      min_time      |      max_time      |     mean_time      |                              query                               
--------+--------------------+--------------------+--------------------+--------------------+------------------------------------------------------------------
     10 |               0.14 |               0.14 |               0.14 |               0.14 | select * from pg_stat_monitor_reset()
     10 |               0.19 |               0.19 |               0.19 |               0.19 | select userid,  dbid, queryid, query from pg_stat_monitor
     10 |               0.30 |               0.13 |               0.16 |               0.15 | select bucket, bucket_start_time, query from pg_stat_monitor
     10 |               0.29 |               0.29 |               0.29 |               0.29 | select userid,  dbid, queryid, query, calls from pg_stat_monitor
     10 |           11277.79 |           11277.79 |           11277.79 |           11277.79 | SELECT * FROM foo
```

#### Client IP address 

**`client_ip`**: The IP address of the client that originated the query.

```sql
SELECT userid::regrole, datname, substr(query,0, 50) AS query, calls, client_ip 
FROM pg_stat_monitor, pg_database 
WHERE dbid = oid;
userid  | datname  |                       query                       | calls | client_ip 
---------+----------+---------------------------------------------------+-------+-----------
 vagrant | postgres | UPDATE pgbench_branches SET bbalance = bbalance + |  1599 | 10.0.2.15
 vagrant | postgres | select userid::regrole, datname, substr(query,$1, |     5 | 10.0.2.15
 vagrant | postgres | UPDATE pgbench_accounts SET abalance = abalance + |  1599 | 10.0.2.15
 vagrant | postgres | select userid::regrole, datname, substr(query,$1, |     1 | 127.0.0.1
 vagrant | postgres | vacuum pgbench_tellers                            |     1 | 10.0.2.15
 vagrant | postgres | SELECT abalance FROM pgbench_accounts WHERE aid = |  1599 | 10.0.2.15
 vagrant | postgres | END                                               |  1599 | 10.0.2.15
 vagrant | postgres | select count(*) from pgbench_branches             |     1 | 10.0.2.15
 vagrant | postgres | BEGIN                                             |  1599 | 10.0.2.15
 vagrant | postgres | INSERT INTO pgbench_history (tid, bid, aid, delta |  1599 | 10.0.2.15
 vagrant | postgres | vacuum pgbench_branches                           |     1 | 10.0.2.15
 vagrant | postgres | truncate pgbench_history                          |     1 | 10.0.2.15
 vagrant | postgres | UPDATE pgbench_tellers SET tbalance = tbalance +  |  1599 | 10.0.2.15
```

#### Call Timings Histogram

**`resp_calls`**: Call histogram

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

There are 10 timebase buckets of the time **`pg_stat_monitor.pgsm_respose_time_step`** in the field ``resp_calls``. The value in the field shows how many queries run in that period of time.


#### Object Information.

**`relations`**: The list of tables involved in the query

##### Example 1: List all the table names involved in the query.
```sql
postgres=# SELECT relations,query FROM pg_stat_monitor;
           relations           |                                                query                                                 
-------------------------------+------------------------------------------------------------------------------------------------------
                               | END
 {pgbench_accounts}            | SELECT abalance FROM pgbench_accounts WHERE aid = $1
                               | vacuum pgbench_branches
 {pgbench_branches}            | select count(*) from pgbench_branches
 {pgbench_accounts}            | UPDATE pgbench_accounts SET abalance = abalance + $1 WHERE aid = $2
                               | truncate pgbench_history
 {pgbench_history}             | INSERT INTO pgbench_history (tid, bid, aid, delta, mtime) VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP)
 {pg_stat_monitor,pg_database} | SELECT relations query FROM pg_stat_monitor
                               | vacuum pgbench_tellers
                               | BEGIN
 {pgbench_tellers}             | UPDATE pgbench_tellers SET tbalance = tbalance + $1 WHERE tid = $2
 {pgbench_branches}            | UPDATE pgbench_branches SET bbalance = bbalance + $1 WHERE bid = $2
(12 rows)
```

##### Example 2: List all the views and the name of the table in the view. Here we have a view "test_view"
```sql
\d+ test_view
                          View "public.test_view"
 Column |  Type   | Collation | Nullable | Default | Storage | Description 
--------+---------+-----------+----------+---------+---------+-------------
 foo_a  | integer |           |          |         | plain   | 
 bar_a  | integer |           |          |         | plain   | 
View definition:
 SELECT f.a AS foo_a,
    b.a AS bar_a
   FROM foo f,
    bar b;
```

Now when we query the pg_stat_monitor, it will show the view name and also all the table names in the view.
```sql
SELECT relations, query FROM pg_stat_monitor;
      relations      |                                                query                                                 
---------------------+------------------------------------------------------------------------------------------------------
 {test_view,foo,bar} | select * from test_view
 {foo,bar}           | select * from foo,bar
(2 rows)
```

#### Query command Type (SELECT, INSERT, UPDATE OR DELETE)

**`cmd_type`**: List the command type of the query.

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

#### Function Execution Tracking 

**`top_queryid`**: Outer layer caller's query id.

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

postgres=# SELECT queryid, top_queryid, query, top_query FROM pg_stat_monitor;
     queryid      |   top_queryid    |                       query.                           |     top_query
------------------+------------------+-------------------------------------------------------------------------+-------------------
 3408CA84B2353094 |                  | select add2($1,$2)                                     |
 762B99349F6C7F31 | 3408CA84B2353094 | SELECT (select $1 + $2)                                | select add2($1,$2)
(2 rows)
```

#### Monitor Query Execution Plan.

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
