---
title: Active Session History capability in YugabyteDB
headerTitle: Active Session History
linkTitle: Active Session History
description: Use Active Session History to get current and past views of the database system activity.
headcontent: Get real-time and historical information about active sessions to analyze and troubleshoot performance issues
techPreview: /preview/releases/versioning/#feature-availability
menu:
  preview:
    identifier: ash
    parent: explore-observability
    weight: 860
type: docs
---

Active Session History (ASH) provides a current and historical view of system activity by sampling session activity in the database. A database session or connection is considered active if it is consuming CPU, or has an active RPC call that is waiting on one of the wait events.

ASH exposes session activity in the form of [SQL views](../../ysql-language-features/advanced-features/views/) so that you can run analytical queries, aggregations for analysis, and troubleshoot performance issues. To run ASH, you need to enable [YSQL](../../../api/ysql/) or [YCQL](../../../api/ycql/) for their respective sessions.

Currently, ASH is available for [YSQL](../../../api/ysql/), [YCQL](../../../api/ycql/), and [YB-TServer](../../../architecture/yb-tserver/) processes. ASH facilitates analysis by recording wait events related to YSQL, YCQL, or YB-TServer requests while they are being executed. These wait events belong to the categories including but not limited to _CPU_, _WaitOnCondition_, _Network_, and _Disk IO_.

Analyzing the wait events and wait event types lets you troubleshoot, answer the following questions, and subsequently tune performance:

- Why is a query taking longer than usual to execute?
- Why is a particular application slow?
- What are the queries that are contributing significantly to database load and performance?

## Key terminology

The following terms include definitions of some important column identifiers related to ASH.

- `root_request_id`: A unique ID for the top-level request. Typically this corresponds to a user issued DML query, (either via YSQL or YCQL APIs).
Note that not all background operations are currently supported.

- `rpc_request_id`: When processing a root or top-level request, many internal requests may be generated. Each of these requests will have a different ID.

