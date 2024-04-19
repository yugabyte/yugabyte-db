---
title: Get query statistics using pg_stat_statements
linkTitle: Get query statistics
description: Track planning and execution statistics for all SQL statements executed by a server.
headerTitle: Get query statistics using pg_stat_statements
menu:
  preview:
    identifier: pg-stat-statements
    parent: query-tuning
    weight: 200
type: docs
---

Databases can be resource-intensive, consuming a lot of memory CPU, IO, and network resources. Optimizing your SQL can be very helpful in minimizing resource utilization. The `pg_stat_statements` module helps you track planning and execution statistics for all the SQL statements executed by a server. It is installed by default.

The columns of the `pg_stat_statements` view are described in the following table.

| Column            |       Type       | Description |
| :----             | :---             | :---        |
userid              | oid              | OID of user who executed the statement |
dbid                | oid              | OID of database in which the statement was executed |
queryid             | bigint           | Internal hash code, computed from the statement's parse tree |
query               | text             | Text of a representative statement |
calls               | bigint           | Number of times executed |
total_time          | double precision | Total time spent in the statement, in milliseconds |
min_time            | double precision | Minimum time spent in the statement, in milliseconds |
max_time            | double precision | Maximum time spent in the statement, in milliseconds |
mean_time           | double precision | Mean time spent in the statement, in milliseconds |
stddev_time         | double precision | Population standard deviation of time spent in the statement, in milliseconds |
rows                | bigint           | Total number of rows retrieved or affected by the statement |
shared_blks_hit     | bigint           | Total number of shared block cache hits by the statement |
shared_blks_read    | bigint           | Total number of shared blocks read by the statement |
shared_blks_dirtied | bigint           | Total number of shared blocks dirtied by the statement |
shared_blks_written | bigint           | Total number of shared blocks written by the statement |
local_blks_hit      | bigint           | Total number of local block cache hits by the statement |
local_blks_read     | bigint           | Total number of local blocks read by the statement |
local_blks_dirtied  | bigint           | Total number of local blocks dirtied by the statement |
local_blks_written  | bigint           | Total number of local blocks written by the statement |
temp_blks_read      | bigint           | Total number of temp blocks read by the statement |
temp_blks_written   | bigint           | Total number of temp blocks written by the statement |
blk_read_time       | double precision | Total time the statement spent reading blocks, in milliseconds (if track_io_timing is enabled, otherwise zero) |
blk_write_time      | double precision | Total time the statement spent writing blocks, in milliseconds (if track_io_timing is enabled, otherwise zero) |
yb_latency_histogram | jsonb           | List of key value pairs where key is the latency range and value is the count of times a query was executed |

## Configuration parameters

You can configure the following parameters in `postgresql.conf`:

| Column | Type | Default | Description |
| :----- | :--- | :------ | :---------- |
| `pg_stat_statements.max` | integer | 5000 | Maximum number of statements tracked by the module. |
| `pg_stat_statements.track` | enum | top | Controls which statements the module tracks. Valid values are `top` (track statements issued directly by clients), `all` (track top-level and nested statements), and `none` (disable statement statistics collection). |
| `pg_stat_statements.track_utility` | boolean | on | Controls whether the module tracks utility commands. |
| `pg_stat_statements.save` | boolean | on | Specifies whether to save statement statistics across server shutdowns. |
| `pg_stat_statements.yb_hdr_bucket_factor` | integer | 16 | Changes the number of latency range buckets. |

The module requires additional shared memory proportional to `pg_stat_statements.max`. Note that this memory is consumed whenever the module is loaded, even if `pg_stat_statements.track` is set to `none`.

```sh
pg_stat_statements.max = 10000
pg_stat_statements.track = all
pg_stat_statements.track_utility = off
pg_stat_statements.save = on
```

To track IO elapsed time, turn on the `track_io_timing` parameter in `postgresql.conf`:

```sh
track_io_timing = on
```

