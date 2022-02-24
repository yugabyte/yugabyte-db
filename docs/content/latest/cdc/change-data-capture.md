---
title: Change Data Capture (CDC)
headerTitle: Change Data Capture (CDC)
linkTitle: Change Data Capture (CDC)
description: CDC or Change Data Capture is a process to capture changes made to data in the database.
---

## What is CDC?

Change Data Capture (CDC) is a process to capture changes made to data in the database and stream those changes to external processes, applications or other databases. <br/>

 The core primitive of CDC is the ‘stream’. Streams can be enabled/disabled on databases. Every change to a watched database table is emitted as a record in a configurable format to a configurable sink. Streams scale to any YugabyteDB cluster independent of its size and are designed to impact production traffic as little as possible.

## Use cases

  Many applications benefit from capturing changes to items stored in a YugabyteDB table, at the point in time when such changes occur. The following are some example use cases such as triggering alerts and notifications in IoT use cases, sending real time updates to analytics pipelines and applications, auditing and compliance, cache invalidation, etc.

### Process Architecture

### CDC Streams

  Streams are the YugabyteDB endpoints for fetching DB changes by applications, processes and systems. Streams can be enabled or disabled [on a per table basis]. Every change to a database table (for which the data is being streamed) is emitted as a record to the stream, which is then propagated further for consumption by applications, in our case to Debezium and then ultimately to Kafka.

  #### DB Stream
  In order to facilitate the streaming of data, we have to create a DB Stream, this stream is created on the database level and can be used to access the data out of all the tables under a particular database.

### Consistency Semantics
  * #### Per-Tablet Ordered Delivery Guarantee
    All changes for a row (or rows in the same tablet) will be received in the order in which they happened. However, due to the distributed nature of the problem, there is no guarantee of the order across tablets.
  * #### At least Once Delivery
    Updates for rows will be streamed at least once. This can happen in the case of Kafka Connect Node failure. If the Kafka Connect Node pushes the records to Kafka and crashes before committing the offset, on the restart, it will again get the same set of records.
  * #### No Gaps in Change Stream
    Note that once you have received a change for a row for some timestamp t, you will not receive a previously unseen change for that row at a lower timestamp. Therefore, there is a guarantee at all times that receiving any change implies all older changes have been received for a row.

### Performance Impact

  The change records for CDC are read from the WAL. CDC module maintains checkpoint internally for each of the stream-id and garbage collects the WAL entries if those have been streamed to cdc clients. <br/>

  In case CDC is lagging or away for some time, the disk usage may grow and may cause YugabyteDB cluster instability. To avoid a scenario like this if a stream is inactive for a configured amount of time we garbage collect the WAL. This is configurable by a GFLAG.

### Prerequisites/Consideration

  * You should be using YugabyteDB version <version-number> or higher
  * You cannot stream data out of system tables
  * Yugabyte cluster should be up and running, for details see YugabyteDB Quick Start
  * There should be at least one primary key on the table you want to stream the changes from
  * You cannot create a stream on a table which doesn’t exist. For example, if you create a DB stream on the database and after that create a new table in that database, you won’t be able to stream data out of that table. A simple workaround is to create a new DB Stream ID and use it to stream data
  
  {{< note title="Note" >}}

  As of current implementation, if a user wants to use CDC, he will need to use Debezium and Kafka mandatorily.

  {{< /note >}}

### yb-admin commands for Change Data Capture

