# pg_stat_monitor - Statistics collector for PostgreSQL.

The pg_stat_monitor is the statistics collection tool based on PostgreSQL's contrib module ``pg_stat_statements``. PostgreSQL’s ``pg_stat_statements`` provides the basic statistics, which is sometimes not enough. The major shortcoming in ``pg_stat_statements`` is that it accumulates all the queries and their statistics and does not provide aggregated statistics nor histogram information. In this case, a user needs to calculate the aggregate which is quite expensive. 

``pg_stat_monitor`` is developed on the basis of ``pg_stat_statements`` as its more advanced replacement. It provides all the features of ``pg_stat_statements`` plus its own feature set.  

``pg_stat_monitor`` collects and aggregates data on a bucket basis. The size of a bucket and the number of buckets should be configured using GUC (Grand Unified Configuration). The flow is the following:

* ``pg_stat_monitor`` collects the statistics and aggregates it in a bucket.
* When a bucket time elapses, ``pg_stat_monitor`` resets all the statistics and switches to the next bucket. 
*   After the last bucket elapses, ``pg_stat_monitor`` goes back to the first bucket. All the data on the first bucket will vanish; therefore, users must read the buckets before that to not lose the data.


## Supported PostgreSQL Versions

The ``pg_stat_monitor`` should work on the latest version of PostgreSQL but is only tested with these PostgreSQL versions:

*   PostgreSQL Version 11
*   PostgreSQL Version 12
*   Percona Distribution for PostgreSQL 11 and 12


# Documentation

