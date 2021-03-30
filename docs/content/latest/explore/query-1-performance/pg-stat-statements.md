## Background
The database is a larger application, needs to consume a lot of memory CPU, IO, and network resources. SQL optimization is one of the methods of database optimization. First of all, we must understand the most resource-consuming SQL, namely TOP SQL. The `pg_stat_statements` module provides a means for tracking planning and execution statistics of all SQL statements executed by a server. If you use yugabyte and you haven’t yet used pg_stat_statements, it is a must to add it to your toolbox. And even if you are familiar, it may be worth a revisit.

## Install pg_stat_statements module/extension

It is installed by default.

##  Load the pg_stat_statements module/extension

In  `var/var/data/pg_data/postgresql.conf` of yugabyte home directory,  the `hared_preload_libraries` configration parameters should be modified.

```sql
yugabyte=# show shared_preload_libraries;
         shared_preload_libraries         
------------------------------------------
 pg_stat_statements,yb_pg_metrics,pgaudit
(1 row)
```
If you want to track IO elapsed time, you also need to turn on the following parameters  in `postgresql.conf`

```shell
track_io_timing = on
```

Set the maximum length of a single SQL to exceed the truncated display (optional) in `postgresql.conf`

```shell
track_activity_query_size = 2048 
```

## Configure the pg_stat_statements sampling parameters in postgresql.conf

```shell
pg_stat_statements.max = 10000      
pg_stat_statements.track = all 
pg_stat_statements.track_utility = off  
pg_stat_statements.save = on  
```

## Create pg_stat_statements extension

```shell
yugabyte=# drop extension pg_stat_statements;
DROP EXTENSION
yugabyte=# create extension pg_stat_statements;
CREATE EXTENSION
```

## Restart yugabyte database

```shell
$ yugabyted start
```

## Analyze the TOP SQL

```shell
yugabyte=# \d pg_stat_statements;
                    View "public.pg_stat_statements"
       Column        |       Type       | Collation | Nullable | Default 
---------------------+------------------+-----------+----------+---------
 userid              | oid              |           |          | 
 dbid                | oid              |           |          | 
 queryid             | bigint           |           |          | 
 query               | text             |           |          | 
 calls               | bigint           |           |          | 
 total_time          | double precision |           |          | 
 min_time            | double precision |           |          | 
 max_time            | double precision |           |          | 
 mean_time           | double precision |           |          | 
 stddev_time         | double precision |           |          | 
 rows                | bigint           |           |          | 
 shared_blks_hit     | bigint           |           |          | 
 shared_blks_read    | bigint           |           |          | 
 shared_blks_dirtied | bigint           |           |          | 
 shared_blks_written | bigint           |           |          | 
 local_blks_hit      | bigint           |           |          | 
 local_blks_read     | bigint           |           |          | 
 local_blks_dirtied  | bigint           |           |          | 
 local_blks_written  | bigint           |           |          | 
 temp_blks_read      | bigint           |           |          | 
 temp_blks_written   | bigint           |           |          | 
 blk_read_time       | double precision |           |          | 
 blk_write_time      | double precision |           |          | 
 
 
 yugabyte=# \dS+ pg_stat_statements;
                                 View "public.pg_stat_statements"
       Column        |       Type       | Collation | Nullable | Default | Storage  | Description 
---------------------+------------------+-----------+----------+---------+----------+-------------
 userid              | oid              |           |          |         | plain    | 
 dbid                | oid              |           |          |         | plain    | 
 queryid             | bigint           |           |          |         | plain    | 
 query               | text             |           |          |         | extended | 
 calls               | bigint           |           |          |         | plain    | 
 total_time          | double precision |           |          |         | plain    | 
 min_time            | double precision |           |          |         | plain    | 
 max_time            | double precision |           |          |         | plain    | 
 mean_time           | double precision |           |          |         | plain    | 
 stddev_time         | double precision |           |          |         | plain    | 
 rows                | bigint           |           |          |         | plain    | 
 shared_blks_hit     | bigint           |           |          |         | plain    | 
 shared_blks_read    | bigint           |           |          |         | plain    | 
 shared_blks_dirtied | bigint           |           |          |         | plain    | 
 shared_blks_written | bigint           |           |          |         | plain    | 
 local_blks_hit      | bigint           |           |          |         | plain    | 
 local_blks_read     | bigint           |           |          |         | plain    | 
 local_blks_dirtied  | bigint           |           |          |         | plain    | 
 local_blks_written  | bigint           |           |          |         | plain    | 
 temp_blks_read      | bigint           |           |          |         | plain    | 
 temp_blks_written   | bigint           |           |          |         | plain    | 
 blk_read_time       | double precision |           |          |         | plain    | 
 blk_write_time      | double precision |           |          |         | plain    | 
View definition:
 SELECT pg_stat_statements.userid,
    pg_stat_statements.dbid,
    pg_stat_statements.queryid,
    pg_stat_statements.query,
    pg_stat_statements.calls,
    pg_stat_statements.total_time,
    pg_stat_statements.min_time,
    pg_stat_statements.max_time,
    pg_stat_statements.mean_time,
    pg_stat_statements.stddev_time,
    pg_stat_statements.rows,
    pg_stat_statements.shared_blks_hit,
    pg_stat_statements.shared_blks_read,
    pg_stat_statements.shared_blks_dirtied,
    pg_stat_statements.shared_blks_written,
    pg_stat_statements.local_blks_hit,
    pg_stat_statements.local_blks_read,
    pg_stat_statements.local_blks_dirtied,
    pg_stat_statements.local_blks_written,
    pg_stat_statements.temp_blks_read,
    pg_stat_statements.temp_blks_written,
    pg_stat_statements.blk_read_time,
    pg_stat_statements.blk_write_time
   FROM pg_stat_statements(true) pg_stat_statements(userid, dbid, queryid, query, calls, total_time, min_time, max_time, mean_time, stddev_time, rows, shared_blks_hit, shared_blks_read, shared_blks_dirtied, shared_blks_written, local_blks_hit, local_blks_read, local_blks_dirtied, local_blks_written, temp_blks_read, temp_blks_written, blk_read_time, blk_write_time);
```

### Example 1: The most consumption IO TOP 10 SQL

```sql
select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time)/calls desc limit 10;    -- mean
select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time) desc limit 10;     -- total
```

### Example 2: The most time-consuming TOP 10 SQL

```sql
select userid::regrole, dbid, query from pg_stat_statements order by mean_time desc limit 10;    -- mean time
select userid::regrole, dbid, query from pg_stat_statements order by total_time desc limit 10;      -- total time
```

### Example 3: The most serious response time jitter TOP 10 SQL
```sql
select userid::regrole, dbid, query from pg_stat_statements order by stddev_time desc limit 10;  
```

### Example 4: The most consuming shared memory TOP 10 SQL

```sql
select userid::regrole, dbid, query from pg_stat_statements order by (shared_blks_hit+shared_blks_dirtied) desc limit 10;
```

### Example 5: The most temporary space consuming TOP 10 SQL
```sql
select userid::regrole, dbid, query from pg_stat_statements order by temp_blks_written desc limit 10;    
```

## Reset statistics

```sql
select pg_stat_statements_reset();   
```
