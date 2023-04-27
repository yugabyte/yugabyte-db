---
title: Overview of CDC in YugabyteDB
headerTitle: Overview
linkTitle: Overview
description: Change Data Capture in YugabyteDB.
headcontent: Change Data Capture in YugabyteDB
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    parent: explore-change-data-capture
    identifier: cdc-overview
    weight: 10
type: docs
---
## What is change data capture?

In databases, change data capture (CDC) is a set of software design patterns used to determine and track the data that has changed so that action can be taken using the changed data. YugabyteDB CDC captures changes made to data in the database and stream those changes to external processes, applications, or other databases.

## Process architecture
Change Data Capture (CDC) allows you to track and propagate changes in a YugabyteDB database to downstream consumers based on its Write-Ahead Log (WAL).
YugabyteDB CDC uses Debezium to capture row-level changes resulting from INSERT, UPDATE and DELETE operations in the upstream database and publishes them as events to Kafka using Kafka Connect-compatible connectors.

![What is CDC](/images/explore/cdc-overview-what.png)

## How does CDC work?

YugabyteDB automatically splits user tables into multiple shards, called tablets, using either a hash or range based strategy. The primary key for each row in the table uniquely identifies the location of the tablet in the row. 

Each Tablet has its own Write Ahead Log(WAL) file. WAL is NOT in-memory but it’s disk persisted. Each WAL preserves the order in which transactions (or changes) happened.Hybrid TS, Operation id, and additional metadata about the transaction is also preserved.

![How does CDC work](/images/explore/cdc-overview-work2.png)

The Debezium YugabyteDB connector captures row-level changes in the schemas of a YugabyteDB database. The first time it connects to a YugabyteDB cluster, the connector takes a consistent snapshot of all schemas. After that snapshot is complete, the connector continuously captures row-level changes that insert, update, and delete database content and that were committed to a PostgreSQL database. The connector generates data change event records and streams them to Kafka topics. For each table, the default behavior is that the connector streams all generated events to a separate Kafka topic for that table. Applications and services consume data change event records from that topic.

![How does CDC work](/images/explore/cdc-overview-work.png)

The connector produces a change event for every row-level insert, update, and delete operation that was captured and sends change event records for each table in a separate Kafka topic. Client applications read the Kafka topics that correspond to the database tables of interest, and can react to every row-level event they receive from those topics.

YugabyteDB normally purges write-ahead log (WAL) segments after some period of time. This means that the connector does not have the complete history of all changes that have been made to the database. Therefore, when the connector first connects to a particular PostgreSQL database, it starts by performing a consistent snapshot of each of the database schemas. 

The core primitive of CDC is the _stream_. Streams can be enabled and disabled on databases. Every change to a watched database table is emitted as a record in a configurable format to a configurable sink. Streams scale to any YugabyteDB cluster independent of its size and are designed to impact production traffic as little as possible.

Debezium is deployed as a set of Kafka Connect-compatible connectors, so you first need to define a YugabyteDB connector configuration and then start the connector by adding it to Kafka Connect.

![How does CDC work](/images/explore/cdc-overview-work3.png)

### Stream expiry

