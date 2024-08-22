---
title: Active Session History capability in YugabyteDB
headerTitle: Active Session History
linkTitle: Active Session History
description: Use Active Session History to get current and past views of the database system activity.
headcontent: Get real-time and historical information about active sessions to analyze and troubleshoot performance issues
badges: tp
menu:
  preview:
    identifier: ash
    parent: explore-observability
    weight: 860
type: docs
---

Active Session History (ASH) provides a current and historical view of system activity by sampling session activity in the database. A database session or connection is considered active if it is consuming CPU, or has an active RPC call that is waiting on one of the wait events.

ASH exposes session activity in the form of [SQL views](../../ysql-language-features/advanced-features/views/) so that you can run analytical queries, aggregations for analysis, and troubleshoot performance issues. To run ASH queries, you need to enable [YSQL](../../../api/ysql/).

Currently, ASH is available for [YSQL](../../../api/ysql/), [YCQL](../../../api/ycql/), and [YB-TServer](../../../architecture/yb-tserver/). ASH facilitates analysis by recording wait events related to YSQL, YCQL, or YB-TServer requests while they are being executed. These wait events belong to the categories including but not limited to _CPU_, _WaitOnCondition_, _Network_, and _Disk IO_.

Analyzing the wait events and wait event types lets you troubleshoot, answer the following questions, and subsequently tune performance:

- Why is a query taking longer than usual to execute?
- Why is a particular application slow?
- What are the queries that are contributing significantly to database load and performance?

## Configure ASH

To use ASH, enable and configure the following flags for each node of your cluster.

| Flag | Description |
| :--- | :---------- |
| allowed_preview_flags_csv | Set the value of this flag to include `ysql_yb_ash_enable_infra,ysql_yb_enable_ash`. |
| ysql_yb_ash_enable_infra | Enable or disable ASH infrastructure. <br>Default: false. Changing this flag requires a VM restart. |
| ysql_yb_enable_ash | Works only in conjunction with the flag `ysql_yb_ash_enable_infra`. Setting this flag to true enables the collection of wait events for YSQL and YCQL queries, and YB-TServer requests.<br> Default: false. Changing this flag doesn't require a VM restart. |

### Additional flags

You can also use the following flags based on your requirements.

| Flag | Description |
| :--- | :---------- |
| ysql_yb_ash_circular_buffer_size | Size (in KBs) of circular buffer where the samples are stored. <br> Default: 16*1024. Changing this flag requires a VM restart. |
| ysql_yb_ash_sampling_interval_ms | Sampling interval (in milliseconds). <br>Default: 1000. Changing this flag doesn't require a VM restart. |
| ysql_yb_ash_sample_size | Maximum number of events captured per sampling interval. <br>Default: 500. Changing this flag doesn't require a VM restart. |

## Limitations

