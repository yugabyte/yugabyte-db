# Comparing pg_stat_monitor and pg_stat_statements

The `pg_stat_monitor` extension is developed on the basis of `pg_stat_statements`  as its more advanced replacement.

Thus, `pg_stat_monitor` inherits the columns available in `pg_stat_statements` plus provides additional ones.

Note that [`pg_stat_monitor` and `pg_stat_statements` process statistics data differently](index.md#how-pg_stat_monitor-works). Because of these differences, memory blocks and WAL (Write Ahead Logs) related statistics data are displayed inconsistently when both extensions are used together. 


To see all available columns, run the following command from the `psql` terminal:

```sql
 postgres=# \d pg_stat_monitor;
```

The following table compares the `pg_stat_monitor` view with that of `pg_stat_statements`.

Note that the column names differ depending on the PostgreSQL version you are running.


| Column  name for PostgreSQL 13 and above | Column  name for PostgreSQL 11 and 12 |  pg_stat_monitor | pg_stat_statements
|--------------------|--------------------------|-----------------------------|----------------------
 bucket              | bucket                  |  :heavy_check_mark: | :x:
bucket_start_time    | bucket_start_time |  :heavy_check_mark: | :x:
userid             | userid        | :heavy_check_mark: | :heavy_check_mark:
datname            | datname           |  :heavy_check_mark: | :heavy_check_mark:
toplevel[^1]       |                   | :heavy_check_mark: | :heavy_check_mark:
client_ip          | client_ip         | :heavy_check_mark:| :x:
queryid            | queryid            | :heavy_check_mark: | :heavy_check_mark:
planid             | planid             | :heavy_check_mark:| :x:
query_plan         | query_plan          | :heavy_check_mark: | :x:
top_query          | top_query         | :heavy_check_mark: | :x:
top_queryid        | top_queryid        | :heavy_check_mark: | :x:
query              | query                 | :heavy_check_mark: | :heavy_check_mark:
application_name   | application_name   | :heavy_check_mark:| :x:
relations          | relations          | :heavy_check_mark: | :x:
cmd_type           | cmd_type           | :heavy_check_mark: | :x:
elevel             | elevel           | :heavy_check_mark: | :x:
sqlcode            | sqlcode | :heavy_check_mark: | :x:
message            | message | :heavy_check_mark: | :x:
plans_calls        | plans_calls  | :heavy_check_mark: | :heavy_check_mark:
total_plan_time    |               | :heavy_check_mark: | :heavy_check_mark:
min_plan_time      |               | :heavy_check_mark: | :heavy_check_mark:
max_plan_time      |               | :heavy_check_mark: | :heavy_check_mark:
mean_plan_time     |               | :heavy_check_mark:  | :heavy_check_mark:
stddev_plan_time   |               | :heavy_check_mark:  | :heavy_check_mark:
calls              | calls          | :heavy_check_mark: | :heavy_check_mark:
total_exec_time    | total_time     | :heavy_check_mark: | :heavy_check_mark:
min_exec_time      | min_time       | :heavy_check_mark: | :heavy_check_mark:
max_exec_time      | max_time       | :heavy_check_mark: | :heavy_check_mark:
mean_exec_time     | mean_time      | :heavy_check_mark: | :heavy_check_mark:
stddev_exec_time   | stddev_time | :heavy_check_mark: | :heavy_check_mark:
rows_retrieved     | rows_retrieved | :heavy_check_mark: | :heavy_check_mark:
shared_blks_hit    | shared_blks_hit | :heavy_check_mark: | :heavy_check_mark:
shared_blks_read   | shared_blks_read | :heavy_check_mark: | :heavy_check_mark:
shared_blks_dirtied | shared_blks_dirtied | :heavy_check_mark: | :heavy_check_mark:
shared_blks_written | shared_blks_written | :heavy_check_mark: | :heavy_check_mark:
local_blks_hit     | local_blks_hit | :heavy_check_mark: | :heavy_check_mark:
local_blks_read    | local_blks_read  | :heavy_check_mark: | :heavy_check_mark:
local_blks_dirtied | local_blks_dirtied  | :heavy_check_mark: | :heavy_check_mark:
local_blks_written | local_blks_written | :heavy_check_mark: | :heavy_check_mark:
temp_blks_read     | temp_blks_read | :heavy_check_mark: | :heavy_check_mark:
temp_blks_written  | temp_blks_written | :heavy_check_mark:  | :heavy_check_mark:
blk_read_time      | blk_read_time | :heavy_check_mark: | :heavy_check_mark:
blk_write_time     | blk_write_time | :heavy_check_mark: | :heavy_check_mark:
resp_calls         | resp_calls    | :heavy_check_mark: | :x:
cpu_user_time      | cpu_user_time | :heavy_check_mark: | :x:
cpu_sys_time       | cpu_sys_time | :heavy_check_mark: | :x:
wal_records         | wal_records | :heavy_check_mark:  | :heavy_check_mark:
wal_fpi             | wal_fpi     | :heavy_check_mark:  | :heavy_check_mark:
wal_bytes           | wal_bytes   | :heavy_check_mark:  | :heavy_check_mark:
state_code          | state_code  | :heavy_check_mark: | :x:
state               | state       | :heavy_check_mark: | :x:

To learn more about the features in `pg_stat_monitor`, please see the [User guide](https://github.com/percona/pg_stat_monitor/blob/master/docs/USER_GUIDE.md).


Additional reading: [pg_stat_statements](https://www.postgresql.org/docs/current/pgstatstatements.html)




[^1]: Available starting from PostgreSQL 14 and above