When a client reads the changes from WAL (Write-ahead Log) /IntentDB, the intents are retained and the retention time is controlled by the gflag [cdc_intent_retention_ms](../../reference/configuration/yb-tserver/#cdc-intent-retention-ms).

When you create a stream, the checkpoint for a tablet is set as soon as the client requests changes. If the client doesn't request changes within `cdc_intent_retention_ms` milliseconds, the CDC service considers the `tablet_id, stream_id` combination to be expired, and allows those intents to be removed by the garbage collection process.

Once a stream has expired, you need to create a new stream ID in order to proceed.

{{< warning title="Warning" >}}

If you set `cdc_intent_retention_ms` to a high value, and the stream lags for any reason, the intents will be retained for a longer period. This may destabilize your cluster if the number of intents keeps growing.

{{< /warning >}}


## Consistency semantics

### Per-tablet ordered delivery guarantee

All changes for a row (or rows in the same tablet) are received in the order in which they happened. However, due to the distributed nature of the problem, there is no guarantee of the order across tablets.

### At least once delivery

Updates for rows are streamed at least once. This can happen in the case of Kafka Connect Node failure. If the Kafka Connect Node pushes the records to Kafka and crashes before committing the offset, on restart, it will again get the same set of records.

### No gaps in change stream

Note that after you have received a change for a row for some timestamp `t`, you won't receive a previously unseen change for that row at a lower timestamp. Receiving any change implies that you have received _all older changes_ for that row.

## Performance impact

The change records for CDC are read from the WAL. The CDC module maintains checkpoints internally for each of the stream-ids and garbage collects the WAL entries if those have been streamed to CDC clients.

In case CDC is lagging or away for some time, the disk usage may grow and may cause YugabyteDB cluster instability. To avoid this scenario, if a stream is inactive for a configured amount of time, the WAL is garbage-collected. This is configurable using a [Gflag](../../reference/configuration/yb-tserver/#change-data-capture-cdc-flags).

## Snapshot support

Initially, if you create a stream for a particular table that already contains some records, the stream takes a snapshot of the table, and streams all the data that resides in the table. After the snapshot of the whole table is completed, YugabyteDB starts streaming the changes that happen in the table.

The snapshot feature uses the `cdc_snapshot_batch_size` GFlag. This flag's default value is 250 records included per batch in response to an internal call to get the snapshot. If the table contains a very large amount of data, you may need to increase this value to reduce the amount of time it takes to stream the complete snapshot. You can also choose not to take a snapshot by modifying the [Debezium](../change-data-capture/debezium-connector-yugabytedb/) configuration.

If the snapshot fails, you need to restart the connector. Upon restart, if the connector detects that the snapshot has been completed for a given tablet, the connector skips that tablet in the snapshot process and resumes streaming the changes for that tablet.

{{< tip title="Taking a snapshot again" >}}

If you need to force the connector to take a snapshot again, you should clean up the Kafka topics manually and delete their contents. Then, create a new stream ID, and deploy the connector again with that newly-created stream ID.

You can't take another snapshot of the table using an existing stream ID. In other words, for a given stream ID, if the snapshot process is _completed successfully_, you can't use that stream ID to take the snapshot again.

{{< /tip >}}

## Before image

Before image refers to the state of the row before the change event occurred. This state is populated during UPDATE and DELETE events as in case of INSERT and READ (Snapshot) events, the change is for the creation of new content.

At any moment, YugabyteDB not only stores the latest state of the data, but also the recent history of changes. By default, the history retention period is controlled by the [history retention interval flag](../../reference/configuration/yb-tserver/#timestamp_history_retention_interval_sec), applied cluster-wide to every YSQL database.

However, when before image is enabled for a database, YugabyteDB adjusts the history retention for that database based on the most lagging active CDC stream. When a CDC active stream's lag increases, the amount of space required for the database grows as more data is retained.

There are no technical limitations on the retention target. The actual overhead depends on the workload, and you'll need to estimate it by running tests based on your applications.

You'll need to create a CDC DB stream indicating the server to send the before image of the changed rows with the streams. To learn more about creating streams with before image enabled, see [yb-admin](../../admin/yb-admin/#change-data-capture-cdc-commands).

{{< note title="Note" >}}

Write operations in the current transaction aren't visible in the before image. In other words, the before image is only available for _records committed prior to the current transaction_.

{{< /note >}}

## Dynamic addition of new tables

If a new table is added to a namespace on which there is an active stream ID, a background thread will add the newly created table to the stream ID so that data can now be streamed from the new table as well. The background thread runs at an interval of 1 second and adds `cdcsdk_table_processing_limit_per_run` tables per iteration to the stream.

## Packed rows

CDC supports packed rows. However, if all the non-key columns of a packed row are modified, CDC emits the changes as an INSERT record rather than an UPDATE record.

## Known limitations

* YCQL tables aren't currently supported. Issue [11320](https://github.com/yugabyte/yugabyte-db/issues/11320).
* CDC behaviour is undefined on downgrading from a CDC supported version (2.13 and newer) to an unsupported version (2.12 and older) and upgrading it back. Issue [12800](https://github.com/yugabyte/yugabyte-db/issues/12800)
* CDC is not supported on a target table for xCluster replication [11829](https://github.com/yugabyte/yugabyte-db/issues/11829).
* A single stream can only be used to stream data from one namespace only.
* There should be a primary key on the table you want to stream the changes from.

In addition, CDC support for the following features will be added in upcoming releases:

* Support for point-in-time recovery (PITR) is tracked in issue [10938](https://github.com/yugabyte/yugabyte-db/issues/10938).
* Support for transaction savepoints is tracked in issue [10936](https://github.com/yugabyte/yugabyte-db/issues/10936).
* Support for enabling CDC on Read Replicas is tracked in issue [11116](https://github.com/yugabyte/yugabyte-db/issues/11116).
* Support for schema evolution with before image is tracked in issue [15197](https://github.com/yugabyte/yugabyte-db/issues/15197).

## Further reading
* Stream CDC events to Snowflake
* Unlocking Azure Storage Options With YugabyteDB CDC
* Change Data Capture (CDC) From YugabyteDB to Elasticsearch
* Snowflake CDC: Publishing Data Using Amazon S3 and YugabyteDB
* Streaming Changes From YugabyteDB to Downstream Databases
* Change Data Capture (CDC) from YugabyteDB CDC to ClickHouse
* Data Capture (CDC): Run Debezium Server with Kafka as a Sink
* Change Data Capture (CDC) Using a Spring Data Processing Pipeline

