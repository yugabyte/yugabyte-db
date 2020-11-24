## User Guide

Create the extension using the ``CREATE EXTENSION`` command.

```sql
CREATE EXTENSION pg_stat_monitor;
CREATE EXTENSION
```

### Configuration
Here is the complete list of configuration parameters.
```sql
SELECT * FROM pg_stat_monitor_settings;
                     name                      | value  | default_value |                            description                            | minimum |  maximum   | restart 
-----------------------------------------------+--------+---------------+-------------------------------------------------------------------+---------+------------+---------
 pg_stat_monitor.pgsm_max                      |   5000 |          5000 | Sets the maximum number of statements tracked by pg_stat_monitor. |    5000 | 2147483647 |       1
 pg_stat_monitor.pgsm_query_max_len            |   1024 |          1024 | Sets the maximum length of query.                                 |    1024 | 2147483647 |       1
 pg_stat_monitor.pgsm_enable                   |      1 |             1 | Enable/Disable statistics collector.                              |       0 |          0 |       1
 pg_stat_monitor.pgsm_track_utility            |      1 |             0 | Selects whether utility commands are tracked.                     |       0 |          0 |       0
 pg_stat_monitor.pgsm_normalized_query         |      1 |             0 | Selects whether save query in normalized format.                  |       0 |          0 |       0
 pg_stat_monitor.pgsm_max_buckets              |     10 |            10 | Sets the maximum number of buckets.                               |       1 |         10 |       1
 pg_stat_monitor.pgsm_bucket_time              |     60 |            60 | Sets the time in seconds per bucket.                              |       1 | 2147483647 |       1
 pg_stat_monitor.pgsm_respose_time_lower_bound |      1 |             1 | Sets the time in millisecond.                                     |       1 | 2147483647 |       1
 pg_stat_monitor.pgsm_respose_time_step        |      1 |             1 | Sets the respose time steps in millisecond.                       |       1 | 2147483647 |       1
 pg_stat_monitor.pgsm_query_shared_buffer      | 500000 |        500000 | Sets the query shared_buffer size.                                |  500000 | 2147483647 |       1
(11 rows)
```
Some configuration parameters require the server restart and should be set before the server startup. These must be set in the ``postgresql.conf`` file. Other parameters do not require server restart and can be set permanently either in the ``postgresql.conf`` or from the client (``psql``).

The table below shows set up options for each configuration parameter and whether the server restart is required to apply its value:


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

pg_stat_monitor extension contains a view called pg_stat_monitor, which contains all the monitoring information. Following is the list of columns in pg_stat_monitor.

|      Column        |           Type           | pg_stat_monitor      | pg_stat_statments
|--------------------|--------------------------|----------------------|------------------
 bucket              | integer                  | :heavy_check_mark:  | :x:
 bucket_start_time   | timestamp with time zone | :heavy_check_mark:  | :x:
 userid              | oid                      | :heavy_check_mark:  | :heavy_check_mark:
 dbid                | oid                      | :heavy_check_mark:  | :heavy_check_mark:
 client_ip           | inet                     | :heavy_check_mark:  | :x:
 queryid             | text                     | :heavy_check_mark:  | :heavy_check_mark:
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
 rows                | bigint                   | :heavy_check_mark:  | :heavy_check_mark:
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

Here are some key features of pg_stat_monitor.

#### Buckets
pg_stat_monitor collects and aggregates data on a bucket basis. The size of a bucket and the number of buckets should be configured using GUC (Grand Unified Configuration). When a bucket time elapses, pg_stat_monitor resets all the statistics and switches to the next bucket. After the last bucket elapses, pg_stat_monitor goes back to the first bucket. All the data on the first bucket will vanish; therefore, users must read the buckets before that to not lose the data.

**`bucket`**: Accumulates the statistics per bucket. All the information and aggregate reset for each bucket. The bucket will be a number showing the number of buckets for which this record belongs.

**`bucket_start_time`**: shows the start time of the bucket. 

```sql
SELECT bucket, bucket_start_time, query FROM pg_stat_monitor;
 bucket |       bucket_start_time       |                            query                             
--------+-------------------------------+--------------------------------------------------------
2       | 2020-05-23 13:24:44.652415+00 | select * from pg_stat_monitor_reset()
3       | 2020-05-23 13:45:01.55658+00  | select bucket, bucket_start_time, query from pg_stat_monitor
2       | 2020-05-23 13:24:44.652415+00 | SELECT * FROM foo
(3 rows)
```

#### Query Information

**`userid`**: An ID of the user to whom that query belongs. pg_stat_monitor is used to collect queries from all the users; therefore, `userid` is used to segregate the queries based on different users.

**`dbid`**: The database ID of the query. pg_stat_monitor accumulates queries from all the databases; therefore, this column is used to identify the database.

**`queryid`**:  pg_stat_monitor generates a unique ID for each query (queryid). 

**`query`**: The query column contains the actual text of the query. This parameter depends on the **`pg_stat_monitor.pgsm_normalized_query`** configuration parameters, in which format to show the query.

**`calls`**: Number of calls of that particular query.