The commands used to manipulate CDC DB streams can be found under the [yb-admin](../admin/yb-admin.md#change-data-capture-cdc-commands).

### DDL commands support

  Change Data Capture supports the schema changes (eg. adding a default value to column, adding a new column, adding constraints to column, etc) for a table as well. When a DDL command is issued, the schema is altered and a DDL record will be emitted with the new schema values, after that further records will come in format of the new schema only.

### Snapshot support

  Initially, if we create a stream for a particular table which already contains some records, the stream would take the snapshot of the table and stream all the data that resides in the table. Once the snapshot of the whole table is completed, it would start streaming the changes that would be made to the table. <br/>

  Note that the snapshot feature uses a GFlag cdc_snapshot_batch size, the default value for which is 250 i.e. the number of records that would be there in one batch when an internal call would be placed to get the snapshot, so if the table consists of a huge amount of data, you might need to change this value otherwise the streaming of the complete snapshot would take some time.

### GFlags affecting Change Data Capture

  | **GFlag**                                         | **Default Value** | **Description**                                                                                                                                                                                                                                                                                                                                                                                                                    |
|---------------------------------------------------|-------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| cdc_max_stream_intent_records                     | 1000              | This is the maximum number of intent records allowed in a single CDC batch.                                                                                                                                                                                                                                                                                                                                                        |
| cdc_snapshot_batch_size                           | 250               | This specifies the number of records that are fetched in a single batch of snapshot operation of CDC.                                                                                                                                                                                                                                                                                                                              |
| consensus_max_size_bytes                          | 4_MB              | The maximum per-tablet RPC batch size when updating peers.                                                                                                                                                                                                                                                                                                                                                                         |
| cdc_min_replicated_index_considered_stale_seconds | 900               | If cdc_min_replicated_index hasn't been replicated in this amount of time, we reset its value to max int64 to avoid retaining any logs.                                                                                                                                                                                                                                                                                            |
| timestamp_history_retention_interval_sec          | 900               | This specifies the time interval, in seconds, to retain history or older versions of data.                                                                                                                                                                                                                                                                                                                                         |
| update_min_cdc_indices_interval_secs              | 60                | This specifies how often to read the cdc_state table to get the minimum applied index for each tablet across all streams. This information is used to correctly keep log files that contain unapplied entries. This is also the rate at which a tablet's minimum replicated index across all streams is sent to the other peers in the configuration. If flag enable_log_retention_by_op_idx is disabled, this flag has no effect. |
| cdc_ybclient_reactor_threads                      | 50                | The number of reactor threads to be used for processing ybclient requests for CDC. Increase to improve throughput on large tablet setups.                                                                                                                                                                                                                                                                                          |
| enable_log_retention_by_op_idx                    | true              | If true, logs will be retained based on an op id passed by the cdc service.                                                                                                                                                                                                                                                                                                                                                        |
| log_max_seconds_to_retain                         | 86400             | Log files that are older will be deleted even if they contain cdc unreplicated entries. If 0, this flag will be ignored. This flag is ignored if a log segment contains entries that haven't been flushed to RocksDB.                                                                                                                                                                                                              |
| log_stop_retaining_min_disk_mb                    | 102400            | Stop retaining logs if the space available for the logs falls below this limit. This flag is ignored if a log segment contains unflushed entries.                                                                                                                                                                                                                                                                                  |

### Running the Debezium connector

  Head over to the [Debezium connector](../integrations/cdc/debezium-for-cdc.md) doc for the steps on how to run with Debezium connector.

### Limitations
  * YCQL tables are not supported currently - [GitHub #11320](https://github.com/yugabyte/yugabyte-db/issues/11320)
  * DROP and TRUNCATE commands are not supported. If a user tries to issue these commands on a table while a stream ID is there for the table, the server might crash, the behaviour is unstable - TRUNCATE [GitHub #10010](https://github.com/yugabyte/yugabyte-db/issues/10010) / DROP [GitHub #10069](https://github.com/yugabyte/yugabyte-db/issues/10069)
  * If a stream ID is created, and after that a new table is created, then the existing stream ID would not be able to stream data from the newly created table, the user would need to create a new stream ID. - [GitHub #10921](https://github.com/yugabyte/yugabyte-db/issues/10921)
  * A single stream cannot be used to stream data for both YSQL and YCQL namespaces and keyspaces respectively - [GitHub #10131](https://github.com/yugabyte/yugabyte-db/issues/10131) <br/><br/>

  Apart from the above mentioned ones, Change Data Capture would not work alongside the following features, the support will be added in the upcoming releases:
  * Tablet splitting - [GitHub #10935](https://github.com/yugabyte/yugabyte-db/issues/10935)
  * Point In Time Recovery (PITR) - [GitHub #10938](https://github.com/yugabyte/yugabyte-db/issues/10938)
  * Savepoints - [GitHub #10936](https://github.com/yugabyte/yugabyte-db/issues/10936)