- `query_id`: A hash code to identify identical normalized queries as there may be many active requests in the system (that is, with different `root_request_id`'s) with the same `query_id`.

- `top_level_node_id`: ID of the node where the top-level YSQL/YCQL query (corresponding to `query_id`) is being processed.

## Configure ASH

To use ASH, enable and configure the following flags for each node of your cluster.

| Flag | Description |
| :--- | :---------- |
| allowed_preview_flags_csv | Pass the flags `ysql_yb_ash_enable_infra` and `ysql_yb_enable_ash` in this flag in CSV format. |
| ysql_yb_ash_enable_infra | Enable or disable ASH infrastructure. <br>Default: false. Changing this flag requires a system restart. |
| ysql_yb_enable_ash | Works only in conjunction with the flag `ysql_yb_ash_enable_infra`. Start sampling and instrumentation (that is, periodically check and keep track) of YSQL and YCQL queries, and YB-TServer requests.<br> Default: false. Changing this flag doesn't require a system restart. |

### Additional flags

You can also use the following flags based on your requirements.

| Flag | Description |
| :--- | :---------- |
| ysql_yb_ash_circular_buffer_size | Size (in KBs) of circular buffer where the samples are stored. <br> Default: 16000. Changing this flag requires a system restart. |
| ysql_yb_ash_sampling_interval_ms | Time (in milliseconds) duration between two sampling events (ysql, ycql, yb-tserver). <br>Default: 1000. Changing this flag doesn't require a system restart. |
| ysql_yb_ash_sample_size | Maximum number of events captured per sampling interval. <br>Default: 500. Changing this flag doesn't require a system restart. |

## Limitations

Note that the following limitations are subject to change as the feature is in [Tech Preview](/preview/releases/versioning/#feature-availability).

- ASH is available per node only. [Aggregations](../../../develop/learn/aggregations-ycql/) need to be done by you.
- ASH is not available for [YB-Master](../../../architecture/yb-master/) processes.
- ASH is available only for foreground activities or queries from customer applications.
- ASH does not capture start and end time of wait events.

<!-- While ASH is not available for most background activities such as backups, restore, remote bootstrap, CDC, tablet splitting. ASH is available for flushes and compactions.
Work done in the TServer process is tracked, even for remote-bootstrap etc. However, we do not collect them under a specific query-id of sorts.

copy/export done using scripts outside of the TServer process is not tracked.
-->

## YSQL/YCQL views

ASH exposes the following views in each node to analyze and troubleshoot performance issues.

### yb_active_session_history

Get information on wait events for each normalized query, YSQL, or YCQL request.

| Column | Type | Description |
| :----- | :--- | :---------- |
| root_request_id | UUID | A 16-byte UUID that is generated per request. Generated for queries at YSQL/YCQL layer. |
| rpc_request_id | integer | Request ID per RPC. This is not globally unique. However, it is a monotonically increasing number for the lifetime of a YB-TServer. If a YB-TServer restarts, the number starts from 0 again, so it may not be unique across time. However, if there are no restarts, the combination of the server/rpc_request_id is unique. This could be used for advanced use cases later. For example, understanding if the same RPC is being sampled multiple times. |
| wait_event_component | text | There are three components: YSQL, YCQL, and YB-TServer. |
| wait_event_class | text | Every wait event has a class associated with it. |
| wait_event | text | Provides insight into what the RPC is waiting on. |
| wait_event_type | text | Type of the wait event such as CPU, WaitOnCondition, Network, Disk IO, and so on. |
| wait_event_aux | text | Additional information for the wait event. For example, tablet ID for YB-TServer wait events. |
| top_level_node_id | UUID | 16-byte YB-TServer UUID of the YSQL/YCQL node where the query is being executed. |
| query_id | bigint | Query ID as seen on the `/statements` endpoint. This can be used to join with [pg_stat_statements](../../query-1-performance/pg-stat-statements/)/[ycql_stat_statements](#ycql-stat-statements). For background activities, query ID is a known constant (for example, log appender is 1, flush is 2, compaction is 3, consensus is 4, and so on). |
| ysql_session_id | bigint | Same as `PgClientSessionId` (displayed as `Session id` in logs). This is 0 for YCQL and background activities, as it is a YSQL-specific field. |
| client_node_ip | text | Client IP for the RPC. For YSQL, it is the client node from where the query is generated. For YB-TServer, the YSQL/TServer node from where the RPC originated. |
| sample_weight | float | If in any sampling interval there are too many events, YugabyteDB only collects `yb_ash_sample_size` samples/events. Based on how many were sampled, weights are assigned to the collected events. <br><br>For example, if there are 200 events, but only 100 events are collected, each of the collected samples will have a weight of (200 / 100) = 2.0 |

<!--
This is not ASH related. We need to mostly create a new page
### yb_local_tablets

This view provides tablet ID metadata, state, and role information. This information is also available on the `<yb-tserver_ip>:9000/tablets` endpoint. You can join the `wait_event_aux` with `tablet_id` in case of [YB-TServer wait events](#yb-tserver).

Note that this is not an ASH-specific view, but can be used to extract more information from the ASH data.

| Column | Type | Description |
| :----- | :--- | :---------- |
| tablet_id | text | 16 byte UUID of the tablet. Same as `/tablets` endpoint. |
| table_id | text | 16 byte UUID of the table which the tablet is part of. |
| table_type | text | Type of the table. Can be YSQL, YCQL, System, or Unknown. |
| namespace_name | text | Name of the database or the keyspace. |
| ysql_schema_name | text | YSQL schema name. Empty for YCQL, System, and Unknown table types. |
| table_name | text | Name of the table which the tablet is part of. |
| partition_key_start| bytea | Start key of the partition (inclusive). |
| partition_key_end  | bytea | End key of the partition (exclusive).| -->

## Wait events

List of wait events by the following request types.

### YSQL

| Class | Wait Event | Type | AUX | Description |
| :--------------- |:---------- | :-------------- |:--- | :---------- |
| TServer Wait | StorageRead | Network  |  | Waiting for a DocDB read operation |
| TServer Wait | CatalogRead | Network  |   | Waiting for a catalog read operation |
| TServer Wait | IndexRead | Network |   | Waiting for a secondary index read operation  |
| TServer Wait | StorageFlush  | Network |  | Waiting for a storage flush request |
| YSQLQuery | QueryProcessing| CPU |  | Doing CPU work |
| YSQLQuery | yb_ash_metadata | LWLock |  | Waiting to update ASH metadata for a query |
| Timeout | YBTxnConflictBackoff | Timeout |  | PG process sleeping due to conflict in DocDB |
| Timeout | PgSleep | Timeout |  | PG process sleeping due to pg_sleep(N) |

### YB-TServer

| Class | Wait Event | Type | AUX | Description |
|:---------------- | :--------- |:--------------- | :--- | :---------- |
| Common | OnCpu_Passive | CPU | | Waiting for a thread to pick it up |
| Common | OnCpu_Active | CPU |  | RPC is being actively processed on a thread |
| Common | ResponseQueued | Network | | Waiting for response to be transferred |
| Tablet | AcquiringLocks | Lock | \<tablet&#8209;id>| Taking row-wise locks. May need to wait for other rpcs to release the lock. |
| Tablet | MVCC_WaitForSafeTime | Lock | \<tablet-id>| Waiting for the SafeTime to be at least the desired read-time. |
| Tablet | BackfillIndex_WaitForAFreeSlot | Lock | \<tablet-id> | Waiting for a slot to open if there are too many backfill requests at the same time. |
| Tablet | CreatingNewTablet | I/O  | \<tablet-id>| Creating a new tablet may involve writing metadata files, causing I/O wait.  |
| Tablet | WaitOnConflictingTxn | Lock | \<tablet-id>| Waiting for the conflicting transactions to complete. |
| Consensus | WAL_Append | I/O | \<tablet-id>| Persisting Wal edits |
| Consensus | WAL_Sync | I/O | \<tablet-id>| Persisting Wal edits |
| Consensus | Raft_WaitingForReplication | Network | \<tablet-id>| Waiting for Raft replication |
| Consensus | Raft_ApplyingEdits | Lock/CPU | \<tablet-id>| Applying the edits locally |
| RocksDB  | BlockCacheReadFromDisk | I/O  | \<tablet-id>| Populating block cache from disk |
| RocksDB | Flush  | I/O | \<tablet-id> | Doing RocksDB flush  |
| RocksDB | Compaction | I/O | \<tablet-id>| Doing RocksDB compaction |
| RocksDB | RateLimiter | I/O | | Slow down due to rate limiter throttling access to disk |

### YCQL

| Class | Wait Event | Type | AUX | Description |
| :--------------- |:---------- | :-------------- |:--- | :---------- |
| YCQLQuery | YCQL_Parse | CPU  | | CQL call is being actively processed |
| YCQLQuery | YCQL_Read | Network | \<table&#8209;id> | Waiting for DocDB read operation |
| YCQLQuery | YCQL_Write | Network | \<table-id> | Waiting for DocDB write operation  |
| YBClient | LookingUpTablet | Network |  | Looking up tablet information  |
| YBClient | YBCSyncLeaderMasterRpc  | Network |  | Waiting on an RPC to the master/master-service  |
| YBClient | YBCFindMasterProxy | Network  | | Waiting on establishing the proxy to master leader |

<!-- #### ycql_stat_statements

This is not ASH related. We need to mostly create a new page

This view provides YCQL statement metrics that can be joined with YCQL wait events in the [yb_active_session_history](#yb-active-session-history) table.

Note that this is not an ASH-specific view, but can be used to extract more information from the ASH data.

| Column | Type | Description |
| :----- | :--- | :---------- |
| queryid | int8 | Hash code to identify identical normalized queries. |
| query | text | Text of a representative statement. |
| is_prepared  | bool | Indicates whether the statement is a prepared statement or an unprepared query. |
| calls | int8 | Number of times the statement is executed.|
| total_time | float8 | Total time spent executing the statement, in milliseconds. |
| min_time | float8 | Minimum time spent executing the statement, in milliseconds. |
| max_time | float8 | Maximum time spent executing the statement, in milliseconds. |
| mean_time | float8 | Mean time spent executing the statement, in milliseconds. |
| stddev_time | float8 | Population standard deviation of time spent executing the statement, in milliseconds. | -->

## Examples

{{% explore-setup-single %}}

Make sure you have an active ysqlsh session (`./bin/ysqlsh`) to run the following examples.

- Distribution of wait events on each component for each query_id

    ```sql
    SELECT
        query_id,
        wait_event_component,
        wait_event,
        wait_event_type,
        ROUND(COUNT(*) * 100.0 / SUM(COUNT(*)) OVER (PARTITION BY query_id,     wait_event_component), 2) AS percentage
    FROM
        yb_active_session_history
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
    query_id             | wait_event_component |             wait_event             | wait_event_type | percentage
    ---------------------+----------------------+------------------------------------+-----------------+------------
    -8743373368438010782 | YSQL                 | QueryProcessing                    | Cpu             |     100.00
    -6991556726170825105 | YSQL                 | QueryProcessing                    | Cpu             |     100.00
    -1970690938654296136 | TServer              | Raft_ApplyingEdits                 | Cpu             |       6.91
    -1970690938654296136 | TServer              | OnCpu_Active                       | Cpu             |      13.29
    -1970690938654296136 | TServer              | OnCpu_Passive                      | Cpu             |      16.59
    -1970690938654296136 | TServer              | RocksDB_NewIterator                | DiskIO          |       0.36
    -1970690938654296136 | TServer              | ConflictResolution_ResolveConficts | Network         |       1.19
    -1970690938654296136 | TServer              | Raft_WaitingForReplication         | Network         |      60.89
    -1970690938654296136 | TServer              | MVCC_WaitForSafeTime               | WaitOnCondition |       0.19
    -1970690938654296136 | TServer              | Rpc_Done                           | WaitOnCondition |       0.59
    -1970690938654296136 | YSQL                 | QueryProcessing                    | Cpu             |     100.00
                       0 | TServer              | OnCpu_Active                       | Cpu             |      90.75
                       0 | TServer              | OnCpu_Passive                      | Cpu             |       9.25
                       1 | TServer              | WAL_Append                         | DiskIO          |      43.23
                       1 | TServer              | WAL_Sync                           | DiskIO          |      56.66
                       1 | TServer              | Idle                               | WaitOnCondition |       0.10
                       4 | TServer              | OnCpu_Active                       | Cpu             |      83.46
                       4 | TServer              | OnCpu_Passive                      | Cpu             |      15.46
                       4 | TServer              | Raft_ApplyingEdits                 | Cpu             |       0.17
                       4 | TServer              | ReplicaState_TakeUpdateLock        | WaitOnCondition |       0.91
     2721988341380097743 | YSQL                 | QueryProcessing                    | Cpu             |     100.00
     6107501747146929242 | TServer              | OnCpu_Active                       | Cpu             |      87.76
     6107501747146929242 | TServer              | OnCpu_Passive                      | Cpu             |       6.74
     6107501747146929242 | TServer              | RocksDB_NewIterator                | DiskIO          |       2.08
     6107501747146929242 | TServer              | Rpc_Done                           | WaitOnCondition |       1.97
     6107501747146929242 | TServer              | MVCC_WaitForSafeTime               | WaitOnCondition |       1.46
     6107501747146929242 | YSQL                 | QueryProcessing                    | Cpu             |      24.58
     6107501747146929242 | YSQL                 | yb_ash_metadata                    | LWLock          |       0.01
     6107501747146929242 | YSQL                 | StorageRead                        | Network         |      75.41
     6424016913938255253 | YSQL                 | QueryProcessing                    | Cpu             |     100.00
    (30 rows)

    ```

- Distribution of wait events on each component for each query

    ```sql
    SELECT
        SUBSTRING(query, 1, 50) AS query,
        wait_event_component,
        wait_event,
        wait_event_type,
        ROUND(COUNT(*) * 100.0 / SUM(COUNT(*)) OVER (PARTITION BY query, wait_event_component), 2) AS percentage
    FROM
        yb_active_session_history
    JOIN
        pg_stat_statements
    ON
        query_id = queryid
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
                        query                          | wait_event_component |             wait_event             | wait_event_type | percentage
    ---------------------------------------------------+----------------------+------------------------------------+-----------------+------------
    INSERT INTO postgresqlkeyvalue (k, v) VALUES ($1,  | TServer              | Raft_ApplyingEdits                 | Cpu             |       6.85
    INSERT INTO postgresqlkeyvalue (k, v) VALUES ($1,  | TServer              | OnCpu_Active                       | Cpu             |      13.31
    INSERT INTO postgresqlkeyvalue (k, v) VALUES ($1,  | TServer              | OnCpu_Passive                      | Cpu             |      16.50
    INSERT INTO postgresqlkeyvalue (k, v) VALUES ($1,  | TServer              | RocksDB_NewIterator                | DiskIO          |       0.34
    INSERT INTO postgresqlkeyvalue (k, v) VALUES ($1,  | TServer              | ConflictResolution_ResolveConficts | Network         |       1.22
    INSERT INTO postgresqlkeyvalue (k, v) VALUES ($1,  | TServer              | Raft_WaitingForReplication         | Network         |      60.97
    INSERT INTO postgresqlkeyvalue (k, v) VALUES ($1,  | TServer              | MVCC_WaitForSafeTime               | WaitOnCondition |       0.20
    INSERT INTO postgresqlkeyvalue (k, v) VALUES ($1,  | TServer              | Rpc_Done                           | WaitOnCondition |       0.61
    INSERT INTO postgresqlkeyvalue (k, v) VALUES ($1,  | YSQL                 | QueryProcessing                    | Cpu             |     100.00
    SELECT k, v FROM postgresqlkeyvalue WHERE k = $1   | TServer              | OnCpu_Active                       | Cpu             |      87.69
    SELECT k, v FROM postgresqlkeyvalue WHERE k = $1   | TServer              | OnCpu_Passive                      | Cpu             |       6.76
    SELECT k, v FROM postgresqlkeyvalue WHERE k = $1   | TServer              | RocksDB_NewIterator                | DiskIO          |       2.14
    SELECT k, v FROM postgresqlkeyvalue WHERE k = $1   | TServer              | MVCC_WaitForSafeTime               | WaitOnCondition |       1.45
    SELECT k, v FROM postgresqlkeyvalue WHERE k = $1   | TServer              | Rpc_Done                           | WaitOnCondition |       1.97
    SELECT k, v FROM postgresqlkeyvalue WHERE k = $1   | YSQL                 | QueryProcessing                    | Cpu             |      24.62
    SELECT k, v FROM postgresqlkeyvalue WHERE k = $1   | YSQL                 | yb_ash_metadata                    | LWLock          |       0.01
    SELECT k, v FROM postgresqlkeyvalue WHERE k = $1   | YSQL                 | StorageRead                        | Network         |      75.38
   (17 rows)

    ```

- Distribution of requests to all the tablets (finding out a hot shard)

    ```sql
    SELECT
        wait_event_aux,
        ROUND(COUNT(*) * 100.0 / SUM(COUNT(*)) OVER(), 2) AS percentage
    FROM
        yb_active_session_history
    WHERE
        wait_event_component = 'TServer' AND
        wait_event_aux IS NOT NULL
    GROUP BY
        wait_event_aux
    ORDER BY
        percentage DESC;
    ```

    ```output
    wait_event_aux  | percentage
   -----------------+------------
    1ee7c269d3a74cc |      67.68
    7f6f448e2280406 |      14.67
    90d9b0138a1c49d |       4.41
    1a0da03e370b4d3 |       4.41
    24db684f73c9479 |       4.24
    49434b5d87ff4f3 |       4.14
    f30265abd9764da |       0.14
    090e43c5da98421 |       0.07
    3d6615f3d97f4be |       0.07
    55e32f4ad65649a |       0.03
    7f2aaf8f96564bd |       0.03
    c1f70242f8cf463 |       0.03
    779602f85a6a466 |       0.03
    fbfc62e86fa2444 |       0.03
    (14 rows)
    ```

- Distribution of type of wait events per component

    ```sql
    SELECT
        wait_event_component,
        wait_event_type,
        ROUND(100.0 * COUNT(*) / SUM(COUNT(*)) OVER (PARTITION BY wait_event_component), 2) AS percentage
    FROM
        yb_active_session_history
    GROUP BY
        wait_event_component,
        wait_event_type
    ORDER BY
        wait_event_component;
    ```

    ```output
    wait_event_component | wait_event_type | percentage
   ----------------------+-----------------+------------
    TServer              | Cpu             |      33.48
    TServer              | DiskIO          |      55.95
    TServer              | WaitOnCondition |       0.53
    TServer              | Network         |      10.04
    YSQL                 | Network         |      78.27
    YSQL                 | Cpu             |      21.73
   (6 rows)
   ```
