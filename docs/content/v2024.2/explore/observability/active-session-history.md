---
title: Active Session History capability in YugabyteDB
headerTitle: Active Session History
linkTitle: Active Session History
description: Use Active Session History to get current and past views of the database system activity.
headcontent: Get real-time and historical information about active sessions to analyze and troubleshoot performance issues
tags:
  feature: early-access
menu:
  v2024.2:
    identifier: ash
    parent: explore-observability
    weight: 860
type: docs
---

Active Session History (ASH) provides a current and historical view of system activity by sampling session activity in the database. A database session or connection is considered active if it is consuming CPU, or has an active RPC call that is waiting on one of the wait events.

ASH exposes session activity in the form of [SQL views](../../ysql-language-features/advanced-features/views/) so that you can run analytical queries, aggregations for analysis, and troubleshoot performance issues.

Currently, ASH is available for [YSQL](../../../api/ysql/), [YCQL](../../../api/ycql/), and [YB-TServer](../../../architecture/yb-tserver/). ASH facilitates analysis by recording wait events related to YSQL, YCQL, or YB-TServer requests while they are being executed. These wait events belong to the categories including but not limited to _CPU_, _WaitOnCondition_, _Network_, and _Disk IO_.

Analyzing the wait events and wait event types lets you troubleshoot, answer the following questions, and subsequently tune performance:

- Why is a query taking longer than usual to execute?
- Why is a particular application slow?
- What are the queries that are contributing significantly to database load and performance?

{{< note title="YSQL must be enabled" >}}

