---
title: Change data capture (CDC)
headerTitle: Change data capture (CDC)
linkTitle: Change data capture (CDC)
description: CDC or Change data capture is a process to capture changes made to data in the database.
beta: /latest/faq/general/#what-is-the-definition-of-the-beta-feature-tag
# url: /latest/cdc/
aliases: 
 - /latest/explore/change-data-capture/
 - /latest/explore/cdc
menu:
  latest:
    name: Change data capture (CDC)
    identifier: explore-change-data-capture
    parent: explore
    weight: 699
isTocNested: true
showAsideToc: true
---

## What is CDC?

Change data capture (CDC) is a process to capture changes made to data in the database and stream those changes to external processes, applications or other databases. <br/>

 The core primitive of CDC is the _stream_. Streams can be enabled/disabled on databases. Every change to a watched database table is emitted as a record in a configurable format to a configurable sink. Streams scale to any YugabyteDB cluster independent of its size and are designed to impact production traffic as little as possible.

## Use cases

Many applications benefit from capturing changes to items stored in a YugabyteDB table, at the point in time when such changes occur. The following are some sample use cases: triggering alerts and notifications in IoT use cases, sending real-time updates to analytics pipelines and applications, auditing and compliance, and cache invalidation.

### Process Architecture

#### CDC Streams

Streams are the YugabyteDB endpoints for fetching DB changes by applications, processes and systems. Streams can be enabled or disabled [on a per table basis]. Every change to a database table (for which the data is being streamed) is emitted as a record to the stream, which is then propagated further for consumption by applications, in our case to Debezium and then ultimately to Kafka.

#### DB Stream

In order to facilitate the streaming of data, we have to create a DB Stream, this stream is created on the database level and can be used to access the data out of all the tables under a particular database.

### Consistency Semantics

#### Per-Tablet Ordered Delivery Guarantee

All changes for a row (or rows in the same tablet) will be received in the order in which they happened. However, due to the distributed nature of the problem, there is no guarantee of the order across tablets.

#### At least Once Delivery

Updates for rows will be streamed at least once. This can happen in the case of Kafka Connect Node failure. If the Kafka Connect Node pushes the records to Kafka and crashes before committing the offset, on the restart, it will again get the same set of records.

#### No Gaps in Change Stream

Note that once you have received a change for a row for some timestamp t, you will not receive a previously unseen change for that row at a lower timestamp. Therefore, there is a guarantee at all times that receiving any change implies all older changes have been received for a row.

### Performance Impact

  The change records for CDC are read from the WAL. CDC module maintains checkpoint internally for each of the stream-id and garbage collects the WAL entries if those have been streamed to cdc clients. <br/>

  In case CDC is lagging or away for some time, the disk usage may grow and may cause YugabyteDB cluster instability. To avoid a scenario like this if a stream is inactive for a configured amount of time we garbage collect the WAL. This is configurable by a GFLAG.

### Prerequisites/Consideration

* You should be using YugabyteDB version 2.13.0 or higher
* You cannot stream data out of system tables
* Yugabyte cluster should be up and running, for details see YugabyteDB Quick Start
* There should be at least one primary key on the table you want to stream the changes from
* You cannot create a stream on a table which doesn’t exist. For example, if you create a DB stream on the database and after that create a new table in that database, you won’t be able to stream data out of that table. A simple workaround is to create a new DB Stream ID and use it to stream data

{{< note title="Note" >}}

As of the current implementation: if you want to use CDC, you need to use Debezium and Kafka.

{{< /note >}}

{{< warning title="Warning" >}}

Do note that we do NOT support DROP TABLE and TRUNCATE TABLE commands yet, the behavior of these commands while streaming data from CDC is not defined. If dropping or truncating a table is necessarily needed, delete the stream ID using [yb-admin](../admin/yb-admin.md/../../cdc/change-data-capture.md).<br/><br/>

See [limitations](#limitations) to see what else is not supported currently.

{{< /warning >}}

### yb-admin commands for Change data capture

The commands used to manipulate CDC DB streams can be found under the [yb-admin](../admin/yb-admin.md#change-data-capture-cdc-commands).

### DDL commands support

  Change data capture supports the schema changes (eg. adding a default value to column, adding a new column, adding constraints to column, etc) for a table as well. When a DDL command is issued, the schema is altered and a DDL record will be emitted with the new schema values, after that further records will come in format of the new schema only.

### Snapshot support

Initially, if you create a stream for a particular table which already contains some records, the stream takes a snapshot of the table, and streams all the data that resides in the table. After the snapshot of the whole table is completed, YugabyteDB starts streaming the changes that would be made to the table.

Note that the snapshot feature uses a GFlag `cdc_snapshot_batch_size`, the default value for which is 250. This is the number of records included per batch in response to an internal call to get the snapshot; if the table contains a very large amount of data, you may need to increase this value to reduce the amount of time it takes to stream the complete snapshot.

### Running the Debezium connector

See the [Debezium connector](../integrations/cdc/debezium-for-cdc) documentation for details on how to run with the Debezium connector.

### Limitations

* YCQL tables are not currently supported. Issue [11320](https://github.com/yugabyte/yugabyte-db/issues/11320).
* DROP and TRUNCATE commands are not supported. If a user tries to issue these commands on a table while a stream ID is there for the table, the server might crash, the behaviour is unstable. Issues for TRUNCATE [10010](https://github.com/yugabyte/yugabyte-db/issues/10010) and DROP [10069](https://github.com/yugabyte/yugabyte-db/issues/10069).
* If a stream ID is created, and after that a new table is created, the existing stream ID is not able to stream data from the newly created table. The user needs to create a new stream ID. Issue [10921](https://github.com/yugabyte/yugabyte-db/issues/10921)
* A single stream cannot be used to stream data for both YSQL and YCQL namespaces and keyspaces respectively - [GitHub #10131](https://github.com/yugabyte/yugabyte-db/issues/10131)

In addition, CDC support for the following features will be added in upcoming releases:

* Tablet splitting support is tracked in issue [10935](https://github.com/yugabyte/yugabyte-db/issues/10935)
* Point In Time Recovery (PITR) support is tracked in issue [10938](https://github.com/yugabyte/yugabyte-db/issues/10938)
* Savepoints support is tracked in issue [10936](https://github.com/yugabyte/yugabyte-db/issues/10936)