Note that the following limitations are subject to change as the feature is in [Tech Preview](/preview/releases/versioning/#feature-maturity).

- ASH is available per node and is not aggregated across the cluster.
- ASH is not available for [YB-Master](../../../architecture/yb-master/) processes.
- ASH is available for queries and a few background activities like compaction and flushes. ASH support for other background activities will be added in future releases.

<!-- While ASH is not available for most background activities such as backups, restore, remote bootstrap, CDC, tablet splitting. ASH is available for flushes and compactions.
Work done in the TServer process is tracked, even for remote-bootstrap etc. However, we do not collect them under a specific query-id of sorts.

copy/export done using scripts outside of the TServer process is not tracked.
-->

## YSQL views

ASH exposes the following views in each node to analyze and troubleshoot performance issues.

### yb_active_session_history

This view provides a list of wait events and their metadata. The columns of the view are described in the following table.

| Column | Type | Description |
| :----- | :--- | :---------- |
| sample_time | timestamp | Timestamp when the sample was captured |
| root_request_id | UUID | A 16-byte UUID that is generated per request. Generated for queries at YSQL/YCQL layer. |
| rpc_request_id | integer | ID for internal requests, it is a monotonically increasing number for the lifetime of a YB-TServer. |
| wait_event_component | text | Component of the wait event, which can be YSQL, YCQL, or TServer. |
| wait_event_class | text | Class of the wait event, such as TabletWait, RocksDB, and so on.  |
| wait_event | text | Name of the wait event. |
| wait_event_type | text | Type of the wait event such as CPU, WaitOnCondition, Network, Disk IO, and so on. |
| wait_event_aux | text | Additional information for the wait event. For example, tablet ID for TServer wait events. |
| top_level_node_id | UUID | 16-byte TServer UUID of the YSQL/YCQL node where the query is being executed. |
| query_id | bigint | Query ID as seen on the `/statements` endpoint. This can be used to join with [pg_stat_statements](../../query-1-performance/pg-stat-statements/)/[ycql_stat_statements](../../query-1-performance/ycql-stat-statements/). It is set as a known constant for background activities. For example, _flush_ is 2, _compaction_ is 3, and so on. |
| pid | bigint | PID of the process that is executing the query. For YCQL and background activites, this will be the YB-TServer PID. |
| client_node_ip | text | IP address of the client which sent the query to YSQL/YCQL. Null for background activities. |
| sample_weight | float | If in any sampling interval there are too many events, YugabyteDB only collects `ysql_yb_ash_sample_size` samples/events. Based on how many were sampled, weights are assigned to the collected events. <br><br>For example, if there are 200 events, but only 100 events are collected, each of the collected samples will have a weight of (200 / 100) = 2.0 |

## Wait events

List of wait events by the wait event components.

### YSQL

These are the wait events introduced by YugabyteDB, however some of the following [wait events](https://www.postgresql.org/docs/current/monitoring-stats.html) inherited from PostgreSQL might also show up in the [yb_active_session_history](#yb-active-session-history) view.

| Class | Wait Event | Wait Event Type | Wait Event Aux | Description |
| :--------------- |:---------- | :-------------- |:--- | :---------- |
| Common | Rpc_Done | WaitOnCondition | | An RPC is done and waiting for the reactor to send the response to a YSQL backend. |
| TServer Wait | StorageRead | Network  |  | Waiting for a DocDB read operation |
| TServer Wait | CatalogRead | Network  |   | Waiting for a catalog read operation |
| TServer Wait | IndexRead | Network |   | Waiting for a secondary index read operation  |
| TServer Wait | StorageFlush  | Network |  | Waiting for a storage flush request |
| YSQLQuery | QueryProcessing| CPU |  | Doing CPU work |
| YSQLQuery | yb_ash_metadata | LWLock |  | Waiting to update ASH metadata for a query |
| Timeout | YBTxnConflictBackoff | Timeout |  | Waiting due to conflict in DocDB |

### YB-TServer

| Class | Wait Event | Wait Event Type | Wait Event Aux | Description |
|:---------------- | :--------- |:--------------- | :--- | :---------- |
| Common | OnCpu_Passive | CPU | | Waiting for a thread to pick it up |
| Common | OnCpu_Active | CPU |  | RPC is being actively processed on a thread |
| Common | ResponseQueued | Network | | Waiting for response to be transferred |
| Common | Idle | WaitOnCondition | | The raft log appender/sync thread is idle |
| Common | RetryableRequests_SaveToDisk | DiskIO | | The in-memory state of the retryable requests is being saved to the disk. |
| TabletWait | MVCC_WaitForSafeTime | WaitOnCondition | | A read/write RPC is waiting for the SafeTime to be at least the desired read-time. |
| TabletWait | LockedBatchEntry_Lock | WaitOnCondition | | A read/write rpc is waiting for a DocDB row level lock. |
| TabletWait | BackfillIndex_WaitForAFreeSlot | WaitOnCondition | | A backfill index rpc is waiting for a slot to open if there are too many backfill requests at the same time. |
| TabletWait | CreatingNewTablet | DiskIO | | The CreateTablet RPC is creating a new tablet, this may involve writing metadata files, causing I/O wait. |
| TabletWait | SaveRaftGroupMetadataToDisk | DiskIO | | The raft/tablet metadata is being written to disk generally during snapshot or restore operations. |
| TabletWait | TransactionStatusCache_DoGetCommitData | Network | | An RPC needs to look up the commit status of a particular transaction. |
| TabletWait | WaitForYSQLBackendsCatalogVersion | WaitOnCondition | | CREATE INDEX is waiting for YSQL backends to have up-to-date pg_catalog. |
| TabletWait | WriteSysCatalogSnapshotToDisk | DiskIO | | Writing initial system catalog snapshot. |
| TabletWait | DumpRunningRpc_WaitOnReactor | WaitOnCondition | | DumpRunningRpcs is waiting on reactor threads. |
| TabletWait | ConflictResolution_ResolveConficts | Network | | A read/write RPC is waiting to identify conflicting transactions. |
| TabletWait | ConflictResolution_WaitOnConflictingTxns | WaitOnCondition |  | A read/write RPC is waiting for conflicting transactions to complete. |
| Consensus | WAL_Append | I/O | \<tablet-id>| Persisting Wal edits |
| Consensus | WAL_Sync | I/O | \<tablet-id>| Persisting Wal edits |
| Consensus | Raft_WaitingForReplication | Network | \<tablet-id>| Waiting for Raft replication |
| Consensus | Raft_ApplyingEdits | Lock/CPU | \<tablet-id>| Applying the edits locally |
| Consensus | ConsensusMeta_Flush | DiskIO | | ConsensusMetadata is flushed, for example, during raft term, configuration change, Remote bootstrap, and so on. |
| Consensus | ReplicaState_TakeUpdateLock | WaitOnCondition | | A write/alter RPC needs to wait for the ReplicaState lock to replicate a batch of writes through Raft. |
| RocksDB | RocksDB_ReadBlockFromFile | DiskIO  | | RocksDB is reading a block from a file. |
| RocksDB | RocksDB_OpenFile | DiskIO | | RocksDB is opening a file. |
| RocksDB | RocksDB_WriteToFile | DiskIO | | RocksDB is writing to a file. |
| RocksDB | RocksDB_Flush | CPU | | RocksDB is doing a flush. |
| RocksDB | RocksDB_Compaction | CPU | | RocksDB is doing a compaction. |
| RocksDB | RocksDB_PriorityThreadPoolTaskPaused | WaitOnCondition | | RocksDB paused one of flush/compaction tasks for another one with a higher priority. |
| RocksDB | RocksDB_CloseFile | DiskIO | | RocksDB is closing a file. |
| RocksDB | RocksDB_RateLimiter | WaitOnCondition | | RocksDB flush/compaction is slowing down due to rate limiter throttling access to disk. |
| RocksDB | RocksDB_WaitForSubcompaction | WaitOnCondition | | RocksDB is waiting for a compaction to complete. |
| RocksDB | RocksDB_NewIterator | DiskIO | | RocksDB is waiting for a new iterator to be created. |

### YCQL

| Class | Wait Event | Wait Event Type | Wait Event Aux | Description |
| :--------------- |:---------- | :-------------- |:--- | :---------- |
| Common | Rpc_Done | WaitOnCondition | | An RPC is done and waiting for the reactor to send the response to a YCQL backend. |
| YCQLQuery | YCQL_Parse | CPU  | | CQL call is being actively processed |
| YCQLQuery | YCQL_Read | Network | \<table&#8209;id> | Waiting for DocDB read operation |
| YCQLQuery | YCQL_Write | Network | \<table-id> | Waiting for DocDB write operation  |
| YCQLQuery | YCQL_Analyze | CPU |  | YCQL is analyzing a query. |
| YCQLQuery | YCQL_Execute | CPU |  | YCQL is executing a query. |
| Client | YBClient_WaitingOnDocDB | Network | | YB Client is waiting on DocDB to return a response. |
| Client | YBClient_LookingUpTablet | Network | | YB Client is looking up tablet information from the master. |

## Examples

{{% explore-setup-single %}}

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
 -4157456334073660389 | YSQL                 | CatalogRead                        | Network         |     3
 -1970690938654296136 | TServer              | Raft_ApplyingEdits                 | Cpu             |    54
 -1970690938654296136 | TServer              | OnCpu_Active                       | Cpu             |   107
 -1970690938654296136 | TServer              | OnCpu_Passive                      | Cpu             |   144
 -1970690938654296136 | TServer              | RocksDB_NewIterator                | DiskIO          |     6
 -1970690938654296136 | TServer              | ConflictResolution_ResolveConficts | Network         |    18
 -1970690938654296136 | TServer              | Raft_WaitingForReplication         | Network         |   194
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
  6107501747146929242 | YSQL                 | StorageRead                        | Network         |   658
  6107501747146929242 | YSQL                 | CatalogRead                        | Network         |     1
```

### Distribution of wait events for each query

As ASH's query_id is the same as [pg_stat_statement's](../../query-1-performance/pg-stat-statements/) queryid, you can join with pg_stat_statements to view the distribution of wait events for each query. This may help in finding what's wrong with a particular query, or determine where most of the time is being spent on. In this example, you can see that in YB-TServer, most of the time is spent on the wait event `ConflictResolution_WaitOnConflictingTxns` which suggests that there are a lot of conflicts in the [DocDB storage layer](../../../architecture/docdb/).

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
 UPDATE test_table set v = v + $1 where k = $2 | TServer              | ConflictResolution_ResolveConficts       | Network         |    99
 UPDATE test_table set v = v + $1 where k = $2 | TServer              | Raft_WaitingForReplication               | Network         |    38
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

You can join with `yb_local_tablets` to get more information about the table type, table_name, and partition keys. As the `wait_event_aux` has only the first 15 characters of the `tablet_id` as a string, you have to join with only the first 15 characters from `yb_local_tablets`.

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

Detect where most time is being spent to help understand if the issue is related to disk IO, network operations, waiting for a condition or lock, or intense CPU work.

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
 TServer              | Network         |   910
 TServer              | Cpu             |  2665
 TServer              | DiskIO          |  8193
 YSQL                 | LWLock          |     1
 YSQL                 | Cpu             |  1479
 YSQL                 | Network         |  4575
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
