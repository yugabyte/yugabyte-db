---
title: Monitor with Active Session History
headerTitle: Monitor with Active Session History
linkTitle: Active Session History
description: Use Active Session History to monitor and troubleshoot performance issues in YugabyteDB.
headContent: Use Active Session History to monitor and troubleshoot performance issues in YugabyteDB
menu:
  stable:
    parent: monitor-and-alert
    identifier: ash-monitor
    weight: 120
type: docs
rightNav:
  hideH4: true
---

[Active Session History](../../../explore/observability/active-session-history/) (ASH) provides a powerful way to troubleshoot performance by giving you a real-time and historical view of your database's activity. ASH captures samples of active sessions and exposes them through a set of SQL views. By querying these views, you can analyze wait events, identify performance bottlenecks, and understand where your database is spending its time.

ASH is currently available for YSQL, YCQL, and YB-TServer and records wait events like CPU, WaitOnCondition, RPCWait, and Disk IO.

By analyzing this data, you can troubleshoot performance by answering questions like:

- Why is a specific query or application slow?

- Which queries are causing the most database load?

## Configure ASH

To configure ASH, you can set the following YB-TServer flags for each node of your cluster.

| Flag | Description |
| :--- | :---------- |
| ysql_yb_enable_ash | Enables ASH. Changing this flag requires a TServer restart. Default: true |
| ysql_yb_ash_circular_buffer_size | Size (in KiB) of circular buffer where the samples are stored. <br> Defaults:<ul><li>32 MiB for 1-2 cores</li><li>64 MiB for 3-4 cores</li><li>128 MiB for 5-8 cores</li><li>256 MiB for 9-16 cores</li><li>512 MiB for 17-32 cores</li><li>1024 MiB for more than 32 cores</li></ul> Changing this flag requires a TServer restart. |
| ysql_yb_ash_sampling_interval_ms | Sampling interval (in milliseconds). Changing this flag doesn't require a TServer restart. Default: 1000 |
| ysql_yb_ash_sample_size | Maximum number of events captured per sampling interval. Changing this flag doesn't require a TServer restart. Default:  500 |

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
| wait_event_type | text | Type of the wait event such as CPU, WaitOnCondition, RPCWait, Disk IO, and so on. |
| wait_event_aux | text | Additional information for the wait event. For example, tablet ID for TServer wait events. |
| top_level_node_id | UUID | 16-byte TServer UUID of the YSQL/YCQL node where the query is being executed. |
| query_id | bigint | Query ID as seen on the `/statements` endpoint. This can be used to join with [pg_stat_statements](../../../launch-and-manage/monitor-and-alert/query-tuning/pg-stat-statements/)/[ycql_stat_statements](../../../launch-and-manage/monitor-and-alert/query-tuning/ycql-stat-statements/). See [Constant query identifiers](#constant-query-identifiers). |
| pid | bigint | PID of the process that is executing the query. For YCQL and background activities, this will be the YB-TServer PID. |
| client_node_ip | text | IP address of the client which sent the query to YSQL/YCQL. Null for background activities. |
| sample_weight | float | If in any sampling interval there are too many events, YugabyteDB only collects `ysql_yb_ash_sample_size` samples/events. Based on how many were sampled, weights are assigned to the collected events. <br><br>For example, if there are 200 events, but only 100 events are collected, each of the collected samples will have a weight of (200 / 100) = 2.0 |
| ysql_dbid | oid | Database OID of the YSQL database. This is 0 for YCQL databases.  |

### yb_wait_event_desc

This view displays the class, type, name, and description of each wait event. The columns of the view are described in the following table.

| Column | Type | Description |
| :----- | :--- | :---------- |
| wait_event_class | text | Class of the wait event, such as TabletWait, RocksDB, and so on. |
| wait_event_type | text | Type of the wait event such as CPU, WaitOnCondition, RPCWait, Disk IO, and so on. |
| wait_event | text | Name of the wait event. |
| wait_event_description | text | Description of the wait event. |

## Constant query identifiers

These fixed constants are used to identify various YugabyteDB background activities. The query IDs of the fixed constants are described in the following table.

| Query ID | Wait Event Component | Description |
| :------- | :------------------- | :---------- |
| 1 | TServer | Query ID for write ahead log (WAL) appender thread. |
| 2 | TServer | Query ID for background flush tasks. |
| 3 | TServer | Query ID for background compaction tasks. |
| 4 | TServer | Query ID for Raft update consensus. |
| 5 | YSQL/TServer | Default query ID, assigned in the interim before pg_stat_statements calculates a proper ID for the query. |
| 6 | TServer | Query ID for write ahead log (WAL) background sync. |

To obtain the IP address and location of a node where a query is being executed, use the `top_level_node_id` from the active session history view in the following command:

```sql
SELECT * FROM pg_catalog.yb_servers() WHERE uuid = <top_level_node_id>;
```

``` output
     host     | port | num_connections | node_type | cloud |  region   |    zone    | public_ip |               uuid
--------------+------+-----------------+-----------+-------+-----------+------------+-----------+----------------------------------
 10.9.111.111 | 5433 |               0 | primary   | aws   | us-west-2 | us-west-2a |           | 5cac7c86ba4e4f0e838bf180d75bcad5
```

## Wait events

The following describes the wait events available in the [active session history](#yb-active-session-history), along with their type and, where applicable, the auxiliary information (`wait_event_aux`) provided with that type. The events are categorized by wait event class.

### YSQL

These are the wait events introduced by YugabyteDB. Some of the following [wait events](https://www.postgresql.org/docs/15/monitoring-stats.html) inherited from PostgreSQL might also show up in the [yb_active_session_history](#yb-active-session-history) view.

#### TServerWait class

| Wait Event | Type | Aux | Description |
| :--------- | :--- |:--- | :---------- |
| TableRead | RPCWait |  | A YSQL backend is waiting for a table read from DocDB. |
| CatalogRead | RPCWait |   | A YSQL backend is waiting for a catalog read from master. |
| IndexRead | RPCWait |   | A YSQL backend is waiting for a secondary index read from DocDB.  |
| StorageFlush  | RPCWait |  | A YSQL backend is waiting for a table/index read/write from DocDB. |
| TableWrite  | RPCWait |  | A YSQL backend is waiting for a table write from DocDB. |
| CatalogWrite  | RPCWait |  | A YSQL backend is waiting for a catalog write from master. |
| IndexWrite | RPCWait |   | A YSQL backend is waiting for a secondary index write from DocDB.  |
| WaitingOnTServer | RPCWait | RPC&nbsp;name | A YSQL backend is waiting on TServer for an RPC. The RPC name is present on the wait event aux column.|

#### YSQLQuery class

| Wait Event | Type |  Description |
| :--------- | :--- | :---------- |
| QueryProcessing| CPU | A YSQL backend is doing CPU work.|
| yb_ash_metadata | LWLock | A YSQL backend is waiting to update ASH metadata for a query. |
| YBParallelScanEmpty| IPC | A YSQL backend is waiting on an empty queue while fetching parallel range keys. |
| CopyCommandStreamRead| IO | A YSQL backend is waiting for a read from a file or program during COPY. |
| CopyCommandStreamWrite| IO | A YSQL backend is waiting for a write to a file or program during COPY. |
| YbAshMain| Activity | The YugabyteDB ASH collector background worker is waiting in the main loop. |
| YbAshCircularBuffer| LWLock | A YSQL backend is waiting for YugabyteDB ASH circular buffer memory access. |
| QueryDiagnosticsMain| Activity | The YugabyteDB query diagnostics background worker is waiting in the main loop. |
| YbQueryDiagnostics| LWLock | A YSQL backend is waiting for YugabyteDB query diagnostics hash table memory access. |
| YbQueryDiagnosticsCircularBuffer| LWLock | A YSQL backend is waiting for YugabyteDB query diagnostics circular buffer memory access. |
| YBTxnConflictBackoff | Timeout | A YSQL backend is waiting for transaction conflict resolution with an exponential backoff. |

### YB-TServer

#### Common class

| Wait Event | Type |  Description |
| :--------- | :--- | :---------- |
| OnCpu_Passive | CPU | An RPC or task is waiting for a thread to pick it up. |
| OnCpu_Active | CPU | An RPC or task is being actively processed on a thread. |
| Idle | WaitOnCondition | The Raft log appender/sync thread is idle. |
| Rpc_Done | WaitOnCondition | An RPC is done and waiting for the reactor to send the response to a YSQL/YCQL backend. |
| RetryableRequests_SaveToDisk | DiskIO | The in-memory state of the retryable requests is being saved to the disk. |

#### TabletWait class

| Wait Event | Type |  Description |
| :--------- | :--- | :---------- |
| MVCC_WaitForSafeTime | WaitOnCondition | A read/write RPC is waiting for the safe time to be at least the desired read-time. |
| LockedBatchEntry_Lock | WaitOnCondition | A read/write RPC is waiting for a DocDB row-level lock. |
| BackfillIndex_WaitForAFreeSlot | WaitOnCondition | A backfill index RPC is waiting for a slot to open if there are too many backfill requests at the same time. |
| CreatingNewTablet | DiskIO | The CreateTablet RPC is creating a new tablet, this may involve writing metadata files, causing I/O wait. |
| SaveRaftGroupMetadataToDisk | DiskIO | The Raft/tablet metadata is being written to disk, generally during snapshot or restore operations. |
| TransactionStatusCache_DoGetCommitData | RPCWait | An RPC needs to look up the commit status of a particular transaction. |
| WaitForYSQLBackendsCatalogVersion | WaitOnCondition | CREATE INDEX is waiting for YSQL backends to have up-to-date pg_catalog. |
| WriteSysCatalogSnapshotToDisk | DiskIO | Writing initial system catalog snapshot during initdb.|
| DumpRunningRpc_WaitOnReactor | WaitOnCondition | DumpRunningRpcs is waiting on reactor threads. |
| ConflictResolution_ResolveConficts | RPCWait | A read/write RPC is waiting to identify conflicting transactions. |
| ConflictResolution_WaitOnConflictingTxns | WaitOnCondition | A read/write RPC is waiting for conflicting transactions to complete. |
| WaitForReadTime | WaitOnCondition | A read/write RPC is waiting for the current time to catch up to [read time](../../../architecture/transactions/single-row-transactions/#safe-timestamp-assignment-for-a-read-request). |

#### Consensus class

| Wait Event | Type | Aux | Description |
| :--------- | :--- |:--- | :---------- |
| WAL_Append | DiskIO | Tablet&nbsp;ID | A write RPC is persisting WAL edits. |
| WAL_Sync | DiskIO | Tablet ID | A write RPC is synchronizing WAL edits. |
| Raft_WaitingForReplication | RPCWait | Tablet ID | A write RPC is waiting for Raft replication. |
| Raft_ApplyingEdits | WaitOnCondition/CPU | Tablet ID | A write RPC is applying Raft edits locally. |
| ConsensusMeta_Flush | DiskIO | | ConsensusMetadata is flushed, for example, during Raft term, configuration change, remote bootstrap, and so on. |
| ReplicaState_TakeUpdateLock | WaitOnCondition | | A write/alter RPC needs to wait for the ReplicaState lock to replicate a batch of writes through Raft. |

#### RocksDB class

| Wait Event | Type |  Description |
| :--------- | :--- | :---------- |
| RocksDB_ReadBlockFromFile | DiskIO | RocksDB is reading a block from a file. |
| RocksDB_OpenFile | DiskIO | RocksDB is opening a file. |
| RocksDB_WriteToFile | DiskIO | RocksDB is writing to a file. |
| RocksDB_Flush | CPU | RocksDB is doing a flush. |
| RocksDB_Compaction | CPU | RocksDB is doing a compaction. |
| RocksDB_PriorityThreadPoolTaskPaused | WaitOnCondition | RocksDB pauses a flush or compaction task for another flush or compaction task with a higher priority. |
| RocksDB_CloseFile | DiskIO | RocksDB is closing a file. |
| RocksDB_RateLimiter | WaitOnCondition | RocksDB flush/compaction is slowing down due to rate limiter throttling access to disk. |
| RocksDB_WaitForSubcompaction | WaitOnCondition | RocksDB is waiting for a compaction to complete. |
| RocksDB_NewIterator | DiskIO | RocksDB is waiting for a new iterator to be created. |

### YCQL

#### YCQLQuery class

| Wait Event | Type | Aux | Description |
| :--------- | :--- |:--- | :---------- |
| YCQL_Parse | CPU  | | YCQL is parsing a query. |
| YCQL_Read | CPU | Table&nbsp;ID | YCQL is processing a read query.|
| YCQL_Write | CPU | Table ID | YCQL is processing a write query.  |
| YCQL_Analyze | CPU |  | YCQL is analyzing a query. |
| YCQL_Execute | CPU |  | YCQL is executing a query. |

#### Client class

| Wait Event | Type | Description |
| :--------- | :--- | :---------- |
| YBClient_WaitingOnDocDB | RPCWait | YB Client is waiting on DocDB to return a response. |
| YBClient_LookingUpTablet | RPCWait | YB Client is looking up tablet information from the master. |

## Limitations

Note that the following limitations are subject to change.

- ASH is available per node and is not aggregated across the cluster.
- ASH is not available for [YB-Master](../../../architecture/yb-master/) processes.
- ASH is available for queries and a few background activities like compaction and flushes. ASH support for other background activities will be added in future releases.

<!-- While ASH is not available for most background activities such as backups, restore, remote bootstrap, CDC, tablet splitting. ASH is available for flushes and compactions.
Work done in the TServer process is tracked, even for remote-bootstrap etc. However, we do not collect them under a specific query-id of sorts.

copy/export done using scripts outside of the TServer process is not tracked.
-->

## Learn more

- Active Session History [examples](../../../explore/observability/active-session-history/#examples)
