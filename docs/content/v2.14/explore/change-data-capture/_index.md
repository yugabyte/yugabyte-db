---
title: Change data capture (CDC)
headerTitle: Change data capture (CDC)
linkTitle: Change data capture (CDC)
description: CDC or Change data capture is a process to capture changes made to data in the database.
headcontent: Capture changes made to data in the database
image: /images/section_icons/index/develop.png
cascade:
  techPreview: /preview/releases/versioning/#feature-availability
menu:
  v2.14:
    identifier: explore-change-data-capture
    parent: explore
    weight: 299
type: indexpage
showRightNav: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../change-data-capture/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

Change data capture (CDC) is a process to capture changes made to data in the database and stream those changes to external processes, applications, or other databases.

## Prerequisites

* The database and its tables must be created using YugabyteDB version 2.13 or later.
* CDC supports YSQL tables only. (See [Limitations](#limitations).)

Be aware that you can't stream data out of system tables.

{{< note title="Note" >}}

The current YugabyteDB CDC implementation supports only Debezium and Kafka.

{{< /note >}}

## Process architecture

The core primitive of CDC is the _stream_. Streams can be enabled and disabled on databases. Every change to a watched database table is emitted as a record in a configurable format to a configurable sink. Streams scale to any YugabyteDB cluster independent of its size and are designed to impact production traffic as little as possible.

```goat
                        .-------------------------------------------.
                        |  Node 1                                   |
                        |  '----------------' '------------------'  |
                        |  |    YB-Master   | |    YB-TServer    |  |  CDC Service is stateless
   CDC Streams metadata |  |  (Stores CDC   | |  '-------------' |  |           |
  replicated with Raft  |  |   metadata)    | |  | CDC Service | |  |           |
           .----------> |  |                | |  .-------------. |  | <---------'
           |            |  .----------------. .------------------.  |
           |            '-------------------------------------------'
           |
           |
           |_______________________________________________
           |                                               |
           v                                               v
.-------------------------------------------.    .-------------------------------------------.
|  Node 2                                   |    |   Node 3                                  |
|  '----------------' '------------------'  |    |  '----------------' '------------------'  |
|  |    YB-Master   | |    YB-TServer    |  |    |  |    YB-Master   | |    YB-TServer    |  |
|  |  (Stores CDC   | |  '-------------' |  |    |  |  (Stores CDC   | |  '-------------' |  |
|  |   metadata)    | |  | CDC Service | |  |    |  |   metadata)    | |  | CDC Service | |  |
|  |                | |  .-------------. |  |    |  |                | |  .-------------. |  |
|  .----------------. .------------------.  |    |  .----------------. .------------------.  |
'-------------------------------------------'    '-------------------------------------------'
```

### CDC streams

Streams are the YugabyteDB endpoints for fetching database changes by applications, processes, and systems. Streams can be enabled or disabled (on a per namespace basis). Every change to a database table (for which the data is being streamed) is emitted as a record to the stream, which is then propagated further for consumption by applications, in this case to Debezium, and then ultimately to Kafka.

### DB stream

To facilitate the streaming of data, you have to create a DB Stream. This stream is created at the database level, and can access the data in all of that database's tables.

### Stream expiry

When a client reads the changes from WAL (Write-ahead Log) /IntentDB, the intents are retained and the retention time is controlled by the gflag [cdc_intent_retention_ms](../../reference/configuration/yb-tserver/#cdc-intent-retention-ms).

When you create a stream, the checkpoint for a tablet is set as soon as the client requests changes. If the client doesn't request changes within `cdc_intent_retention_ms` milliseconds, the CDC service considers the `tablet_id, stream_id` combination to be expired, and allows those intents to be removed by the garbage collection process.

Once a stream has expired, you need to create a new stream ID in order to proceed.

{{< warning title="Warning" >}}

If you set `cdc_intent_retention_ms` to a high value, and the stream lags for any reason, the intents will be retained for a longer period. This may destabilize your cluster if the number of intents keeps growing.

{{< /warning >}}

## Debezium connector for YugabyteDB

The Debezium connector for YugabyteDB pulls data from YugabyteDB and publishes it to Kafka. The following illustration explains the pipeline:

![CDC Pipeline with Debezium and Kafka](/images/architecture/cdc-2dc/cdc-pipeline.png)

See [Debezium connector for YugabyteDB](./debezium-connector-yugabytedb/) to learn more, and [Running Debezium with YugabyteDB](/preview/integrations/cdc/debezium/) to get started with the Debezium connector for YugabyteDB.

## TServer configuration

There are several GFlags you can use to fine-tune YugabyteDB's CDC behavior. These flags are documented in the [Change data capture flags](../../reference/configuration/yb-tserver/#change-data-capture-cdc-flags) section of the yb-tserver reference page.

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

## yb-admin commands

The commands used to manipulate CDC DB streams are described in the [yb-admin](../../admin/yb-admin/#change-data-capture-cdc-commands) reference documentation.

## Snapshot support

Initially, if you create a stream for a particular table that already contains some records, the stream takes a snapshot of the table, and streams all the data that resides in the table. After the snapshot of the whole table is completed, YugabyteDB starts streaming the changes that happen in the table.

The snapshot feature uses the `cdc_snapshot_batch_size` GFlag. This flag's default value is 250 records included per batch in response to an internal call to get the snapshot. If the table contains a very large amount of data, you may need to increase this value to reduce the amount of time it takes to stream the complete snapshot. You can also choose not to take a snapshot by modifying the [Debezium](../change-data-capture/debezium-connector-yugabytedb/) configuration.

If the snapshot fails, you need to restart the connector. Upon restart, if the connector detects that the snapshot has been completed for a given tablet, the connector skips that tablet in the snapshot process and resumes streaming the changes for that tablet.

{{< tip title="Taking a snapshot again" >}}

If you need to force the connector to take a snapshot again, you should clean up the Kafka topics manually and delete their contents. Then, create a new stream ID, and deploy the connector again with that newly-created stream ID.

You can't take another snapshot of the table using an existing stream ID. In other words, for a given stream ID, if the snapshot process is _completed successfully_, you can't use that stream ID to take the snapshot again.

{{< /tip >}}

## Dynamic addition of new tables

If a new table is added to a namespace on which there is an active stream ID, a background thread will add the newly created table to the stream ID so that data can now be streamed from the new table as well. The background thread runs at an interval of 1 second and adds `cdcsdk_table_processing_limit_per_run` tables per iteration to the stream.

## Limitations

* YCQL tables aren't currently supported. Issue [11320](https://github.com/yugabyte/yugabyte-db/issues/11320).
* CDC behaviour is undefined on downgrading from a CDC supported version (2.13 and newer) to an unsupported version (2.12 and older) and upgrading it back. Issue [12800](https://github.com/yugabyte/yugabyte-db/issues/12800)
* CDC is not supported on a target table for xCluster replication [11829](https://github.com/yugabyte/yugabyte-db/issues/11829).
* A single stream can only be used to stream data from one namespace only.
* There should be a primary key on the table you want to stream the changes from.

In addition, CDC support for the following features will be added in upcoming releases:

* Support for point-in-time recovery (PITR) is tracked in issue [10938](https://github.com/yugabyte/yugabyte-db/issues/10938).
* Support for transaction savepoints is tracked in issue [10936](https://github.com/yugabyte/yugabyte-db/issues/10936).
* Support for enabling CDC on Read Replicas is tracked in issue [11116](https://github.com/yugabyte/yugabyte-db/issues/11116).
