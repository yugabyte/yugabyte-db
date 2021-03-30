## Background
The database is a larger application, needs to consume a lot of memory CPU, IO, and network resources. SQL optimization is one of the methods of database optimization. First of all, we must understand the most resource-consuming SQL, namely TOP SQL. The pg_stat_statements module provides a means for tracking planning and execution statistics of all SQL statements executed by a server. If you use yugabyte and you haven’t yet used pg_stat_statements, it is a must to add it to your toolbox. And even if you are familiar, it may be worth a revisit.

## Install pg_stat_statements

It is installed by default.

##  Load the pg_stat_statements module

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

Set the maximum length of a single SQL to exceed the truncated display (optional) in `postgresql.conf`, for example:

```shell
track_activity_query_size = 2048 
```

## Configure the pg_stat_statements sampling parameters in postgresql.conf, for example:

```shell
pg_stat_statements.max = 10000      
pg_stat_statements.track = all 
pg_stat_statements.track_utility = off  
pg_stat_statements.save = on  
```

## Create pg_stat_statements extension

```sql
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

```sql
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
yugabyte=# \x
```
### The most consumption IO TOP 10 SQL

```sql
select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time)/calls desc limit 10;    -- mean
select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time) desc limit 10;     -- total
```

### The most time-consuming TOP 10 SQL

```sql
select userid::regrole, dbid, query from pg_stat_statements order by mean_time desc limit 10;    -- mean time
select userid::regrole, dbid, query from pg_stat_statements order by total_time desc limit 10;      -- total time
```

### The most serious response time jitter TOP 10 SQL
```sql
select userid::regrole, dbid, query from pg_stat_statements order by stddev_time desc limit 10;  
```

### The most consuming shared memory TOP 10 SQL

```sql
select userid::regrole, dbid, query from pg_stat_statements order by (shared_blks_hit+shared_blks_dirtied) desc limit 10;
```

### The most temporary space consuming TOP 10 SQL
```sql
select userid::regrole, dbid, query from pg_stat_statements order by temp_blks_written desc limit 10;    
```

## Reset statistics

```sql
select pg_stat_statements_reset();   
```
