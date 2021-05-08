---
title: View Live Queries with pg_stat_activity
linkTitle: View Live Queries with pg_stat_activity
description: Using pg_stat_activity to troubleshoot issues and help to identify long running transactions.
aliases:
headerTitle: View Live Queries with pg_stat_activity
image: /images/section_icons/index/develop.png
menu:
  latest:
    identifier: pg-stat-activity
    parent: query-tuning
    weight: 566
isTocNested: true
showAsideToc: true
---
 
Yugabyte leverages PostgreSQL’s pg_stat_activity view to analyze live queries.
This view returns analysis and diagnostic information about active Yugabyte server processes and queries.
The pg_stat_activity view returns one row per Yugabyte server process and displays information related to the current status of the database connection.
 
At a yugabyte prompt, running the following command returns the columns supported by pg_stat_activity.
 
```sql
yugabyte=# \d pg_stat_activity
```
 
```
                     View "pg_catalog.pg_stat_activity"
     Column      |           Type           | Collation | Nullable | Default
------------------+--------------------------+-----------+----------+---------
datid            | oid                      |           |          |
datname          | name                     |           |          |
pid              | integer                  |           |          |
usesysid         | oid                      |           |          |
usename          | name                     |           |          |
application_name | text                     |           |          |
client_addr      | inet                     |           |          |
client_hostname  | text                     |           |          |
client_port      | integer                  |           |          |
backend_start    | timestamp with time zone |           |          |
xact_start       | timestamp with time zone |           |          |
query_start      | timestamp with time zone |           |          |
state_change     | timestamp with time zone |           |          |
wait_event_type  | text                     |           |          |
wait_event       | text                     |           |          |
state            | text                     |           |          |
backend_xid      | xid                      |           |          |
backend_xmin     | xid                      |           |          |
query            | text                     |           |          |
backend_type     | text                     |           |          |
```
### Examples
 
We can start by running a query on a yugabyte terminal that returns basic information about active Yugabyte processes.
 
```sql
yugabyte=# SELECT datname, pid, application_name, state, query FROM pg_stat_activity;
```
The output is displayed with the following details:
```
datname  |  pid  | application_name | state  |                                   query                                   
----------+-------+------------------+--------+----------------------------------------------------------------------------
yugabyte | 10027 | ysqlsh           | active | SELECT datname, pid, application_name, state, query FROM pg_stat_activity;
         | 10013 |                  |        |
(2 rows)
```
 
`datname` :  The database connected to this process.                               
`pid` : The ID value of the specific process.                                      
`application_name`: The application connected to this process.                                
`state`: The operational condition of the process which can be determined by one of the several states
```
•active
•idle
•idle in transaction
•idle in transaction (aborted)
•fastpath function call
•disabled
```
`query` : The latest query executed for this process.
 
The next example focuses on identifying an open transaction. We can use the yugabyte data resources from [here](https://download.yugabyte.com/#macos) and follow the steps to load a sample dataset.
Often enough, we need to identify long-running queries, because these queries could indicate deeper problems. The pg_stat_activity view can help identify these issues. In our examples, we'll focus on the users table in the sample yb_demo database.
 
This query shows columns from a row in the users table:
```sql
yugabyte=# SELECT id, name, state FROM users WHERE id = 212;
```
```
id  |     name      | state
-----+---------------+-------
212 | Jacinthe Rowe | CO
(1 row)
```
 
We might want to update the state column value of this role with a transaction, as shown in this query:
 
```sql
yugabyte=# BEGIN TRANSACTION; UPDATE users SET state = 'IA' WHERE id = 212;
```
However, we forgot to add END; at the end of the query, to close the transaction. So, the above query will return
```
BEGIN
UPDATE 1
```
But since the transaction never ended, it will waste resources as an open process.
We can check for the state of the transaction by opening another terminal running yugabte and find information about this idle transaction with pg_stat_activity, as shown here
```sql
yugabyte=# SELECT datname, pid, application_name, state, query FROM pg_stat_activity;
```
```
datname  |  pid  | application_name |        state        |                                   query                                   
----------+-------+------------------+---------------------+----------------------------------------------------------------------------
yugabyte | 10381 | ysqlsh           | active              | SELECT datname, pid, application_name, state, query FROM pg_stat_activity;
yb_demo  | 10033 | ysqlsh           | idle in transaction | UPDATE users SET state = 'IA' WHERE id = 212;
         | 10013 |                  |                     |
(3 rows)
```
In the second row, the state column value shows the idle transaction. The process has a pid value of 10033.
Additionally , we can run some time related queries to help us identify long running transactions which are particularly useful when there are a lot of open connections on that node.
 
To get list of processes ordered by current `txn_duration`
```sql
yugabyte=# SELECT datname, pid, application_name, state, query, now() - xact_start AS txn_duration
FROM pg_stat_activity ORDER BY txn_duration desc;
```
```
datname  |  pid  | application_name |        state        |                                          query                                          |  txn_duration  
----------+-------+------------------+---------------------+-----------------------------------------------------------------------------------------+-----------------
         | 10013 |                  |                     |                                                                                         |
yb_demo  | 10033 | ysqlsh           | idle in transaction | UPDATE users SET state = 'IA' WHERE id = 212;                                           | 00:03:47.957187
yugabyte | 10381 | ysqlsh           | active              | SELECT datname, pid, application_name, state, query, now() - xact_start AS txn_duration+| 00:00:00
         |       |                  |                     | FROM pg_stat_activity ORDER BY txn_duration desc;                                       |
(3 rows)
```
Alternatively, to get a list of processes where current transaction has taken more than 1 minute
```sql
yugabyte=# SELECT datname, pid, application_name, state, query, xact_start FROM pg_stat_activity WHERE now() - xact_start > '1 min';
```
```
datname |  pid  | application_name |        state        |                     query                     |          xact_start         
---------+-------+------------------+---------------------+-----------------------------------------------+------------------------------
yb_demo | 10033 | ysqlsh           | idle in transaction | UPDATE users SET state = 'IA' WHERE id = 212; | 2021-05-06 15:26:28.74615-04
(1 row)
```
Finally, to terminate this idle transaction with pid(10033), we can run the following query
```sql
yugabyte=# SELECT pg_terminate_backend(10033);
```
```
pg_terminate_backend
---------------------------
t
(1 row)
```
The pg_terminate_backend function returns "t" when it succeeds, and "f" if it fails. When we again query pg_stat_activity in the second terminal, we see that the idle process ended:
```sql
yugabyte=# SELECT datname, pid, application_name, state, query FROM pg_stat_activity;
```
```
datname  |  pid  | application_name | state  |                                   query                                   
----------+-------+------------------+--------+----------------------------------------------------------------------------
yugabyte | 10381 | ysqlsh           | active | SELECT datname, pid, application_name, state, query FROM pg_stat_activity;
         | 10013 |                  |        |
(2 rows)
```