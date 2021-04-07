---
title: Get query statistics using pg_stat_statements
linkTitle: Get query statistics using pg_stat_statements
description: Track planning and execution statistics for all SQL statements executed by a server.
headerTitle: Get query statistics using pg_stat_statements
image: /images/section_icons/index/develop.png
menu:
  latest:
    identifier: pg_stat_statements
    parent: query-1-performance
    weight: 600
isTocNested: true
showAsideToc: true
---

Databases can be resource-intensive, consuming a lot of memory CPU, IO, and network resources. Optimizing your SQL can be very helpful in minimizing resource utilization. The `pg_stat_statements` module helps you track planning and execution statistics for all the SQL statements executed by a server. It is installed by default.


## Install pg_stat_statements module/extension


Open `var/data/pg_data/postgresql.conf` for your YugabyteDB instance in a text editor, and modify the `shared_preload_libraries` parameter. To load only this extension, the line would look like this: 


```text
shared_preload_libraries = 'pg_stat_statements'  # (change requires restart)
```

Note that the `var` directory is located in your home directory by default; use `bin/yugabyted status` to check your instance's configuration.

Check your instance's `shared_preload_libraries` setting in YSQL with the following command:

```sql
yugabyte=# show shared_preload_libraries;
         shared_preload_libraries         
------------------------------------------
 pg_stat_statements,yb_pg_metrics,pgaudit
(1 row)
```
To track IO elapsed time, turn on the `track_io_timing` parameter  in `postgresql.conf`:

```sh
track_io_timing = on
```

The `track_activity_query_size` parameter sets the number of characters to display when reporting a SQL query. Raise this value if you're not seeing longer queries in their entirety. For example:

```sh
track_activity_query_size = 2048 
```

## Configuration parameters

|Column|Type|Default|Description|
|:----:|:----:|:----:|:----:|
|pg_stat_statements.max|integer|5000|It is the maximum number of statements tracked by the module.|
|pg_stat_statements.track|enum|top|It controls which statements are counted by the module.|
|pg_stat_statements.track_utility|boolean|on|It controls whether utility commands are tracked by the module.|
|pg_stat_statements.save|boolean|on|It specifies whether to save statement statistics across server shutdowns.|


The module requires additional shared memory proportional to pg_stat_statements.max. Note that this memory is consumed whenever the module is loaded, even if pg_stat_statements.track is set to none.

You can configure the following parameters in `postgresql.conf`:

```sh
pg_stat_statements.max = 10000      
pg_stat_statements.track = all 
pg_stat_statements.track_utility = off  
pg_stat_statements.save = on  
```

## Create pg_stat_statements extension

Loading/unloading pg_stat_statements extension.

```sh
yugabyte=# create extension pg_stat_statements;
CREATE EXTENSION
yugabyte=# drop extension pg_stat_statements;
DROP EXTENSION
```

## Restart YugabyteDB

Restart YugabyteDB with the following command:

```sh
$ bin/yugabyted stop && bin/yugabyted start
```

## Examples