##### Example 1: Shows the userid, dbid, unique queryid hash, query, and the total number of calls or that query.
```sql
SELECT userid,  dbid, queryid, substr(query,0, 50) AS query, calls FROM pg_stat_monitor;
 userid | dbid  |     queryid      |                       query                       | calls 
--------+-------+------------------+---------------------------------------------------+-------
     10 | 12709 | 214646CE6F9B1A85 | BEGIN                                             |  1577
     10 | 12709 | 8867FEEB8A5388AC | vacuum pgbench_branches                           |     1
     10 | 12709 | F47D95C9DF863E43 | UPDATE pgbench_branches SET bbalance = bbalance + |  1577
     10 | 12709 | 2FE0A6ABDC20623  | select substr(query,$1, $2) as query, cmd_type fr |     7
     10 | 12709 | A83503D3E1F99139 | select userid,  dbid, queryid, substr(query,$1, $ |     1
     10 | 12709 | D4B1243AC3268B9B | select count(*) from pgbench_branches             |     1
     10 | 12709 | 2FE0A6ABDC20623  | select substr(query,$1, $2) as query, cmd_type fr |     1
     10 | 12709 | 1D9BDBBCFB89F096 | UPDATE pgbench_accounts SET abalance = abalance + |  1577
     10 | 12709 | 15343084284782B  | update test SET a = $1                            |     2
     10 | 12709 | 939C2F56E1F6A174 | END                                               |  1577
      1 | 12709 | DAE6D269D27F2EC4 | SELECT FOR UPDATE test SET a = 1;                 |     1
     10 | 12709 | 2B50C2406BFAC907 | INSERT INTO pgbench_history (tid, bid, aid, delta |  1577
     10 | 12709 | F6DA9838660825CA | vacuum pgbench_tellers                            |     1
     10 | 12709 | 3AFB8B2452721F9  | SELECT a from test for update                     |     1
     10 | 12709 | A5CD0AF80D28363  | SELECT abalance FROM pgbench_accounts WHERE aid = |  1577
     10 | 12709 | E445DD36B9189C53 | UPDATE pgbench_tellers SET tbalance = tbalance +  |  1577
     10 | 12709 | 4876BBA9A8FCFCF9 | truncate pgbench_history                          |     1
     10 | 12709 | D3C2299FD9E7348C | select userid,  dbid, queryid, query, calls from  |     1
(18 rows)
```

##### Example 2: Shows the different usernames for the query. 

```
SELECT userid::regrole, datname, substr(query,0, 50) AS query, calls from pg_stat_monitor, pg_database WHERE dbid = oid;
  userid  | datname  |                       query                       | calls 
----------+----------+---------------------------------------------------+-------
 vagrant  | postgres | select userid::regrole, datname, substr(query,$1, |     1
 vagrant  | test_db  | insert into bar values($1)                        |     1
 vagrant  | postgres | insert into bar values($1)                        |     1
 vagrant  | test_db  | select * from bar                                 |     1
 postgres | postgres | insert into bar values($1)                        |     1
(5 rows)
```

##### Example 3: Shows the different database involved in the queries.

```
SELECT userid::regrole, datname, substr(query,0, 50) as query, calls from pg_stat_monitor, pg_database WHERE dbid = oid;
 userid  | datname  |                       query                       | calls 
---------+----------+---------------------------------------------------+-------
 vagrant | postgres | select userid::regrole, datname, substr(query,$1, |     0
 vagrant | test_db  | insert into bar values($1)                        |     1
 vagrant | test_db  | select * from bar                                 |     1
(3 rows)
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

**`elevel`**, **`sqlcode`**,**`message`**,: error level / sql code and  log/warning/error message

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
```

There are 10 timebase buckets of the time **`pg_stat_monitor.pgsm_respose_time_step`** in the field ``resp_calls``. The value in the field shows how many queries run in that period of time.


#### Object Information.

**`relations`**: The list of tables involved in the query

##### Example 1: List all the table names involved in the query.
```sql
SELECT relations::oid[]::regclass[], query FROM pg_stat_monitor;
     relations      |                                                query                                                 
--------------------+------------------------------------------------------------------------------------------------------
 {pgbench_accounts} | UPDATE pgbench_accounts SET abalance = abalance + $1 WHERE aid = $2
 {pgbench_accounts} | SELECT abalance FROM pgbench_accounts WHERE aid = $1
 {pg_stat_monitor}  | select relations::oid[]::regclass[], cmd_type,resp_calls,query from pg_stat_monitor
 {pgbench_branches} | select count(*) from pgbench_branches
 {pgbench_history}  | INSERT INTO pgbench_history (tid, bid, aid, delta, mtime) VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP)
 {foo,bar}          | select * from foo,bar
(5 rows)
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
SELECT relations::oid[]::regclass[], query FROM pg_stat_monitor;
      relations      |                                                query                                                 
---------------------+------------------------------------------------------------------------------------------------------
 {test_view,foo,bar} | select * from test_view
 {foo,bar}           | select * from foo,bar
(2 rows)
```

#### Query command Type (SELECT, INSERT, UPDATE OR DELETE)

**`cmd_type`**: List the command type of the query.

```sql
SELECT substr(query,0, 50) AS query, cmd_type FROM pg_stat_monitor WHERE elevel = 0;
                       query                       |    cmd_type     
---------------------------------------------------+-----------------
 BEGIN                                             | {INSERT}
 vacuum pgbench_branches                           | {}
 UPDATE pgbench_branches SET bbalance = bbalance + | {UPDATE,SELECT}
 select substr(query,$1, $2) as query, cmd_type fr | {SELECT}
 select count(*) from pgbench_branches             | {SELECT}
 select substr(query,$1, $2) as query, cmd_type fr | {}
 UPDATE pgbench_accounts SET abalance = abalance + | {UPDATE,SELECT}
 update test SET a = $1                            | {UPDATE}
 END                                               | {INSERT}
 INSERT INTO pgbench_history (tid, bid, aid, delta | {INSERT}
 vacuum pgbench_tellers                            | {}
 SELECT a from test for update                     | {UPDATE,SELECT}
 SELECT abalance FROM pgbench_accounts WHERE aid = | {SELECT}
 UPDATE pgbench_tellers SET tbalance = tbalance +  | {UPDATE,SELECT}
 truncate pgbench_history                          | {}
(15 rows)
```
