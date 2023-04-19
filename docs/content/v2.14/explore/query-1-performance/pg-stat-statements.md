---
title: Get query statistics using pg_stat_statements
linkTitle: Get query statistics
description: Track planning and execution statistics for all SQL statements executed by a server.
headerTitle: Get query statistics using pg_stat_statements
image: /images/section_icons/index/develop.png
menu:
  v2.14:
    identifier: pg-stat-statements
    parent: query-tuning
    weight: 200
type: docs
---

Databases can be resource-intensive, consuming a lot of memory CPU, IO, and network resources. Optimizing your SQL can be very helpful in minimizing resource utilization. The `pg_stat_statements` module helps you track planning and execution statistics for all the SQL statements executed by a server. It is installed by default.

## Configuration parameters

You can configure the following parameters in `postgresql.conf`:

| Column | Type | Default | Description |
| :----- | :--- | :------ | :---------- |
| `pg_stat_statements.max` | integer | 5000 | Maximum number of statements tracked by the module. |
| `pg_stat_statements.track` | enum | top | Controls which statements the module tracks. Valid values are `top` (track statements issued directly by clients), `all` (track top-level and nested statements), and `none` (disable statement statistics collection). |
| `pg_stat_statements.track_utility` | boolean | on | Controls whether the module tracks utility commands. |
| `pg_stat_statements.save` | boolean | on | Specifies whether to save statement statistics across server shutdowns. |

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

## Examples

Describe the columns in the view:

```sql
yugabyte=# \d pg_stat_statements;
```

```output
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
 ```

Describe the view's columns, including storage type and description:

 ```sql
 yugabyte=# \dS+ pg_stat_statements;
 ```

 ```output
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

- Refer to [View live queries with pg_stat_activity](../pg-stat-activity/) to analyze live queries.
- Refer to [View COPY progress with pg_stat_progress_copy](../pg-stat-progress-copy/) to track the COPY operation status.
- Refer to [Analyze queries with EXPLAIN](../explain-analyze/) to optimize YSQL's EXPLAIN and EXPLAIN ANALYZE queries.
- Refer to [Optimize YSQL queries using pg_hint_plan](../pg-hint-plan/) show the query execution plan generated by YSQL.
