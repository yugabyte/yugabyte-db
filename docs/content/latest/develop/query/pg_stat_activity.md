---
title: View Live Queries
headerTitle: View Live Queries
linkTitle: View Live Queries
description: Analyze and diagnose running queries with pg\_stat\_activity
menu:
  latest:
    identifier: pg_stat_activity
    parent: develop
    weight: 566
isTocNested: true
showAsideToc: true
---

The `pg_stat_activity` view provides information on currently running tasks, so you can use it to troubleshoot problems by, for example, identifying long-running idle in transaction sessions or very long running queries. For each server process, `pg_stat_activity` can be used to discover the process ID, active user, running query, state, time the last query started, and more.

To query `pg_stat_activity`, use the `SELECT` statement, as with any other table:

```SQL
SELECT * FROM pg_stat_activity;
```

The `pg_stat_activity` view displays one row per server process, showing information related to the current activity of that process.

The fields and their values are described in the following table.

Field | Type | Description
--- | --- | ---
datid | oid | The object identifier (OID) of the database to which the backend is connected.
datname | name | The name of the database to which the backend is connected.
pid | integer | The ID of the backend process.
usesysid | oid | The OID of the user.
usename | name | The name of the user.
application_name | text | The name of the application that is connected to this backend.
client_addr | inet | The IP address of the client connected to this backend. If this field is empty, the client is connected through a Unix socket on the server or this is an internal process such as autovacuum.
client_hostname | text | The host name of the client connected to this backend, as reported by a reverse DNS lookup of client_addr.
client_port | integer | The TCP port number that the client uses for communication with the backend server. A value of -1 indicates that a Unix socket is used.
backend_start | timestampz | Time when the current backend process was started.
xact_start | timestampz | The time when the current transaction of this process was started, or null if no transaction is active. If the current query is the first transaction of the process, this field is equivalent to the query_start field.
query_start | timestampz | Time when the currently active query was started. If the state field is not set to active, the query_start field indicates the time when the last query was started.
state_change | timestampz | Time when the previous state was changed.
wait_event_type | text | The type of event the backend is waiting for.
wait_event | text | The name of the event being waiting for.
state | text | The current state of the backend. Valid values: active, idle, idle in transaction, idle in transaction (aborted), fastpath function call, and disabled.
backend_xid | xid | Top-level transaction identifier of this backend, if any.
backend_xmin | xid | The current backend's xmin horizon.
query | text | The last executed query. If the state parameter is set to active, the currently executing query is displayed. If the state parameter is set to a value other than active, the last executed query is displayed. By default, the query text is truncated to 1,024 characters in length. Specify the track_activity_query_size parameter to change the maximum number of characters.
backend_type | text | The type of current backend. Possible values are autovacuum launcher, autovacuum worker, background worker, background writer, client backend, checkpointer, startup, walreceiver, walsender and walwriter.

### Examples

To determine the number of active connections:

```sql
yugabyte=# SELECT count(*) FROM pg_stat_activity;
```

```output
 count 
-------
     2
(1 row)
```

To view usernames and the corresponding clients:

```SQL
yugabyte=# SELECT datname,usename,client_addr,client_port FROM pg_stat_activity ;
```

```output
 datname | usename  | client_addr  | client_port 
---------+----------+--------------+-------------
 yb_demo | yugabyte | 127.0.0.1/32 |       49964
 yb_demo | yugabyte | 128.0.0.2/32 |       56734
(2 rows)
```

To view only active queries:

```SQL
yugabyte=# SELECT datname,usename,query
   FROM pg_stat_activity
   WHERE state != 'idle' ;
```

```output
 datname | usename  |            query             
---------+----------+------------------------------
 yb_demo | yugabyte | SELECT datname,usename,query+
         |          |    FROM pg_stat_activity    +
         |          |    WHERE state != 'idle' ;
(1 row)
```

To view long running queries:

```SQL
yugabyte=# SELECT current_timestamp - query_start as runtime, datname, usename, query
    FROM pg_stat_activity
    WHERE state != 'idle'
    ORDER BY 1 desc;
```

```output
     runtime      | datname | usename  |                   query                    
------------------+---------+----------+--------------------------------------------
 00:01:47.35704   | yb_demo | yugabyte | update t set name = 'fourth' where id = 1;
 -00:00:00.000557 | yb_demo | yugabyte | SELECT application_name,                  +
                  |         |          |     state, wait_event, query              +
                  |         |          |     FROM pg_stat_activity;
(2 rows)
```

Use `pg_stat_activity` to view the state of running processes and find out if any are idle or waiting:

```SQL
yugabyte=# SELECT application_name, 
    state, wait_event, query 
    FROM pg_stat_activity;
```

```output
 application_name |        state        |    wait_event    |                           query                            
------------------+---------------------+------------------+------------------------------------------------------------
 ysqlsh           | active              |                  | select application_name,state,query from pg_stat_activity;
 user2            | idle in transaction | ClientRead       | update t set name = 'second' where id = 1;
                  |                     | CheckpointerMain | 
(3 rows)
```

You can then use `pg_stat_activity` to identify the process ID of the waiting process so that you can terminate it:

```SQL
yugabyte=# SELECT pid, 
    application_name 
    FROM pg_stat_activity 
    WHERE state = 'idle in transaction';
```

```output
 pid  | application_name 
------+------------------
 2527 | user2
(1 row)
```
