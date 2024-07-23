---
title: Get query statistics using ycql_stat_statements
linkTitle: Get query statistics
description: Track planning and execution statistics for all YCQL statements executed by a server.
headerTitle: Get query statistics using ycql_stat_statements
badges: tp
menu:
  preview:
    identifier: ycql-stat-statements
    parent: query-tuning
    weight: 200
type: docs
---

{{<tabs>}}
{{<tabitem href="../pg-stat-statements/" text="YSQL" icon="postgres" >}}
{{<tabitem href="../ycql-stat-statements/" text="YCQL" icon="cassandra" active="true" >}}
{{</tabs>}}

Databases can be resource-intensive, consuming a lot of memory, CPU, IO, and network resources. By optimizing your CQL, you can minimize resource use. The `ycql_stat_statements` module helps you track execution statistics for all the YCQL statements executed by a server.

This view is accessible only via YSQL and provides YCQL statement metrics (similar to pg_stat_statements) that are also present on `<yb-tserver-ip>:12000/statements`. The view can be joined with YCQL wait events in the [yb_active_session_history](../../observability/active-session-history/#yb-active-session-history) view on the query ID.

This view is added in a YSQL extension `yb_ycql_utils`, which is not enabled by default.

The columns of the `ycql_stat_statements` view are described in the following table.

| Column | Type | Description |
| :----- | :--- | :---------- |
| queryid | int8 | Hash code to identify identical normalized queries. |
| query | text | Text of a representative statement. |
| is_prepared  | bool | Indicates whether the query statement is prepared statement or unprepared. |
| calls | int8 | Number of times the statement is executed.|
| total_time | float8 | Total time spent executing the statement, in milliseconds. |
| min_time | float8 | Minimum time spent executing the statement, in milliseconds. |
| max_time | float8 | Maximum time spent executing the statement, in milliseconds. |
| mean_time | float8 | Mean time spent executing the statement, in milliseconds. |
| stddev_time | float8 | Population standard deviation of time spent executing the statement, in milliseconds. |

## Examples

{{% setup/local %}}

Note that as this view is accessible via YSQL, run your examples using [ysqlsh](../../../admin/ysqlsh/#starting-ysqlsh).

### Describe the columns in the view

1. Create the extension `yb_ycql_utils` as follows:

    ```sql
    yugabyte=# CREATE EXTENSION yb_ycql_utils;
    ```

1. Get the view description as follows:

    ```sql
    yugabyte=# \d ycql_stat_statements
    ```

    ```output
                   View "public.ycql_stat_statements"
       Column    |       Type       | Collation | Nullable | Default
    -------------+------------------+-----------+----------+---------
     queryid     | bigint           |           |          |
     query       | text             |           |          |
     is_prepared | boolean          |           |          |
     calls       | bigint           |           |          |
     total_time  | double precision |           |          |
     min_time    | double precision |           |          |
     max_time    | double precision |           |          |
     mean_time   | double precision |           |          |
     stddev_time | double precision |           |          |
    ```

### Get basic information

The following example uses a [YCQL workload generator](../../../benchmark/key-value-workload-ycql/) to run YCQL queries in the background and then uses YSQL to query `ycql_stat_statements`.

```sql
yugabyte=# SELECT * FROM ycql_stat_statements;
```

```output
       queryid        |                                                          query                                                          | is_prepared |  calls  |    total_time    | min_time  | max_time  |     mean_time     |    stddev_time
----------------------+-------------------------------------------------------------------------------------------------------------------------+-------------+---------+------------------+-----------+-----------+-------------------+--------------------
  8473086508688080607 | CREATE KEYSPACE IF NOT EXISTS ybdemo_keyspace WITH REPLICATION = {'class': 'SimpleStrategy', 'replication_factor' : 1}; | t           |       1 |            0.778 |     0.778 |     0.778 |             0.778 |                  0
 -8368694706463025697 | USE ybdemo_keyspace;                                                                                                    | t           |       1 |           0.1905 |    0.1905 |    0.1905 |            0.1905 |                  0
  2594328841729435159 | CREATE TABLE IF NOT EXISTS CassandraKeyValue (k varchar, v blob, primary key (k));                                      | t           |       1 |        72.598458 | 72.598458 | 72.598458 |         72.598458 |                  0
  1957997667337333449 | SELECT k, v FROM CassandraKeyValue WHERE k = ?;                                                                         | t           | 1316141 | 376623.670250017 |  0.045125 | 17.026083 | 0.286157539541749 |  0.336173036868407
 -1081940071252633265 | INSERT INTO CassandraKeyValue (k, v) VALUES (?, ?);                                                                     | t           |   80525 | 48712.6266890002 |  0.107417 |    38.126 | 0.604937928457004 |  0.501434959216858
  5685694520060019787 | SELECT * FROM system.peers_v2                                                                                           | f           |       1 |         0.611459 |  0.611459 |  0.611459 |          0.611459 |                  0
 -8693369126900706654 | SELECT * FROM system_schema.functions WHERE keyspace_name = 'ybdemo_keyspace'                                           | f           |       1 |         0.408416 |  0.408416 |  0.408416 |          0.408416 |                  0
  7231358282794359932 | SELECT * FROM system_schema.views WHERE keyspace_name = 'ybdemo_keyspace' AND view_name = 'cassandrakeyvalue'           | f           |       1 |            0.341 |     0.341 |     0.341 |             0.341 |                  0
 -6069774349418914791 | SELECT * FROM system.local WHERE key='local'                                                                            | f           |       1 |         1.088167 |  1.088167 |  1.088167 |          1.088167 |                  0
 -5046967885002247753 | SELECT peer, rpc_address, schema_version, host_id FROM system.peers                                                     | f           |       1 |         0.301083 |  0.301083 |  0.301083 |          0.301083 |                  0
 -3349549932189089002 | SELECT * FROM system_schema.columns WHERE keyspace_name = 'ybdemo_keyspace'                                             | f           |       1 |         1.400792 |  1.400792 |  1.400792 |          1.400792 |                  0
  6930116125454979846 | SELECT * FROM system_schema.views                                                                                       | f           |       1 |         1.129625 |  1.129625 |  1.129625 |          1.129625 |                  0
  7981072946573997034 | select cluster_name from system.local where key = 'local'                                                               | f           |       4 |         1.650293 |  0.300084 |  0.522709 |        0.41257325 | 0.0795915597358633
  4163559541422844425 | SELECT * FROM system_schema.keyspaces WHERE keyspace_name = 'ybdemo_keyspace'                                           | f           |       1 |         0.375458 |  0.375458 |  0.375458 |          0.375458 |                  0
 -1896671018756022147 | SELECT * FROM system_schema.functions                                                                                   | f           |       1 |           0.7635 |    0.7635 |    0.7635 |            0.7635 |                  0
   477300852678741015 | SELECT * FROM system.peers                                                                                              | f           |       1 |         1.051666 |  1.051666 |  1.051666 |          1.051666 |                  0
 -5984255118081173147 | SELECT * FROM system_schema.aggregates WHERE keyspace_name = 'ybdemo_keyspace'                                          | f           |       1 |         0.507375 |  0.507375 |  0.507375 |          0.507375 |                  0
  -199636290905897800 | SELECT * FROM system_schema.tables WHERE keyspace_name = 'ybdemo_keyspace' AND table_name = 'cassandrakeyvalue'         | f           |       1 |         1.229417 |  1.229417 |  1.229417 |          1.229417 |                  0
  6202644009413539627 | SELECT * FROM system_schema.types                                                                                       | f           |       1 |         0.942792 |  0.942792 |  0.942792 |          0.942792 |                  0
  6660660976596803555 | SELECT peer, rpc_address, schema_version, host_id FROM system.peers                                                     | f           |       1 |           0.4265 |    0.4265 |    0.4265 |            0.4265 |                  0
 -4656374775045675304 | SELECT * FROM system_schema.tables                                                                                      | f           |       1 |         1.838917 |  1.838917 |  1.838917 |          1.838917 |                  0
 -2256941656319582329 | SELECT * FROM system_schema.columns                                                                                     | f           |       1 |         3.208791 |  3.208791 |  3.208791 |          3.208791 |                  0
 -2674195503457906853 | SELECT * FROM system_schema.aggregates                                                                                  | f           |       1 |         1.452334 |  1.452334 |  1.452334 |          1.452334 |                  0
 -2285418643723910393 | SELECT * FROM system_schema.tables WHERE keyspace_name = 'ybdemo_keyspace'                                              | f           |       1 |         1.251458 |  1.251458 |  1.251458 |          1.251458 |                  0
   194015117456500066 | SELECT keyspace_name, table_name, start_key, end_key, replica_addresses FROM system.partitions                          | f           |       3 |         2.742959 |  0.253125 |  1.297125 | 0.914319666666667 |  0.469474504586659
 -3362784747732104326 | SELECT schema_version, host_id FROM system.local WHERE key='local'                                                      | f           |       1 |         0.421417 |  0.421417 |  0.421417 |          0.421417 |                  0
 -4290033807176898337 | SELECT * FROM system_schema.types WHERE keyspace_name = 'ybdemo_keyspace'                                               | f           |       1 |         0.346209 |  0.346209 |  0.346209 |          0.346209 |                  0
  1413414562899452953 | SELECT * FROM system_schema.indexes                                                                                     | f           |       1 |         2.569667 |  2.569667 |  2.569667 |          2.569667 |                  0
 -3220527242581763013 | SELECT * FROM system_schema.keyspaces                                                                                   | f           |       1 |         1.031458 |  1.031458 |  1.031458 |          1.031458 |                  0
 -4060076456160928053 | SELECT * FROM system_schema.indexes WHERE keyspace_name = 'ybdemo_keyspace'                                             | f           |       1 |         1.297167 |  1.297167 |  1.297167 |          1.297167 |                  0
  8278745859691011170 | SELECT * FROM system_schema.columns WHERE keyspace_name = 'ybdemo_keyspace' AND table_name = 'cassandrakeyvalue'        | f           |       1 |         1.280542 |  1.280542 |  1.280542 |          1.280542 |                  0
 -5564581055929365977 | SELECT schema_version, host_id FROM system.local WHERE key='local'                                                      | f           |       1 |         0.311208 |  0.311208 |  0.311208 |          0.311208 |                  0
 -1076592564131011035 | SELECT * FROM system_schema.indexes WHERE keyspace_name = 'ybdemo_keyspace' AND table_name = 'cassandrakeyvalue'        | f           |       1 |         1.201042 |  1.201042 |  1.201042 |          1.201042 |                  0
  6806527679817077701 | SELECT * FROM system_schema.views WHERE keyspace_name = 'ybdemo_keyspace'                                               | f           |       1 |         0.421375 |  0.421375 |  0.421375 |          0.421375 |                  0
```

### Top 10 time consuming queries

```sql
yugabyte=# SELECT query FROM ycql_stat_statements ORDER BY mean_time DESC LIMIT 10;
```

```output
                                                          query
-------------------------------------------------------------------------------------------------------------------------
 SELECT * FROM system.local WHERE key='local'
 SELECT * FROM system_schema.columns
 SELECT * FROM system_schema.tables
 SELECT * FROM system.peers_v2
 SELECT * FROM system_schema.indexes
 SELECT * FROM system_schema.columns WHERE keyspace_name = 'ybdemo_keyspace'
 SELECT * FROM system_schema.views
 CREATE KEYSPACE IF NOT EXISTS ybdemo_keyspace WITH REPLICATION = {'class': 'SimpleStrategy', 'replication_factor' : 1};
 SELECT * FROM system_schema.indexes WHERE keyspace_name = 'ybdemo_keyspace'
 SELECT * FROM system_schema.columns WHERE keyspace_name = 'ybdemo_keyspace' AND table_name = 'cassandrakeyvalue'
```

### Top 10 response-time outlier queries

```sql
yugabyte=# SELECT query FROM ycql_stat_statements ORDER BY stddev_time DESC LIMIT 10;
```

```output
                                             query
------------------------------------------------------------------------------------------------
 SELECT * FROM system.local WHERE key='local'
 SELECT * FROM system.peers_v2
 SELECT * FROM system_schema.tables
 INSERT INTO CassandraKeyValue (k, v) VALUES (?, ?);
 SELECT * FROM system_schema.columns
 select cluster_name from system.local where key = 'local'
 SELECT k, v FROM CassandraKeyValue WHERE k = ?;
 SELECT * FROM system_schema.indexes
 SELECT keyspace_name, table_name, start_key, end_key, replica_addresses FROM system.partitions
 SELECT * FROM system_schema.aggregates
```