1. [Installation](#installation)
2. [Configuration](#configuration)
3. [Setup](#setup) 
4. [User Guide](#user-guide)
5. [License](#license)

**Copyright notice**

Copyright (c) 2006 - 2020, Percona LLC.


## Installation

There are two ways to install ``pg_stat_monitor``:
- by downloading the pg_stat_monitor source code and compiling it. 
- by downloading the ``deb`` or ``rpm`` packages.

**Compile from the source code**

The latest release of ``pg_stat_monitor`` can be downloaded from [this GitHub page](https://github.com/Percona/pg_stat_monitor/releases) or it can be downloaded using the git:

```
git clone git://github.com/Percona/pg_stat_monitor.git
```

After downloading the code, set the path for the PostgreSQL binary.

Compile and Install the extension

```
cd pg_stat_monitor

make USE_PGXS=1

make USE_PGXS=1 install
```

**Installing from rpm/deb packages**

``pg_stat_monitor`` is supplied as part of Percona Distribution for PostgreSQL. The rpm/deb packages are available from Percona repositories. Refer to [Percona Documentation](https://www.percona.com/doc/postgresql/LATEST/installing.html) for installation instructions.


## Configuration

Here is the complete list of configuration parameters.

```
postgres=# select * from pg_stat_monitor_settings;
                     name                      | value  | default_value |                            description                            | minimum |  maximum   | restart 
-----------------------------------------------+--------+---------------+-------------------------------------------------------------------+---------+------------+---------
 pg_stat_monitor.pgsm_max                      |   5000 |          5000 | Sets the maximum number of statements tracked by pg_stat_monitor. |    5000 | 2147483647 |       1
 pg_stat_monitor.pgsm_query_max_len            |   1024 |          1024 | Sets the maximum length of query.                                 |    1024 | 2147483647 |       1
 pg_stat_monitor.pgsm_enable                   |      1 |             1 | Enable/Disable statistics collector.                              |       0 |          0 |       1
 pg_stat_monitor.pgsm_track_utility            |      1 |             0 | Selects whether utility commands are tracked.                     |       0 |          0 |       0
 pg_stat_monitor.pgsm_normalized_query         |      1 |             0 | Selects whether save query in normalized format.                  |       0 |          0 |       0
 pg_stat_monitor.pgsm_max_buckets              |     10 |            10 | Sets the maximum number of buckets.                               |       1 |         10 |       1
 pg_stat_monitor.pgsm_bucket_time              |     60 |            60 | Sets the time in seconds per bucket.                              |       1 | 2147483647 |       1
 pg_stat_monitor.pgsm_object_cache             |     50 |            50 | Sets the maximum number of object cache                           |      50 | 2147483647 |       1
 pg_stat_monitor.pgsm_respose_time_lower_bound |      1 |             1 | Sets the time in millisecond.                                     |       1 | 2147483647 |       1
 pg_stat_monitor.pgsm_respose_time_step        |      1 |             1 | Sets the respose time steps in millisecond.                       |       1 | 2147483647 |       1
 pg_stat_monitor.pgsm_query_shared_buffer      | 500000 |        500000 | Sets the query shared_buffer size.                                |  500000 | 2147483647 |       1
(11 rows)
```


Some configuration parameters require the server restart and should be set before the server startup. These must be set in the ``postgresql.conf`` file. Other parameters do not require server restart and  can be set permanently either in the ``postgresql.conf`` or from the client (``psql``).

The table below shows set up options for each configuration parameter and whether the server restart is required to apply its value:


<table>
  <tr>
   <td rowspan="2" ><strong>Name</strong>
   </td>
   <td colspan="3" ><strong>Setup options</strong>
   </td>
   <td colspan="2" ><strong>Restart methods</strong>
   </td>
  </tr>
  <tr>
   <td>edit postgresql  conf
   </td>
   <td>set 
   </td>
   <td>alter system set 
   </td>
   <td>server restart
   </td>
   <td>configuration reload (via pg_reload_conf)
   </td>
  </tr>
  <tr>
   <td>pg_stat_monitor.pgsm_max
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
   <td>NO
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
  </tr>
  <tr>
   <td>pg_stat_monitor.pgsm_query_max_len
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
   <td>NO
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
  </tr>
  <tr>
   <td>pg_stat_monitor.pgsm_enable
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
   <td>NO
   </td>
   <td>NO
   </td>
   <td>YES
   </td>
  </tr>
  <tr>
   <td>pg_stat_monitor.pgsm_track_utility
   </td>
   <td>YES
   </td>
   <td>YES
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
   <td>YES
   </td>
  </tr>
  <tr>
   <td>pg_stat_monitor.pgsm_normalized_query
   </td>
   <td>YES
   </td>
   <td>YES
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
   <td>YES
   </td>
  </tr>
  <tr>
   <td>pg_stat_monitor.pgsm_max_buckets
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
   <td>NO
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
  </tr>
  <tr>
   <td>pg_stat_monitor.pgsm_bucket_time
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
   <td>NO
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
  </tr>
  <tr>
   <td>pg_stat_monitor.pgsm_object_cache
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
   <td>NO
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
  </tr>
  <tr>
   <td>pg_stat_monitor.pgsm_respose_time_lower_bound
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
   <td>NO
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
  </tr>
  <tr>
   <td>pg_stat_monitor.pgsm_respose_time_step
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
   <td>NO
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
  </tr>
  <tr>
   <td>pg_stat_monitor.pgsm_query_shared_buffer
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
   <td>NO
   </td>
   <td>YES
   </td>
   <td>NO
   </td>
  </tr>
</table>

### Parameters description:

- **pg_stat_monitor.pgsm_max**: This parameter defines the limit of shared memory for ``pg_stat_monitor``. This memory is used by buckets in a circular manner. The memory is divided between the buckets equally, at the start of the PostgreSQL. 
- **pg_stat_monitor.pgsm_query_max_len**: Sets the maximum size of the query. This parameter can only be set at the start of PostgreSQL. For long queries, the query is truncated to that particular length. This is to avoid unnecessary usage of shared memory.
- **pg_stat_monitor.pgsm_enable**: This parameter enables or disables the monitoring. "Disable" means that ``pg_stat_monitor`` will not collect the statistics for the whole cluster.
- **pg_stat_monitor.pgsm_track_utility**: This parameter controls whether utility commands are tracked by the module. Utility commands are all those other than ``SELECT``, ``INSERT``, ``UPDATE`` and ``DELETE``. 
- **pg_stat_monitor.pgsm_normalized_query**: By default, query shows the actual parameter instead of the placeholder. It is quite useful when users want to use that query and try to run that query to check the abnormalities. But in most cases users like the queries with a placeholder. This parameter is used to toggle between the two said options.
- **pg_stat_monitor.pgsm_max_buckets**: ``pg_stat_monitor`` accumulates the information in the form of buckets. All the aggregated information is bucket based. This parameter is used to set the number of buckets the system can have. For example, if this parameter is set to 2, then the system will create two buckets. First, the system will add all the information into the first bucket. After its lifetime (defined in the  pg_stat_monitor.pgsm_bucket_time parameter) expires, it will switch to the second bucket,  reset all the counters and repeat the process.
- **pg_stat_monitor.pgsm_bucket_time**: This parameter is used to set the lifetime of the bucket. System switches between buckets on the basis of ``pg_stat_monitor.pgsm_bucket_time``. 
- **pg_stat_monitor.pgsm_object_cache**: This parameter is used to store information about the objects in the query.  ``pg_stat_monitor`` saves the information used by the objects in the query. To limit that information, you can set the length of that memory.
- **pg_stat_monitor.pgsm_respose_time_lower_bound**: ``pg_stat_monitor`` also stores the execution time histogram. This parameter is used to set the lower bound of the histogram.
- **pg_stat_monitor.pgsm_respose_time_step:** This parameter is used to set the steps for the histogram. 

## Setup

``pg_stat_monitor`` cannot be installed in your running PostgreSQL instance. It should be set in the ``postgresql.conf`` file.

```
# - Shared Library Preloading -

shared_preload_libraries = 'pg_stat_monitor' # (change requires restart)
#local_preload_libraries = ''
#session_preload_libraries = ''
```

Or you can do from `psql` terminal using the ``alter system`` command.

``pg_stat_monitor`` needs to be loaded at the start time. This requires adding the  ``pg_stat_monitor`` extension for the ``shared_preload_libraries`` parameter and restart the PostgreSQL instance.

```
postgres=# alter system set shared_preload_libraries=pg_stat_monitor;
ALTER SYSTEM

sudo systemctl restart postgresql-11
```


Create the extension using the ``create extension`` command.


```
postgres=# create extension pg_stat_monitor;
CREATE EXTENSION
```

After doing that change, we need to restart the PostgreSQL server. PostgreSQL will start monitoring and collecting the statistics. 


##  User Guide

To view the statistics, there are multiple views available.


```
postgres=# \d pg_stat_monitor;
                          View "public.pg_stat_monitor"
       Column        |           Type           | Collation | Nullable | Default 
---------------------+--------------------------+-----------+----------+---------
 bucket              | integer                  |           |          | 
 bucket_start_time   | timestamp with time zone |           |          | 
 userid              | oid                      |           |          | 
 dbid                | oid                      |           |          | 
 client_ip           | inet                     |           |          | 
 queryid             | text                     |           |          | 
 query               | text                     |           |          | 
 plans               | bigint                   |           |          | 
 plan_total_time     | double precision         |           |          | 
 plan_min_timei      | double precision         |           |          | 
 plan_max_time       | double precision         |           |          | 
 plan_mean_time      | double precision         |           |          | 
 plan_stddev_time    | double precision         |           |          |  
 calls               | bigint                   |           |          | 
 total_time          | double precision         |           |          | 
 min_time            | double precision         |           |          | 
 max_time            | double precision         |           |          | 
 mean_time           | double precision         |           |          | 
 stddev_time         | double precision         |           |          | 
 rows                | bigint                   |           |          | 
 shared_blks_hit     | bigint                   |           |          | 
 shared_blks_read    | bigint                   |           |          | 
 shared_blks_dirtied | bigint                   |           |          | 
 shared_blks_written | bigint                   |           |          | 
 local_blks_hit      | bigint                   |           |          | 
 local_blks_read     | bigint                   |           |          | 
 local_blks_dirtied  | bigint                   |           |          | 
 local_blks_written  | bigint                   |           |          | 
 temp_blks_read      | bigint                   |           |          | 
 temp_blks_written   | bigint                   |           |          | 
 blk_read_time       | double precision         |           |          | 
 blk_write_time      | double precision         |           |          | 
 resp_calls          | text[]                   |           |          | 
 cpu_user_time       | double precision         |           |          | 
 cpu_sys_time        | double precision         |           |          | 
 tables_names        | text[]                   |           |          | 

```


**`bucket`**: ``pg_stat_monitor`` accumulates the statistics per bucket. All the information and aggregate reset for each bucket. The `bucket` will be a number showing the number of buckets for which this record belongs.

**`bucket_start_time`**: `bucket_start_time` shows the start time of the bucket. 


```
postgres=# select bucket, bucket_start_time, query from pg_stat_monitor;
 bucket |       bucket_start_time       |                            query                             
--------+-------------------------------+--------------------------------------------------------
2 | 2020-05-23 13:24:44.652415+00 | select * from pg_stat_monitor_reset()
3 | 2020-05-23 13:45:01.55658+00  | select bucket, bucket_start_time, query from pg_stat_monitor
2 | 2020-05-23 13:24:44.652415+00 | SELECT * FROM foo
(3 rows)
```

**`userid`**: An ID of the user whom  that query belongs. ``pg_stat_monitor`` is used to collect queries from all the users; therefore, `userid` is used to segregate the queries based on different users.

**`dbid`**: The database ID of the query. ``pg_stat_monitor`` accumulates queries from all the databases; therefore, this column is used to identify the database.

**`queryid`**:  ``pg_stat_monitor`` generates a unique ID for each query (queryid). 

**`query`**: The `query` column contains the actual text of the query. This parameter depends on the **`pg_stat_monitor.pgsm_normalized_query`** configuration parameters, in which format to show the query.

**`calls`**: Number of calls of that particular query.

```
postgres=# select userid,  dbid, queryid, query, calls from pg_stat_monitor;
 userid | dbid  |     queryid      |                            query                             | calls 
--------+-------+------------------+--------------------------------------------------------------+-------
     10 | 12696 | 56F12CE7CD01CF2C | select * from pg_stat_monitor_reset()                        |     1
     10 | 12696 | 748D9EC1F4CECB36 | select userid,  dbid, queryid, query from pg_stat_monitor    |     1
     10 | 12696 | 85900141D214EC52 | select bucket, bucket_start_time, query from pg_stat_monitor |     2
     10 | 12696 | F1AC132034D5B366 | SELECT * FROM foo                                            |     1
```
**`elevel`**, **`sqlcode`**,**`message`**,: error level / sql code and  log/warning/error message

```
postgres=# select substr(query,0,50) as query, decode_error_level(elevel)as elevel,sqlcode,calls, substr(message,0,50) message from pg_stat_monitor;
                       query                       | elevel | sqlcode | calls |                      message                      
---------------------------------------------------+--------+---------+-------+---------------------------------------------------
 select substr(query,$1,$2) as query, decode_error |        |       0 |     1 | 
 select bucket,substr(query,$1,$2),decode_error_le |        |       0 |     3 | 
                                                   | LOG    |       0 |     1 | database system is ready to accept connections
 select 1/0;                                       | ERROR  |     130 |     1 | division by zero
                                                   | LOG    |       0 |     1 | database system was shut down at 2020-11-11 11:37
 select $1/$2                                      |        |       0 |     1 | 
(6 rows)
```

**`total_time`**,  **`min_time`**, **`max_time`**, **`mean_time`**: The total / minimum / maximum and mean time spent for the same query.


```
postgres=# select userid,  total_time, min_time, max_time, mean_time, query from pg_stat_monitor;
 userid |     total_time     |      min_time      |      max_time      |     mean_time      |                              query                               
--------+--------------------+--------------------+--------------------+--------------------+------------------------------------------------------------------
     10 |           0.14|           0.14 |           0.14 |           0.14 | select * from pg_stat_monitor_reset()
     10 |           0.19 |           0.19 |           0.19 |           0.19 | select userid,  dbid, queryid, query from pg_stat_monitor
     10 |           0.30 |           0.13 |           0.16 |           0.15 | select bucket, bucket_start_time, query from pg_stat_monitor
     10 |           0.29 |           0.29 |           0.29 |           0.29 | select userid,  dbid, queryid, query, calls from pg_stat_monitor
     10 | 11277.79 | 11277.79 | 11277.79 | 11277.79| SELECT * FROM foo
```


**`client_ip`**: The IP address of the client that originated the query.

```
postgres=# select client_ip, query from pg_stat_monitor;
 client_ip |                                         query                                         
-----------+---------------------------------------------------------------------------------------
 127.0.0.1 | select * from pg_stat_monitor_reset()
 127.0.0.1 | select userid,  dbid, queryid, query from pg_stat_monitor
 127.0.0.1 | select bucket, bucket_start_time, query from pg_stat_monitor
 127.0.0.1 | select userid,  total_time, min_time, max_time, mean_time, query from pg_stat_monitor
 127.0.0.1 | select userid,  dbid, queryid, query, calls from pg_stat_monitor
 127.0.0.1 | SELECT * FROM foo
(6 rows)
```

**`resp_calls`**: Call histogram

```
postgres=# select resp_calls, query from pg_stat_monitor;
                    resp_calls                    |                 query                                         
--------------------------------------------------+---------------------------------------------- 
{1," 0"," 0"," 0"," 0"," 0"," 0"," 0"," 0"," 0"} | select client_ip, query from pg_stat_monitor
{3," 0"," 0"," 0"," 0"," 0"," 0"," 0"," 0"," 1"} | select * from pg_stat_monitor_reset()
{3," 0"," 0"," 0"," 0"," 0"," 0"," 0"," 0"," 1"} | SELECT * FROM foo
```

There are 10 timebase buckets of the time **`pg_stat_monitor.pgsm_respose_time_step`** in the field ``resp_calls``. The value in the field shows how many queries run in that period of time.

**`tables_names`**: The list of tables involved in the query

```
postgres=# select tables_names, query from pg_stat_monitor;
       tables_names       |                                         query                                         
--------------------------+---------------------------------------------------------------------------------------
 {public.pg_stat_monitor} | select client_ip, query from pg_stat_monitor
                          | select * from pg_stat_monitor_reset()
 {public.pg_stat_monitor} | select userid,  dbid, queryid, query from pg_stat_monitor
 {public.pg_stat_monitor} | select bucket, bucket_start_time, query from pg_stat_monitor
 {public.pg_stat_monitor} | select userid,  total_time, min_time, max_time, mean_time, query from pg_stat_monitor
 {public.pg_stat_monitor} | select userid,  dbid, queryid, query, calls from pg_stat_monitor
 {public.foo}             | SELECT * FROM foo
 {public.pg_stat_monitor} | select resp_calls, query from pg_stat_monitor
(8 rows)
```


**`wait_event`**: Current state of the query 

```
postgres=# select wait_event_type, query from pg_stat_monitor;
 wait_event |                                         query                                         
-----------------+-------------------------------------------------------------------------------
                 | select client_ip, query from pg_stat_monitor
 ClientRead      | select wait_event_type, query from pg_stat_monitor
                 | select * from pg_stat_monitor_reset()
```