```sh
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

### Top 10 I/O-intensive queries

```sh
yugabyte=# \x
Expanded display is on.
yugabyte=# select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time)/calls desc limit 10;    -- mean
-[ RECORD 1 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT r.rulename, trim(trailing $1 from pg_catalog.pg_get_ruledef(r.oid, $2))                                    
                                                                                                                           
                                            +
       | FROM pg_catalog.pg_rewrite r                                                                                      
                                                                                                                           
                                            +
       | WHERE r.ev_class = $3 AND r.rulename != $4 ORDER BY 1
-[ RECORD 2 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | CREATE TABLE t1 (                                                                                                 
                                                                                                                           
                                            +
       |     a   int,                                                                                                      
                                                                                                                           
                                            +
       |     b   int                                                                                                       
                                                                                                                           
                                            +
       | )
-[ RECORD 3 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT SUBSTRING(unnest(reloptions) from $1) AS tablegroup                                                        
                                                                                                                           
                                            +
       | FROM pg_catalog.pg_class WHERE oid = $2
-[ RECORD 4 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | INSERT INTO t1 SELECT i/$1, i/$2                                                                                  
                                                                                                                           
                                            +
       |                  FROM generate_series($3,$4) s(i)
-[ RECORD 5 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT c.relchecks, c.relkind, c.relhasindex, c.relhasrules, c.relhastriggers, c.relrowsecurity, c.relforcerowsecu
rity, c.relhasoids, $1, c.reltablespace, CASE WHEN c.reloftype = $2 THEN $3 ELSE c.reloftype::pg_catalog.regtype::pg_catalo
g.text END, c.relpersistence, c.relreplident+
       | FROM pg_catalog.pg_class c                                                                                        
                                                                                                                           
                                            +
       |  LEFT JOIN pg_catalog.pg_class tc ON (c.reltoastrelid = tc.oid)                                                   
                                                                                                                           
                                            +
       | WHERE c.oid = $4
-[ RECORD 6 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | select userid::regrole, dbid, query from pg_stat_statements order by stddev_time desc limit $1
-[ RECORD 7 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time)/calls desc lim
it $1
-[ RECORD 8 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | DISCARD TEMP
-[ RECORD 9 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | select userid::regrole, dbid, query from pg_stat_statements order by total_time desc limit $1
-[ RECORD 10 ]-------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT pg_catalog.pg_get_viewdef($1::pg_catalog.oid, $2)
```
```sh
yugabyte=# select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time) desc limit 10;     -- total
-[ RECORD 1 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT r.rulename, trim(trailing $1 from pg_catalog.pg_get_ruledef(r.oid, $2))                                    
                                                                                                                           
                                            +
       | FROM pg_catalog.pg_rewrite r                                                                                      
                                                                                                                           
                                            +
       | WHERE r.ev_class = $3 AND r.rulename != $4 ORDER BY 1
-[ RECORD 2 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | CREATE TABLE t1 (                                                                                                 
                                                                                                                           
                                            +
       |     a   int,                                                                                                      
                                                                                                                           
                                            +
       |     b   int                                                                                                       
                                                                                                                           
                                            +
       | )
-[ RECORD 3 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT SUBSTRING(unnest(reloptions) from $1) AS tablegroup                                                        
                                                                                                                           
                                            +
       | FROM pg_catalog.pg_class WHERE oid = $2
-[ RECORD 4 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | INSERT INTO t1 SELECT i/$1, i/$2                                                                                  
                                                                                                                           
                                            +
       |                  FROM generate_series($3,$4) s(i)
-[ RECORD 5 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT c.relchecks, c.relkind, c.relhasindex, c.relhasrules, c.relhastriggers, c.relrowsecurity, c.relforcerowsecu
rity, c.relhasoids, $1, c.reltablespace, CASE WHEN c.reloftype = $2 THEN $3 ELSE c.reloftype::pg_catalog.regtype::pg_catalo
g.text END, c.relpersistence, c.relreplident+
       | FROM pg_catalog.pg_class c                                                                                        
                                                                                                                           
                                            +
       |  LEFT JOIN pg_catalog.pg_class tc ON (c.reltoastrelid = tc.oid)                                                   
                                                                                                                           
                                            +
       | WHERE c.oid = $4
-[ RECORD 6 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | select userid::regrole, dbid, query from pg_stat_statements order by stddev_time desc limit $1
-[ RECORD 7 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time)/calls desc lim
it $1
-[ RECORD 8 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | DISCARD TEMP
-[ RECORD 9 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | select userid::regrole, dbid, query from pg_stat_statements order by total_time desc limit $1
-[ RECORD 10 ]-------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT pg_catalog.pg_get_viewdef($1::pg_catalog.oid, $2)
```

### Top 10 time-consuming queries

```sh
yugabyte=# select userid::regrole, dbid, query from pg_stat_statements order by mean_time desc limit 10;    -- mean time
-[ RECORD 1 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | INSERT INTO t1 SELECT i/$1, i/$2                                                                                  
                                                                                                                           
      +
       |                  FROM generate_series($3,$4) s(i)
-[ RECORD 2 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | CREATE TABLE t1 (                                                                                                 
                                                                                                                           
      +
       |     a   int,                                                                                                      
                                                                                                                           
      +
       |     b   int                                                                                                       
                                                                                                                           
      +
       | )
-[ RECORD 3 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | SELECT a.attname,                                                                                                 
                                                                                                                           
      +
       |   pg_catalog.format_type(a.atttypid, a.atttypmod),                                                                
                                                                                                                           
      +
       |   (SELECT substring(pg_catalog.pg_get_expr(d.adbin, d.adrelid) for $1)                                            
                                                                                                                           
      +
       |    FROM pg_catalog.pg_attrdef d                                                                                   
                                                                                                                           
      +
       |    WHERE d.adrelid = a.attrelid AND d.adnum = a.attnum AND a.atthasdef),                                          
                                                                                                                           
      +
       |   a.attnotnull,                                                                                                   
                                                                                                                           
      +
       |   (SELECT c.collname FROM pg_catalog.pg_collation c, pg_catalog.pg_type t                                         
                                                                                                                           
      +
       |    WHERE c.oid = a.attcollation AND t.oid = a.atttypid AND a.attcollation <> t.typcollation) AS attcollation,     
                                                                                                                           
      +
       |   a.attidentity,                                                                                                  
                                                                                                                           
      +
       |   a.attstorage,                                                                                                   
                                                                                                                           
      +
       |   pg_catalog.col_description(a.attrelid, a.attnum)                                                                
                                                                                                                           
      +
       | FROM pg_catalog.pg_attribute a                                                                                    
                                                                                                                           
      +
       | WHERE a.attrelid = $2 AND a.attnum > $3 AND NOT a.attisdropped                                                    
                                                                                                                           
      +
       | ORDER BY a.attnum
-[ RECORD 4 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | SELECT a.attname,                                                                                                 
                                                                                                                           
      +
       |   pg_catalog.format_type(a.atttypid, a.atttypmod),                                                                
                                                                                                                           
      +
       |   (SELECT substring(pg_catalog.pg_get_expr(d.adbin, d.adrelid) for $1)                                            
                                                                                                                           
      +
       |    FROM pg_catalog.pg_attrdef d                                                                                   
                                                                                                                           
      +
       |    WHERE d.adrelid = a.attrelid AND d.adnum = a.attnum AND a.atthasdef),                                          
                                                                                                                           
      +
       |   a.attnotnull,                                                                                                   
                                                                                                                           
      +
       |   (SELECT c.collname FROM pg_catalog.pg_collation c, pg_catalog.pg_type t                                         
                                                                                                                           
      +
       |    WHERE c.oid = a.attcollation AND t.oid = a.atttypid AND a.attcollation <> t.typcollation) AS attcollation,     
                                                                                                                           
      +
       |   a.attidentity                                                                                                   
                                                                                                                           
      +
       | FROM pg_catalog.pg_attribute a                                                                                    
                                                                                                                           
      +
       | WHERE a.attrelid = $2 AND a.attnum > $3 AND NOT a.attisdropped                                                    
                                                                                                                           
      +
       | ORDER BY a.attnum
-[ RECORD 5 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | ANALYZE t1
-[ RECORD 6 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | SELECT pg_catalog.pg_get_viewdef($1::pg_catalog.oid, $2)
-[ RECORD 7 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | SELECT n.nspname as "Schema",                                                                                     
                                                                                                                           
      +
       |   p.proname as "Name",                                                                                            
                                                                                                                           
      +
       |   pg_catalog.pg_get_function_result(p.oid) as "Result data type",                                                 
                                                                                                                           
      +
       |   pg_catalog.pg_get_function_arguments(p.oid) as "Argument data types",                                           
                                                                                                                           
      +
       |  CASE p.prokind                                                                                                   
                                                                                                                           
      +
       |   WHEN $1 THEN $2                                                                                                 
                                                                                                                           
      +
       |   WHEN $3 THEN $4                                                                                                 
                                                                                                                           
      +
       |   WHEN $5 THEN $6                                                                                                 
                                                                                                                           
      +
       |   ELSE $7                                                                                                         
                                                                                                                           
      +
       |  END as "Type"                                                                                                    
                                                                                                                           
      +
       | FROM pg_catalog.pg_proc p                                                                                         
                                                                                                                           
      +
       |      LEFT JOIN pg_catalog.pg_namespace n ON n.oid = p.pronamespace                                                
                                                                                                                           
      +
       | WHERE p.proname OPERATOR(pg_catalog.~) $8                                                                         
                                                                                                                           
      +
       |   AND pg_catalog.pg_function_is_visible(p.oid)                                                                    
                                                                                                                           
      +
       | ORDER BY 1, 2, 4
-[ RECORD 8 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | SELECT c.oid,                                                                                                     
                                                                                                                           
      +
       |   n.nspname,                                                                                                      
                                                                                                                           
      +
       |   c.relname                                                                                                       
                                                                                                                           
      +
       | FROM pg_catalog.pg_class c                                                                                        
                                                                                                                           
      +
       |      LEFT JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace                                                
                                                                                                                           
      +
       | WHERE c.relname OPERATOR(pg_catalog.~) $1                                                                         
                                                                                                                           
      +
       |   AND pg_catalog.pg_table_is_visible(c.oid)                                                                       
                                                                                                                           
      +
       | ORDER BY 2, 3
-[ RECORD 9 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | SELECT c.relchecks, c.relkind, c.relhasindex, c.relhasrules, c.relhastriggers, c.relrowsecurity, c.relforcerowsecu
rity, c.relhasoids, pg_catalog.array_to_string(c.reloptions || array(select $1 || x from pg_catalog.unnest(tc.reloptions) x
), $2)+
       | , c.reltablespace, CASE WHEN c.reloftype = $3 THEN $4 ELSE c.reloftype::pg_catalog.regtype::pg_catalog.text END, c
.relpersistence, c.relreplident                                                                                            
      +
       | FROM pg_catalog.pg_class c                                                                                        
                                                                                                                           
      +
       |  LEFT JOIN pg_catalog.pg_class tc ON (c.reltoastrelid = tc.oid)                                                   
                                                                                                                           
      +
       | WHERE c.oid = $5
-[ RECORD 10 ]-------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | select userid::regrole, dbid, query from pg_stat_statements order by (shared_blks_hit+shared_blks_dirtied) desc li
mit $1
```

```
yugabyte=# select userid::regrole, dbid, query from pg_stat_statements order by total_time desc limit 10;      -- total time
-[ RECORD 1 ]---------------------------------------------------------------------------------------------------------
userid | yugabyte
dbid   | 12463
query  | INSERT INTO t1 SELECT i/$1, i/$2                                                                             +
       |                  FROM generate_series($3,$4) s(i)
-[ RECORD 2 ]---------------------------------------------------------------------------------------------------------
userid | yugabyte
dbid   | 12463
query  | CREATE TABLE t1 (                                                                                            +
       |     a   int,                                                                                                 +
       |     b   int                                                                                                  +
       | )
-[ RECORD 3 ]---------------------------------------------------------------------------------------------------------
userid | yugabyte
dbid   | 12463
query  | END
-[ RECORD 4 ]---------------------------------------------------------------------------------------------------------
userid | yugabyte
dbid   | 12463
query  | BEGIN
-[ RECORD 5 ]---------------------------------------------------------------------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT a.attname,                                                                                            +
       |   pg_catalog.format_type(a.atttypid, a.atttypmod),                                                           +
       |   (SELECT substring(pg_catalog.pg_get_expr(d.adbin, d.adrelid) for $1)                                       +
       |    FROM pg_catalog.pg_attrdef d                                                                              +
       |    WHERE d.adrelid = a.attrelid AND d.adnum = a.attnum AND a.atthasdef),                                     +
       |   a.attnotnull,                                                                                              +
       |   (SELECT c.collname FROM pg_catalog.pg_collation c, pg_catalog.pg_type t                                    +
       |    WHERE c.oid = a.attcollation AND t.oid = a.atttypid AND a.attcollation <> t.typcollation) AS attcollation,+
       |   a.attidentity,                                                                                             +
       |   a.attstorage,                                                                                              +
       |   pg_catalog.col_description(a.attrelid, a.attnum)                                                           +
       | FROM pg_catalog.pg_attribute a                                                                               +
       | WHERE a.attrelid = $2 AND a.attnum > $3 AND NOT a.attisdropped                                               +
       | ORDER BY a.attnum
-[ RECORD 6 ]---------------------------------------------------------------------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT pg_catalog.pg_get_viewdef($1::pg_catalog.oid, $2)
-[ RECORD 7 ]---------------------------------------------------------------------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT a.attname,                                                                                            +
       |   pg_catalog.format_type(a.atttypid, a.atttypmod),                                                           +
       |   (SELECT substring(pg_catalog.pg_get_expr(d.adbin, d.adrelid) for $1)                                       +
       |    FROM pg_catalog.pg_attrdef d                                                                              +
       |    WHERE d.adrelid = a.attrelid AND d.adnum = a.attnum AND a.atthasdef),                                     +
       |   a.attnotnull,                                                                                              +
       |   (SELECT c.collname FROM pg_catalog.pg_collation c, pg_catalog.pg_type t                                    +
       |    WHERE c.oid = a.attcollation AND t.oid = a.atttypid AND a.attcollation <> t.typcollation) AS attcollation,+
       |   a.attidentity                                                                                              +
       | FROM pg_catalog.pg_attribute a                                                                               +
       | WHERE a.attrelid = $2 AND a.attnum > $3 AND NOT a.attisdropped                                               +
       | ORDER BY a.attnum
-[ RECORD 8 ]---------------------------------------------------------------------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT n.nspname as "Schema",                                                                                +
       |   p.proname as "Name",                                                                                       +
       |   pg_catalog.pg_get_function_result(p.oid) as "Result data type",                                            +
       |   pg_catalog.pg_get_function_arguments(p.oid) as "Argument data types",                                      +
       |  CASE p.prokind                                                                                              +
       |   WHEN $1 THEN $2                                                                                            +
       |   WHEN $3 THEN $4                                                                                            +
       |   WHEN $5 THEN $6                                                                                            +
       |   ELSE $7                                                                                                    +
       |  END as "Type"                                                                                               +
       | FROM pg_catalog.pg_proc p                                                                                    +
       |      LEFT JOIN pg_catalog.pg_namespace n ON n.oid = p.pronamespace                                           +
       | WHERE p.proname OPERATOR(pg_catalog.~) $8                                                                    +
       |   AND pg_catalog.pg_function_is_visible(p.oid)                                                               +
       | ORDER BY 1, 2, 4
-[ RECORD 9 ]---------------------------------------------------------------------------------------------------------
userid | yugabyte
dbid   | 12463
query  | ANALYZE t1
-[ RECORD 10 ]--------------------------------------------------------------------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT c.oid,                                                                                                +
       |   n.nspname,                                                                                                 +
       |   c.relname                                                                                                  +
       | FROM pg_catalog.pg_class c                                                                                   +
       |      LEFT JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace                                           +
       | WHERE c.relname OPERATOR(pg_catalog.~) $1                                                                    +
       |   AND pg_catalog.pg_table_is_visible(c.oid)                                                                  +
       | ORDER BY 2, 3
```

### Top 10 response-time outliers

```sh
yugabyte=# select userid::regrole, dbid, query from pg_stat_statements order by stddev_time desc limit 10;
-[ RECORD 1 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | INSERT INTO t1 SELECT i/$1, i/$2                                                                                  
                                                                                                                           
      +
       |                  FROM generate_series($3,$4) s(i)
-[ RECORD 2 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | SELECT a.attname,                                                                                                 
                                                                                                                           
      +
       |   pg_catalog.format_type(a.atttypid, a.atttypmod),                                                                
                                                                                                                           
      +
       |   (SELECT substring(pg_catalog.pg_get_expr(d.adbin, d.adrelid) for $1)                                            
                                                                                                                           
      +
       |    FROM pg_catalog.pg_attrdef d                                                                                   
                                                                                                                           
      +
       |    WHERE d.adrelid = a.attrelid AND d.adnum = a.attnum AND a.atthasdef),                                          
                                                                                                                           
      +
       |   a.attnotnull,                                                                                                   
                                                                                                                           
      +
       |   (SELECT c.collname FROM pg_catalog.pg_collation c, pg_catalog.pg_type t                                         
                                                                                                                           
      +
       |    WHERE c.oid = a.attcollation AND t.oid = a.atttypid AND a.attcollation <> t.typcollation) AS attcollation,     
                                                                                                                           
      +
       |   a.attidentity,                                                                                                  
                                                                                                                           
      +
       |   a.attstorage,                                                                                                   
                                                                                                                           
      +
       |   pg_catalog.col_description(a.attrelid, a.attnum)                                                                
                                                                                                                           
      +
       | FROM pg_catalog.pg_attribute a                                                                                    
                                                                                                                           
      +
       | WHERE a.attrelid = $2 AND a.attnum > $3 AND NOT a.attisdropped                                                    
                                                                                                                           
      +
       | ORDER BY a.attnum
-[ RECORD 3 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | SELECT pg_catalog.pg_get_viewdef($1::pg_catalog.oid, $2)
-[ RECORD 4 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | SELECT n.nspname as "Schema",                                                                                     
                                                                                                                           
      +
       |   p.proname as "Name",                                                                                            
                                                                                                                           
      +
       |   pg_catalog.pg_get_function_result(p.oid) as "Result data type",                                                 
                                                                                                                           
      +
       |   pg_catalog.pg_get_function_arguments(p.oid) as "Argument data types",                                           
                                                                                                                           
      +
       |  CASE p.prokind                                                                                                   
                                                                                                                           
      +
       |   WHEN $1 THEN $2                                                                                                 
                                                                                                                           
      +
       |   WHEN $3 THEN $4                                                                                                 
                                                                                                                           
      +
       |   WHEN $5 THEN $6                                                                                                 
                                                                                                                           
      +
       |   ELSE $7                                                                                                         
                                                                                                                           
      +
       |  END as "Type"                                                                                                    
                                                                                                                           
      +
       | FROM pg_catalog.pg_proc p                                                                                         
                                                                                                                           
      +
       |      LEFT JOIN pg_catalog.pg_namespace n ON n.oid = p.pronamespace                                                
                                                                                                                           
      +
       | WHERE p.proname OPERATOR(pg_catalog.~) $8                                                                         
                                                                                                                           
      +
       |   AND pg_catalog.pg_function_is_visible(p.oid)                                                                    
                                                                                                                           
      +
       | ORDER BY 1, 2, 4
-[ RECORD 5 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | SELECT c.oid,                                                                                                     
                                                                                                                           
      +
       |   n.nspname,                                                                                                      
                                                                                                                           
      +
       |   c.relname                                                                                                       
                                                                                                                           
      +
       | FROM pg_catalog.pg_class c                                                                                        
                                                                                                                           
      +
       |      LEFT JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace                                                
                                                                                                                           
      +
       | WHERE c.relname OPERATOR(pg_catalog.~) $1                                                                         
                                                                                                                           
      +
       |   AND pg_catalog.pg_table_is_visible(c.oid)                                                                       
                                                                                                                           
      +
       | ORDER BY 2, 3
-[ RECORD 6 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time)/calls desc lim
it $1
-[ RECORD 7 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | SELECT r.rulename, trim(trailing $1 from pg_catalog.pg_get_ruledef(r.oid, $2))                                    
                                                                                                                           
      +
       | FROM pg_catalog.pg_rewrite r                                                                                      
                                                                                                                           
      +
       | WHERE r.ev_class = $3 AND r.rulename != $4 ORDER BY 1
-[ RECORD 8 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | SELECT c.relchecks, c.relkind, c.relhasindex, c.relhasrules, c.relhastriggers, c.relrowsecurity, c.relforcerowsecu
rity, c.relhasoids, pg_catalog.array_to_string(c.reloptions || array(select $1 || x from pg_catalog.unnest(tc.reloptions) x
), $2)+
       | , c.reltablespace, CASE WHEN c.reloftype = $3 THEN $4 ELSE c.reloftype::pg_catalog.regtype::pg_catalog.text END, c
.relpersistence, c.relreplident                                                                                            
      +
       | FROM pg_catalog.pg_class c                                                                                        
                                                                                                                           
      +
       |  LEFT JOIN pg_catalog.pg_class tc ON (c.reltoastrelid = tc.oid)                                                   
                                                                                                                           
      +
       | WHERE c.oid = $5
-[ RECORD 9 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | SELECT SUBSTRING(unnest(reloptions) from $1) AS tablegroup                                                        
                                                                                                                           
      +
       | FROM pg_catalog.pg_class WHERE oid = $2
-[ RECORD 10 ]-------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
------
userid | yugabyte
dbid   | 12463
query  | SELECT inhparent::pg_catalog.regclass,                                                                            
                                                                                                                           
      +
       |   pg_catalog.pg_get_expr(c.relpartbound, inhrelid),                                                               
                                                                                                                           
      +
       |   pg_catalog.pg_get_partition_constraintdef(inhrelid)                                                             
                                                                                                                           
      +
       | FROM pg_catalog.pg_class c JOIN pg_catalog.pg_inherits i ON c.oid = inhrelid                                      
                                                                                                                           
      +
       | WHERE c.oid = $1 AND c.relispartition
```


### Top 10 queries by memory usage

```sh
yugabyte=# select userid::regrole, dbid, query from pg_stat_statements order by (shared_blks_hit+shared_blks_dirtied) desc limit 10;
-[ RECORD 1 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT r.rulename, trim(trailing $1 from pg_catalog.pg_get_ruledef(r.oid, $2))                                    
                                                                                                                           
                                            +
       | FROM pg_catalog.pg_rewrite r                                                                                      
                                                                                                                           
                                            +
       | WHERE r.ev_class = $3 AND r.rulename != $4 ORDER BY 1
-[ RECORD 2 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | CREATE TABLE t1 (                                                                                                 
                                                                                                                           
                                            +
       |     a   int,                                                                                                      
                                                                                                                           
                                            +
       |     b   int                                                                                                       
                                                                                                                           
                                            +
       | )
-[ RECORD 3 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT SUBSTRING(unnest(reloptions) from $1) AS tablegroup                                                        
                                                                                                                           
                                            +
       | FROM pg_catalog.pg_class WHERE oid = $2
-[ RECORD 4 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | INSERT INTO t1 SELECT i/$1, i/$2                                                                                  
                                                                                                                           
                                            +
       |                  FROM generate_series($3,$4) s(i)
-[ RECORD 5 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT c.relchecks, c.relkind, c.relhasindex, c.relhasrules, c.relhastriggers, c.relrowsecurity, c.relforcerowsecu
rity, c.relhasoids, $1, c.reltablespace, CASE WHEN c.reloftype = $2 THEN $3 ELSE c.reloftype::pg_catalog.regtype::pg_catalo
g.text END, c.relpersistence, c.relreplident+
       | FROM pg_catalog.pg_class c                                                                                        
                                                                                                                           
                                            +
       |  LEFT JOIN pg_catalog.pg_class tc ON (c.reltoastrelid = tc.oid)                                                   
                                                                                                                           
                                            +
       | WHERE c.oid = $4
-[ RECORD 6 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | select userid::regrole, dbid, query from pg_stat_statements order by stddev_time desc limit $1
-[ RECORD 7 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time)/calls desc lim
it $1
-[ RECORD 8 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | DISCARD TEMP
-[ RECORD 9 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time) desc limit $1
-[ RECORD 10 ]-------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT pg_catalog.pg_get_viewdef($1::pg_catalog.oid, $2)
```


### The 10 consumers of temporary space

```sh
yugabyte=# select userid::regrole, dbid, query from pg_stat_statements order by temp_blks_written desc limit 10;    
-[ RECORD 1 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | INSERT INTO t1 SELECT i/$1, i/$2                                                                                  
                                                                                                                           
                                            +
       |                  FROM generate_series($3,$4) s(i)
-[ RECORD 2 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | CREATE TABLE t1 (                                                                                                 
                                                                                                                           
                                            +
       |     a   int,                                                                                                      
                                                                                                                           
                                            +
       |     b   int                                                                                                       
                                                                                                                           
                                            +
       | )
-[ RECORD 3 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time)/calls desc lim
it $1
-[ RECORD 4 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | select userid::regrole, dbid, query from pg_stat_statements order by (blk_read_time+blk_write_time) desc limit $1
-[ RECORD 5 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT c.relchecks, c.relkind, c.relhasindex, c.relhasrules, c.relhastriggers, c.relrowsecurity, c.relforcerowsecu
rity, c.relhasoids, $1, c.reltablespace, CASE WHEN c.reloftype = $2 THEN $3 ELSE c.reloftype::pg_catalog.regtype::pg_catalo
g.text END, c.relpersistence, c.relreplident+
       | FROM pg_catalog.pg_class c                                                                                        
                                                                                                                           
                                            +
       |  LEFT JOIN pg_catalog.pg_class tc ON (c.reltoastrelid = tc.oid)                                                   
                                                                                                                           
                                            +
       | WHERE c.oid = $4
-[ RECORD 6 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | select userid::regrole, dbid, query from pg_stat_statements order by stddev_time desc limit $1
-[ RECORD 7 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT SUBSTRING(unnest(reloptions) from $1) AS tablegroup                                                        
                                                                                                                           
                                            +
       | FROM pg_catalog.pg_class WHERE oid = $2
-[ RECORD 8 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | DISCARD TEMP
-[ RECORD 9 ]--------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT r.rulename, trim(trailing $1 from pg_catalog.pg_get_ruledef(r.oid, $2))                                    
                                                                                                                           
                                            +
       | FROM pg_catalog.pg_rewrite r                                                                                      
                                                                                                                           
                                            +
       | WHERE r.ev_class = $3 AND r.rulename != $4 ORDER BY 1
-[ RECORD 10 ]-------------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------
--------------------------------------------
userid | yugabyte
dbid   | 12463
query  | SELECT pg_catalog.pg_get_viewdef($1::pg_catalog.oid, $2)
```

## Reset statistics

pg_stat_statements_reset discards all statistics gathered so far by pg_stat_statements. By default, this function can only be executed by superusers.

```sh
yugabyte=# select pg_stat_statements_reset();
 pg_stat_statements_reset 
--------------------------
 
(1 row)
```