The `track_activity_query_size` parameter sets the number of characters to display when reporting a SQL query. Raise this value if you're not seeing longer queries in their entirety. For example:

```sh
track_activity_query_size = 2048
```

The extension is created by default. To add or remove it manually, use the following statements:

```sql
yugabyte=# create extension pg_stat_statements;
```

```sql
yugabyte=# drop extension pg_stat_statements;
```

## yb_latency_histogram column

Each row in the `pg_stat_statements` view in YugabyteDB includes the `yb_latency_histogram` column, of type JSONB.

`yb_latency_histogram` consists of a list of key value pairs where key is the latency range and value is the count of times a query was executed. After each query execution, based on the execution time of the query, the execution counter for a specific key (latency range) for that query is incremented. Each key field represents time span in milliseconds (ms). For the resolution of each latency range, refer to [Latency range buckets](#latency-range-buckets).

The value associated with each key is the count of that query's execution latencies falling in that time span. The time spans are beginning-inclusive and end-exclusive. For example, `{"[12.8, 25.6)": 4}` means the query has been recorded as taking between 12.8 ms and 25.6 ms, 4 times.

Queries taking longer than the max query duration (default 1677721.5 ms or ~28 min) are recorded in the overflow bucket. The overflow bucket has the format `{"[max_latency,)": n}`, indicating that it covers all values beyond the scope of the histogram. Currently the `max_latency` tracked is 1677721.6 ms.

To view latency histograms, use the following command:

```sql
yugabyte=# select query, calls, yb_latency_histogram from pg_stat_statements;
```

```output
-[ RECORD 1 ]-----------------------------------------------------------------
query                | insert into table1 values($1, $2)
calls                | 6
yb_latency_histogram | [{"[0.1,0.2)": 4}, {"[0.2,0.3)": 2}]
-[ RECORD 2 ]-----------------------------------------------------------------
query                | select pg_sleep($1)
calls                | 9
yb_latency_histogram | [{"[0.8,0.9)": 6}, {"[1677721.6,)": 3}]
```

In this example, Record 1 shows that the `insert` query took between 0.1 to 0.2 ms 4 times and 0.2 to 0.3 ms 2 times. Record 2 shows that the `pg_sleep`  query took between 0.8 to 0.9 ms 6 times and 3 times it took more than 1677721.6 ms.

### Latency range buckets

The following table shows the time resolution for each latency range. For example, for queries finishing between 25.6-51.2 the resolution of the bucket is 3.2ms. If the query finishes in 28 ms then it is recorded in the sub-bucket `{25.6, 28.8)`.

Time Span (ms) | Resolution (ms)
| :--- | :--- |
| 0-1.6 | 0.1
1.6-3.2 | 0.2
3.2-6.4 | 0.4
6.4-12.8 | 0.8
12.8-25.6 | 1.6
25.6-51.2 | 3.2
51.2-102.4 | 6.4
102.4-204.8 | 12.8
204.8-409.6 | 25.6
409.6-819.2 | 51.2
819.2-1638.4 | 102.4
1638.4-3276.8 | 204.8
3276.8-6553.6 | 409.6
6553.6-13107.2 | 819.2
13107.2-26214.4 | 1638.4
26214.4-52428.8 | 3276.8
52428.8-104857.6 | 6553.6
104857.6-209715.2 (104.8-209.7 s) | 13107.2 (~13s)
209715.2-419430.4 (3.49-6.99 min) | 26214.4 (~26s)
419430.4-838860.8 (6.99-13.98 min) | 52428.8 (~52s)
838860.8-1677721.6 (13.98-27.96 min) | 104857.6 (~104s)

You can change the number of buckets using the `yb_hdr_bucket_factor` parameter in the `postgresql.conf` file. Valid values are 8, 16 (default), or 32. For example:

```sh
pg_stat_statements.yb_hdr_bucket_factor = 32
```

This changes the buckets as shown in the following table.

Time Span (ms) | Resolution (ms)
| :--- | :--- |
| 0-3.2 | 0.1
3.2-6.4 | 0.2
6.4-12.8 | 0.4
12.8-25.6 | 0.8
25.6-51.2 | 1.6
51.2-102.4 | 3.2
102.4-204.8 | 6.4
204.8-409.6 | 12.8
409.6-819.2 | 25.6
819.2-1638.4 | 51.2
... |
209715.2-419430.4 (3.49-6.99 min) | 13107.2
419430.4-838860.8 (6.99-13.98 min) | 26214.4
838860.8-1677721.6 (13.98-27.96 min) | 52428.8

### Show latency percentiles

Use the `yb_get_percentile` function to show percentiles for a given histogram. This function takes a JSONB histogram (including `yb_latency_histogram`) and a target percentile (double precision) as input.

```sql
\df yb_get_percentile
```

```output
   Schema   |       Name        | Result data type |           Argument data types           | Type
----------- +-------------------+------------------+-----------------------------------------+------
 pg_catalog | yb_get_percentile | double precision | hist jsonb, percentile double precision | func
(1 row)
```

The following examples show how to display various percentile latencies for a given JSONB histogram.

```sql
SELECT yb_get_percentile('[{"[0.1,0.2)": 4}, {"[0.2,0.3)": 1}, {"[0.8,0.9)": 1}, {"[1.1,1.2)": 3}, {"[1.5,1.6)": 1}, {"[2.0,2.2)": 4}, {"[3.2,3.6)": 1}]', 99);
```

```output
 yb_get_percentile
-------------------
               3.6
(1 row)
```

```sql
SELECT yb_get_percentile('[{"[0.1,0.2)": 4}, {"[0.2,0.3)": 1}, {"[0.8,0.9)": 1}, {"[1.1,1.2)": 3}, {"[1.5,1.6)": 1}, {"[2.0,2.2)": 4}, {"[3.2,3.6)": 1}]', 90);
```

```output
 yb_get_percentile
-------------------
               2.2
(1 row)
```

```sql
SELECT yb_get_percentile('[{"[0.1,0.2)": 4}, {"[0.2,0.3)": 1}, {"[0.8,0.9)": 1}, {"[1.1,1.2)": 3}, {"[1.5,1.6)": 1}, {"[2.0,2.2)": 4}, {"[3.2,3.6)": 1}]', 50);
```

```output
 yb_get_percentile
-------------------
               1.2
(1 row)
```

The following example displays the P99, P95, and P90 latency by augmenting the `pg_stat_statements` output for a specific query:

```sql
SELECT query, calls, total_time, min_time, max_time, mean_time, rows,
  yb_latency_histogram,
  yb_get_percentile(yb_latency_histogram, 99),
  yb_get_percentile(yb_latency_histogram, 95),
  yb_get_percentile(yb_latency_histogram, 90),
  FROM pg_stat_statements WHERE query like '%select v from foo where customer_id%';
```

```output
query                | select v from foo where customer_id=$1 and day=$2::date for update
calls                | 33904964
total_time           | 1122539431.94119
min_time             | 1.005537
max_time             | 524.190576
mean_time            | 33.1084094925296
rows                 | 33904964
yb_latency_histogram | [{"[1.0,1.1)": 7658}, {"[1.1,1.2)": 326051}, {"[1.2,1.3)": 984139}, {"[1.3,1.4)": 862572}, {"[1.4,1.5)": 604798}, {"[1.5,1.6)": 662095}, {"[1.6,1.8)": 1258824}, {"[1.8,2.0)": 951168}, {"[2.0,2.2)": 373974}, {"[2.2,2.4)": 107085}, {"[2.4,2.6)": 64638}, {"[2.6,2.8)": 73508}, {"[2.8,3.0)": 81727}, {"[3.0,3.2)": 92673}, {"[3.2,3.6)": 199462}, {"[3.6,4.0)": 216870}, {"[4.0,4.4)": 188390}, {"[4.4,4.8)": 212072}, {"[4.8,5.2)": 249788}, {"[5.2,5.6)": 274928}, {"[5.6,6.0)": 329839}, {"[6.0,6.4)": 484011}, {"[6.4,7.2)": 1198917}, {"[7.2,8.0)": 830484}, {"[8.0,8.8)": 572708}, {"[8.8,9.6)": 451159}, {"[9.6,10.4)": 395841}, {"[10.4,11.2)": 373503}, {"[11.2,12.0)": 451987}, {"[12.0,12.8)": 531331}, {"[12.8,14.4)": 770149}, {"[14.4,16.0)": 632207}, {"[16.0,17.6)": 533134}, {"[17.6,19.2)": 486974}, {"[19.2,20.8)": 426922}, {"[20.8,22.4)": 517026}, {"[22.4,24.0)": 474835}, {"[24.0,25.6)": 419358}, {"[25.6,28.8)": 893763}, {"[28.8,32.0)": 852830}, {"[32.0,35.2)": 819699}, {"[35.2,38.4)": 809411}, {"[38.4,41.6)": 744626}, {"[41.6,44.8)": 769875}, {"[44.8,48.0)": 713671}, {"[48.0,51.2)": 704817}, {"[51.2,57.6)": 1373824}, {"[57.6,64.0)": 1376515}, {"[64.0,70.4)": 1402915}, {"[70.4,76.8)": 1434799}, {"[76.8,83.2)": 1426155}, {"[83.2,89.6)": 1263072}, {"[89.6,96.0)": 919922}, {"[96.0,102.4)": 492696}, {"[102.4,115.2)": 222437}, {"[115.2,128.0)": 10392}, {"[128.0,140.8)": 451}, {"[140.8,153.6)": 146}, {"[153.6,166.4)": 78}, {"[166.4,179.2)": 22}, {"[179.2,192.0)": 1}, {"[307.2,332.8)": 3}, {"[332.8,358.4)": 6}, {"[358.4,384.0)": 3}, {"[384.0,409.6)": 7}, {"[409.6,460.8)": 6}, {"[460.8,512.0)": 15}, {"[512.0,563.2)": 2}]
p99                  | 102.4
p95                  | 89.6
p90                  | 83.2
```

## Examples

{{% explore-setup-single %}}

Describe the columns in the view:

```sql
yugabyte=# \d pg_stat_statements;
```

```output
                     View "public.pg_stat_statements"
       Column         |       Type       | Collation | Nullable | Default
----------------------+------------------+-----------+----------+---------
 userid               | oid              |           |          |
 dbid                 | oid              |           |          |
 queryid              | bigint           |           |          |
 query                | text             |           |          |
 calls                | bigint           |           |          |
 total_time           | double precision |           |          |
 min_time             | double precision |           |          |
 max_time             | double precision |           |          |
 mean_time            | double precision |           |          |
 stddev_time          | double precision |           |          |
 rows                 | bigint           |           |          |
 shared_blks_hit      | bigint           |           |          |
 shared_blks_read     | bigint           |           |          |
 shared_blks_dirtied  | bigint           |           |          |
 shared_blks_written  | bigint           |           |          |
 local_blks_hit       | bigint           |           |          |
 local_blks_read      | bigint           |           |          |
 local_blks_dirtied   | bigint           |           |          |
 local_blks_written   | bigint           |           |          |
 temp_blks_read       | bigint           |           |          |
 temp_blks_written    | bigint           |           |          |
 blk_read_time        | double precision |           |          |
 blk_write_time       | double precision |           |          |
 yb_latency_histogram | jsonb            |           |          |
```

### Top 10 I/O-intensive queries

```sql
yugabyte=# select userid::regrole, dbid, query
    from pg_stat_statements
    order by (blk_read_time+blk_write_time)/calls desc
    limit 10;
```

```output
  userid  | dbid  |                                                          query
----------+-------+--------------------------------------------------------------------------------------------------------
 yugabyte | 12463 | select pg_stat_statements_reset()
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time)/cal
ls desc limit $1
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by total_time desc limit $1
(3 rows)
```

```sql
yugabyte=# select userid::regrole, dbid, query
    from pg_stat_statements
    order by (blk_read_time+blk_write_time) desc
    limit 10;
```

```output
  userid  | dbid  |                                                          query
----------+-------+--------------------------------------------------------------------------------------------------------
 yugabyte | 12463 | select pg_stat_statements_reset()
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time)/cal
ls desc limit $1
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time) des
c limit $1
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by total_time desc limit $1
(4 rows)
```

### Top 10 time-consuming queries

```sql
yugabyte=# select userid::regrole, dbid, query
    from pg_stat_statements
    order by mean_time desc
    limit 10;
```

```output
  userid  | dbid  |                                                          query
----------+-------+--------------------------------------------------------------------------------------------------------
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time)/cal
ls desc limit $1
 yugabyte | 12463 | select pg_stat_statements_reset()
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by total_time desc limit $1
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time) des
c limit $1
(4 rows)
```

```sql
yugabyte=# select userid::regrole, dbid, query
    from pg_stat_statements
    order by total_time desc
    limit 10;
```

```output
  userid  | dbid  |                                                          query
----------+-------+--------------------------------------------------------------------------------------------------------
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time)/cal
ls desc limit $1
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time) des
c limit $1
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by mean_time desc limit $1
 yugabyte | 12463 | select pg_stat_statements_reset()
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by total_time desc limit $1
(5 rows)
```

### Top 10 response-time outliers

```sql
yugabyte=# select userid::regrole, dbid, query
    from pg_stat_statements
    order by stddev_time desc
    limit 10;
```

```output
  userid  | dbid  |                                                          query
----------+-------+--------------------------------------------------------------------------------------------------------
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time)/cal
ls desc limit $1
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by total_time desc limit $1
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time) des
c limit $1
 yugabyte | 12463 | select pg_stat_statements_reset()
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by mean_time desc limit $1
(5 rows)
```

### Top 10 queries by memory usage

```sql
yugabyte=# select userid::regrole, dbid, query
    from pg_stat_statements
    order by (shared_blks_hit+shared_blks_dirtied) desc
    limit 10;
```

```output
  userid  | dbid  |                                                          query
----------+-------+--------------------------------------------------------------------------------------------------------
 yugabyte | 12463 | select pg_stat_statements_reset()
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by stddev_time desc limit $1
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time)/cal
ls desc limit $1
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time) des
c limit $1
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by total_time desc limit $1
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by mean_time desc limit $1
(6 rows)
```

### Top 10 consumers of temporary space

```sql
yugabyte=# select userid::regrole, dbid, query
    from pg_stat_statements
    order by temp_blks_written desc
    limit 10;
```

```output
  userid  | dbid  |                                                          query
----------+-------+--------------------------------------------------------------------------------------------------------
 yugabyte | 12463 | select pg_stat_statements_reset()
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by stddev_time desc limit $1
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time)/cal
ls desc limit $1
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time) des
c limit $1
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by total_time desc limit $1
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by mean_time desc limit $1
 yugabyte | 12463 | select userid::regrole, dbid, query from pg_stat_statements order by (shared_blks_hit+shared_blks_dirti
ed) desc limit $1
(7 rows)
```

## Reset statistics

`pg_stat_statements_reset` discards all statistics gathered so far by pg_stat_statements. By default, this function can only be executed by superusers.

```sql
yugabyte=# select pg_stat_statements_reset();
```

```output
 pg_stat_statements_reset
--------------------------

(1 row)
```

## Learn more

- [Latency histogram and P99 latencies](../../../yugabyte-platform/alerts-monitoring/latency-histogram/) in YugabyteDB Anywhere
- [Active Session History](../../observability/active-session-history)