To run ASH queries, regardless of whether you are using YSQL or YCQL, the YSQL API must be enabled on your universe. For more information, refer to [Configure ASH](../../../launch-and-manage/monitor-and-alert/active-session-history-monitor/#configure-ash).

{{< /note >}}

## Configuration and usage

- How to [Configure ASH](../../../launch-and-manage/monitor-and-alert/active-session-history-monitor/#configure-ash)
- See [Monitor with Active Session History](../../../launch-and-manage/monitor-and-alert/active-session-history-monitor/) for information on YSQL views, query identifiers, and wait events that are exposed via active sessions captured by ASH.
- ASH [limitations](../../../launch-and-manage/monitor-and-alert/active-session-history-monitor/#limitations)

## Examples

{{% explore-setup-single-new %}}

Make sure you have an active ysqlsh session (`./bin/ysqlsh`) to run the following examples.

### Distribution of wait events for each query_id

Check the distribution of wait events for each query_id, for the last 20 minutes.

```sql
SELECT
    query_id,
    wait_event_component,
    wait_event,
    wait_event_type,
    COUNT(*)
FROM
    yb_active_session_history
WHERE
    sample_time >= current_timestamp - interval '20 minutes'
GROUP BY
    query_id,
    wait_event_component,
    wait_event,
    wait_event_type
ORDER BY
    query_id,
    wait_event_component,
    wait_event_type;
```

```output
 query_id             | wait_event_component |             wait_event             | wait_event_type | count
 ---------------------+----------------------+------------------------------------+-----------------+------------
 -4157456334073660389 | YSQL                 | CatalogRead                        | RPCWait         |     3
 -1970690938654296136 | TServer              | Raft_ApplyingEdits                 | Cpu             |    54
 -1970690938654296136 | TServer              | OnCpu_Active                       | Cpu             |   107
 -1970690938654296136 | TServer              | OnCpu_Passive                      | Cpu             |   144
 -1970690938654296136 | TServer              | RocksDB_NewIterator                | DiskIO          |     6
 -1970690938654296136 | TServer              | ConflictResolution_ResolveConficts | RPCWait         |    18
 -1970690938654296136 | TServer              | Raft_WaitingForReplication         | RPCWait         |   194
 -1970690938654296136 | TServer              | Rpc_Done                           | WaitOnCondition |    18
 -1970690938654296136 | TServer              | MVCC_WaitForSafeTime               | WaitOnCondition |     5
 -1970690938654296136 | YSQL                 | QueryProcessing                    | Cpu             |  1023
                    0 | TServer              | OnCpu_Passive                      | Cpu             |    10
                    0 | TServer              | OnCpu_Active                       | Cpu             |     9
  6107501747146929242 | TServer              | OnCpu_Active                       | Cpu             |   208
  6107501747146929242 | TServer              | RocksDB_NewIterator                | DiskIO          |     5
  6107501747146929242 | TServer              | MVCC_WaitForSafeTime               | WaitOnCondition |    10
  6107501747146929242 | TServer              | Rpc_Done                           | WaitOnCondition |    15
  6107501747146929242 | YSQL                 | QueryProcessing                    | Cpu             |   285
  6107501747146929242 | YSQL                 | TableRead                          | RPCWait         |   658
  6107501747146929242 | YSQL                 | CatalogRead                        | RPCWait         |     1
```

### Distribution of wait events for each query

As ASH's query_id is the same as the [pg_stat_statement](../../../launch-and-manage/monitor-and-alert/query-tuning/pg-stat-statements/) queryid, you can join with pg_stat_statements to view the distribution of wait events for each query. This may help in finding what's wrong with a particular query, or determine where most of the time is being spent on. In this example, you can see that in YB-TServer, most of the time is spent on the wait event `ConflictResolution_WaitOnConflictingTxns` which suggests that there are a lot of conflicts in the [DocDB storage layer](../../../architecture/docdb/).

```sql
SELECT
    SUBSTRING(query, 1, 50) AS query,
    wait_event_component,
    wait_event,
    wait_event_type,
    COUNT(*)
FROM
    yb_active_session_history
JOIN
    pg_stat_statements
ON
    query_id = queryid
WHERE
    sample_time >= current_timestamp - interval '20 minutes'
GROUP BY
    query,
    wait_event_component,
    wait_event,
    wait_event_type
ORDER BY
    query,
    wait_event_component,
    wait_event_type;
```

```output
                query                          | wait_event_component |             wait_event                   | wait_event_type | count
-----------------------------------------------+----------------------+------------------------------------------+-----------------+------------
 UPDATE test_table set v = v + $1 where k = $2 | TServer              | OnCpu_Passive                            | Cpu             |    46
 UPDATE test_table set v = v + $1 where k = $2 | TServer              | Raft_ApplyingEdits                       | Cpu             |    34
 UPDATE test_table set v = v + $1 where k = $2 | TServer              | OnCpu_Active                             | Cpu             |    39
 UPDATE test_table set v = v + $1 where k = $2 | TServer              | RocksDB_NewIterator                      | DiskIO          |     3
 UPDATE test_table set v = v + $1 where k = $2 | TServer              | ConflictResolution_ResolveConficts       | RPCWait         |    99
 UPDATE test_table set v = v + $1 where k = $2 | TServer              | Raft_WaitingForReplication               | RPCWait         |    38
 UPDATE test_table set v = v + $1 where k = $2 | TServer              | ConflictResolution_WaitOnConflictingTxns | WaitOnCondition |  1359
 UPDATE test_table set v = v + $1 where k = $2 | TServer              | Rpc_Done                                 | WaitOnCondition |     5
 UPDATE test_table set v = v + $1 where k = $2 | TServer              | LockedBatchEntry_Lock                    | WaitOnCondition |   141
 UPDATE test_table set v = v + $1 where k = $2 | YSQL                 | QueryProcessing                          | Cpu             |  1929
```

### Detect a hot shard

In this example, you can see that a particular tablet is getting a lot of requests as compared to the other tablets. The `wait_event_aux` field contains the `tablet_id` in case of YB-TServer events.

```sql
SELECT
    wait_event_aux AS tablet_id,
    COUNT(*)
FROM
    yb_active_session_history
WHERE
    wait_event_component = 'TServer' AND
    wait_event_aux IS NOT NULL
GROUP BY
    wait_event_aux
ORDER BY
    count DESC;
```

```output
    tablet_id    | count
-----------------+-------
 09f26a0bb117411 | 33129
 a1d82ef77aa64a8 |  5235
 31bc90e0c59e4da |  2431
 7b49c915e7fe4f1 |  1518
 6b6a264711a84d2 |   403
 96948dbb19674cb |   338
 e112a0dd35994e5 |   320
 f901168f334f432 |   315
 bddebf9b7d9b485 |   310
 04a37ec2cecf49e |    70
 70f6e424970c44c |    66
 77bdebc4f7e3400 |    65
 b4ae6f1115fc4a9 |    63
 8674a0708cba422 |    63
 9cf4fc4a834040d |    61
 e66879054249434 |    61
 c2cfa997bf63463 |    59
 9d64f3479792499 |    58
 e70fd34078e84fe |    58
 8ea1aa0f2e4749a |    56
 b3bbaec3014f4f1 |    53
 d3bbd37828ab422 |    53
 542c6f91ff6a403 |    52
 27780cde5a1b445 |    50
 4a64a9f25e414ce |    44
 09bb0274a41146a |     5
 d58c56ce3fc7458 |     4
 0350744dad944bd |     4
 219fd39bafee44a |     3
```

You can join with `yb_local_tablets` to get more information about the table type, table name, and partition keys. As the `wait_event_aux` has only the first 15 characters of the `tablet_id` as a string, you have to join with only the first 15 characters from `yb_local_tablets`.

```sql
SELECT
    tablet_id,
    table_type,
    namespace_name,
    table_name,
    partition_key_start,
    partition_key_end,
    COUNT(*)
FROM
    yb_active_session_history
JOIN
    yb_local_tablets
ON
    wait_event_aux = SUBSTRING(tablet_id, 1, 15)
GROUP BY
    tablet_id,
    table_type,
    namespace_name,
    table_name,
    partition_key_start,
    partition_key_end
ORDER BY
    table_name,
    count DESC;
```

```output
            tablet_id             | table_type | namespace_name |  table_name  | partition_key_start | partition_key_end | count
----------------------------------+------------+----------------+--------------+---------------------+-------------------+-------
 09f26a0bb117411fa068df13420ea643 | YSQL       | yugabyte       | test_table   |                     | \x2aaa            | 33129
 d58c56ce3fc74584bd2eb892cea51e2a | YSQL       | yugabyte       | test_table   | \x5555              | \x8000            |    10
 0350744dad944bd9ba70c91432c5d8e2 | YSQL       | yugabyte       | test_table   | \xaaaa              | \xd555            |     9
 219fd39bafee44a59117c4089a2f71bf | YSQL       | yugabyte       | test_table   | \x2aaa              | \x5555            |     8
 09bb0274a41146abbb6fe70e41b4f3c1 | YSQL       | yugabyte       | test_table   | \xd555              |                   |     7
 a1d82ef77aa64a85987eef4aa2322c11 | System     | system         | transactions | \xf555              |                   |  7973
 31bc90e0c59e4da5b36e38e9e48554a0 | System     | system         | transactions | \x9555              | \xa000            |  5169
 7b49c915e7fe4f13afdee958028d4446 | System     | system         | transactions | \xb555              | \xc000            |  4256
 6b6a264711a84d25b3226f78b9dd4d6f | System     | system         | transactions |                     | \x0aaa            |   403
 96948dbb19674cb596c7f69177533000 | System     | system         | transactions | \x7555              | \x8000            |   338
 e112a0dd35994e5990b25c4ae8a00eb6 | System     | system         | transactions | \x5555              | \x6000            |   320
 f901168f334f43289007ee8385fedb67 | System     | system         | transactions | \x8aaa              | \x9555            |   315
 bddebf9b7d9b4858b4e502b8a1d93e01 | System     | system         | transactions | \x6aaa              | \x7555            |   310
 04a37ec2cecf49e5bdb974ee80bb5c97 | System     | system         | transactions | \x2000              | \x2aaa            |    73
 70f6e424970c44c391edbbc18225ccb0 | System     | system         | transactions | \x0aaa              | \x1555            |    69
 77bdebc4f7e340068ad3d8c6ccf74b35 | System     | system         | transactions | \xeaaa              | \xf555            |    69
 b4ae6f1115fc4a94b2d9e7aa514b5f28 | System     | system         | transactions | \xe000              | \xeaaa            |    67
 8674a0708cba4228bd1d435a642134bb | System     | system         | transactions | \xaaaa              | \xb555            |    66
 8ea1aa0f2e4749a4802986fefbf54d43 | System     | system         | transactions | \xd555              | \xe000            |    66
 e70fd34078e84fef945f28fe6b004309 | System     | system         | transactions | \x8000              | \x8aaa            |    64
 e66879054249434da01b3e7a314b983c | System     | system         | transactions | \x4000              | \x4aaa            |    63
 9d64f34797924998911922563d3ad0ec | System     | system         | transactions | \xa000              | \xaaaa            |    63
 9cf4fc4a834040df91987b26bf9831ed | System     | system         | transactions | \x4aaa              | \x5555            |    63
 c2cfa997bf634637880137870cee25a1 | System     | system         | transactions | \xcaaa              | \xd555            |    61
 d3bbd37828ab4225a68bf4dd54924138 | System     | system         | transactions | \x1555              | \x2000            |    56
 b3bbaec3014f4f1aa01e04b42c5f40c7 | System     | system         | transactions | \x3555              | \x4000            |    56
 542c6f91ff6a403399ddf46e4d5f29bb | System     | system         | transactions | \x6000              | \x6aaa            |    56
 27780cde5a1b445d83ff83c865b9bea5 | System     | system         | transactions | \xc000              | \xcaaa            |    51
 4a64a9f25e414ce7a649be2e6d24c7ee | System     | system         | transactions | \x2aaa              | \x3555            |    46
```

You can see that only a single tablet of the table `test_table` is getting most of the requests, and you have the partition range of that particular tablet.

### Distribution of the type of the wait events for each component

Detect where most time is being spent to help understand if the issue is related to disk IO, RPC calls, waiting for a condition or lock, or intense CPU work.

```sql
SELECT
    wait_event_component,
    wait_event_type,
    COUNT(*)
FROM
    yb_active_session_history
GROUP BY
    wait_event_component,
    wait_event_type
ORDER BY
    wait_event_component,
    count;
```

```output
 wait_event_component | wait_event_type | count
----------------------+-----------------+-------
 TServer              | WaitOnCondition |    47
 TServer              | RPCWait         |   910
 TServer              | Cpu             |  2665
 TServer              | DiskIO          |  8193
 YSQL                 | LWLock          |     1
 YSQL                 | Cpu             |  1479
 YSQL                 | RPCWait         |  4575
```

### Detect which client/application is sending the most amount of queries

Suppose you have a rogue application which is sending a lot of queries, you can identify the application using the following example query:

```sql
SELECT
    client_node_ip,
    COUNT(*)
FROM
    yb_active_session_history
WHERE
    client_node_ip IS NOT NULL
GROUP BY
    client_node_ip;
```

```output
 client_node_ip  |  count
-----------------+-------
 127.0.0.2:56471 |    92
 127.0.0.1:56473 |   106
 127.0.0.1:56477 |    91
 127.0.0.2:56481 |    51
 127.0.0.3:56475 |    53
 127.0.0.3:56485 |    18
 127.0.0.3:56479 | 10997
```

### Augment ASH with topology information

By joining ASH with [yb_servers()](../../going-beyond-sql/cluster-topology/), you can augment ASH views with information about the location of the nodes involved, including the IP address, cloud, region, and zone.

The `yb_servers()` function returns a list of all the nodes in your cluster and their location, and includes a `uuid` column with the same IDs as the `top_level_node_id` column in the `yb_active_session_history` view.

Note that because these columns have different data types, (`top_level_node_id` is type UUID, while the `uuid` column of `yb_servers()` is type text), you need to cast the text to UUID to perform the join.

For example:

```sql
SELECT
    SUBSTRING(query, 1, 50) AS query,
    top_level_node_id,
    host,
    port,
    cloud,
    region,
    zone,
    COUNT(*)
FROM
    yb_active_session_history
JOIN
    pg_stat_statements
ON
    query_id = queryid
JOIN
    yb_servers()
ON
    top_level_node_id = uuid::uuid
WHERE
    sample_time >= current_timestamp - interval '20 minutes'
GROUP BY
    query,
    top_level_node_id,
    host,
    port,
    cloud,
    region,
    zone;
```

```output
                       query                        |          top_level_node_id           |   host    | port | cloud | region  |    zone    | count 
----------------------------------------------------+--------------------------------------+-----------+------+-------+---------+------------+-------
 COMMIT                                             | 6b556919-0198-4617-a7bc-42b84c965ec4 | 127.0.0.1 | 5433 | aws   | us-west | us-west-2a |     2
 ANALYZE "public"."postgresqlkeyvalue"              | 6b556919-0198-4617-a7bc-42b84c965ec4 | 127.0.0.1 | 5433 | aws   | us-west | us-west-2a |    44
 SET extra_float_digits = 3                         | 6b556919-0198-4617-a7bc-42b84c965ec4 | 127.0.0.1 | 5433 | aws   | us-west | us-west-2a |     2
 SHOW yb_disable_auto_analyze                       | 6b556919-0198-4617-a7bc-42b84c965ec4 | 127.0.0.1 | 5433 | aws   | us-west | us-west-2a |     1
 SELECT k, v FROM postgresqlkeyvalue WHERE k = $1   | 6b556919-0198-4617-a7bc-42b84c965ec4 | 127.0.0.1 | 5433 | aws   | us-west | us-west-2a |  1450
 BEGIN                                              | 6b556919-0198-4617-a7bc-42b84c965ec4 | 127.0.0.1 | 5433 | aws   | us-west | us-west-2a |     2
 SET application_name = 'PostgreSQL JDBC Driver'    | 6b556919-0198-4617-a7bc-42b84c965ec4 | 127.0.0.1 | 5433 | aws   | us-west | us-west-2a |     1
 SELECT reltuples FROM pg_class WHERE relfilenode = | 6b556919-0198-4617-a7bc-42b84c965ec4 | 127.0.0.1 | 5433 | aws   | us-west | us-west-2a |     2
(8 rows)
```
