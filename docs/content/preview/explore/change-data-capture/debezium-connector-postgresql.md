---
title: Debezium connector for YugabyteDB (Logical Replication)
headerTitle: Debezium connector for YugabyteDB (Logical Replication)
linkTitle: Debezium connector for YBPG
description: Debezium is an open source distributed platform used to capture the changes in a database.
aliases:
  - /preview/explore/change-data-capture/debezium-connector-yugabytedb-ysql-pg
#   - /preview/explore/change-data-capture/debezium-connector
#   - /preview/explore/change-data-capture/debezium
  - /preview/explore/change-data-capture/debezium-connector-postgres
  - /preview/explore/change-data-capture/debezium-YugabyteDB
  - /preview/explore/change-data-capture/replication
menu:
  preview:
    parent: explore-change-data-capture
    identifier: debezium-connector-yugabytedb-pg
    weight: 20
type: docs
rightNav:
  hideH4: true
---

The Debezium YugabyteDB connector captures row-level changes in the schemas of a YugabyteDB database.

The first time it connects to a YugabyteDB server or cluster, the connector takes a consistent snapshot of all schemas. After that snapshot is complete, the connector continuously captures row-level changes that insert, update, and delete database content and that were committed to a YugabyteDB database. The connector generates data change event records and streams them to Kafka topics. For each table, the default behavior is that the connector streams all generated events to a separate Kafka topic for that table. Applications and services consume data change event records from that topic.

## Overview

YugabyteDB’s [logical decoding](#reminder) feature was introduced in version 9.4. It is a mechanism that allows the extraction of the changes that were committed to the transaction log and the processing of these changes in a user-friendly manner with the help of an [output plug-in](#reminder). The output plug-in enables clients to consume the changes.

The YugabyteDB connector contains two main parts that work together to read and process database changes:

<!-- todo vaibhav: need sub-bullets -->
* A logical decoding output plug-in. You might need to install the output plug-in that you choose to use. You must configure a replication slot that uses your chosen output plug-in before running the YugabyteDB server. The plug-in can be one of the following:

    <!-- YB specific -->
    * `yboutput` is the plugin packaged with YugabyteDB. 

    * `pgoutput` is the standard logical decoding output plug-in in YugabyteDB 10+. It is maintained by the YugabyteDB community, and used by YugabyteDB itself for logical replication. This plug-in is always present so no additional libraries need to be installed. The Debezium connector interprets the raw replication event stream directly into change events.

<!-- YB note driver part -->
* Java code (the actual Kafka Connect connector) that reads the changes produced by the chosen logical decoding output plug-in. It uses YugabyteDB’s [streaming replication protocol](#reminder), by means of the YugabyteDB JDBC driver

The connector produces a change event for every row-level insert, update, and delete operation that was captured and sends change event records for each table in a separate Kafka topic. Client applications read the Kafka topics that correspond to the database tables of interest, and can react to every row-level event they receive from those topics.

YugabyteDB normally purges write-ahead log (WAL) segments after some period of time. This means that the connector does not have the complete history of all changes that have been made to the database. Therefore, when the YugabyteDB connector first connects to a particular YugabyteDB database, it starts by performing a consistent snapshot of each of the database schemas. After the connector completes the snapshot, it continues streaming changes from the exact point at which the snapshot was made. This way, the connector starts with a consistent view of all of the data, and does not omit any changes that were made while the snapshot was being taken.

The connector is tolerant of failures. As the connector reads changes and produces events, it records the WAL position for each event. If the connector stops for any reason (including communication failures, network problems, or crashes), upon restart the connector continues reading the WAL where it last left off. This includes snapshots. If the connector stops during a snapshot, the connector begins a new snapshot when it restarts.

{{< tip title="Behaviour with Logical Decoding" >}}

The connector relies on and reflects the YugabyteDB logical decoding feature, which has the following limitations:

* Logical decoding does not support DDL changes. This means that the connector is unable to report DDL change events back to consumers.

* Logical decoding replication slots are supported on only primary servers. When there is a cluster of YugabyteDB servers, the connector can run on only the active primary server. It cannot run on hot or warm standby replicas. If the primary server fails or is demoted, the connector stops. After the primary server has recovered, you can restart the connector. If a different YugabyteDB server has been promoted to primary, adjust the connector configuration before restarting the connector.

Additionally, the pgoutput logical decoding output plug-in does not capture values for generated columns, resulting in missing data for these columns in the connector’s output.

[Behavior when things go wrong](#reminder) describes how the connector responds if there is a problem.

{{< /tip >}}

{{< tip title="Use UTF-8 encoding" >}}

Debezium supports databases with UTF-8 character encoding only. With a single-byte character encoding, it's not possible to correctly process strings that contain extended ASCII code characters.

{{< /tip >}}

## Reminder

Reminder that the clicked link is an empty link and should be replaced with an appropriate one.

## How the connector works

To optimally configure and run a Debezium YugabyteDB connector, it is helpful to understand how the connector performs snapshots, streams change events, determines Kafka topic names, and uses metadata.

### Security

To use the Debezium connector to stream changes from a YugabyteDB database, the connector must operate with specific privileges in the database. Although one way to grant the necessary privileges is to provide the user with `superuser` privileges, doing so potentially exposes your YugabyteDB data to unauthorized access. Rather than granting excessive privileges to the Debezium user, it is best to create a dedicated Debezium replication user to which you grant specific privileges.

For more information about configuring privileges for the Debezium YugabyteDB user, see [Setting up permissions](#reminder). For more information about YugabyteDB logical replication security, see the [YugabyteDB documentation](#reminder).

### Snapshots

Most YugabyteDB servers are configured to not retain the complete history of the database in the WAL segments. This means that the YugabyteDB connector would be unable to see the entire history of the database by reading only the WAL. Consequently, the first time that the connector starts, it performs an initial consistent snapshot of the database.

#### Default workflow behavior of initial snapshots

The default behavior for performing a snapshot consists of the following steps. You can change this behavior by setting the `snapshot.mode` [connector configuration property](#reminder) to a value other than `initial`.

1. Start a transaction with a [SERIALIZABLE, READ ONLY, DEFERRABLE](#reminder) isolation level to ensure that subsequent reads in this transaction are against a single consistent version of the data. Any changes to the data due to subsequent `INSERT`, `UPDATE`, and `DELETE` operations by other clients are not visible to this transaction.
2. Read the current position in the server’s transaction log.
3. Scan the database tables and schemas, generate a `READ` event for each row and write that event to the appropriate table-specific Kafka topic.
4. Commit the transaction.
5. Record the successful completion of the snapshot in the connector offsets.

If the connector fails, is rebalanced, or stops after Step 1 begins but before Step 5 completes, upon restart the connector begins a new snapshot. After the connector completes its initial snapshot, the YugabyteDB connector continues streaming from the position that it read in Step 2. This ensures that the connector does not miss any updates. If the connector stops again for any reason, upon restart, the connector continues streaming changes from where it previously left off.

*Options for the **snapshot.mode** connector configuration property:*

| Option | Description |
| :--- | :--- |
| `always` | The connector always performs a snapshot when it starts. After the snapshot completes, the connector continues streaming changes from step 3 in the above sequence. This mode is useful in these situations:<br/><br/> <ul><li>It is known that some WAL segments have been deleted and are no longer available.</li> <li>After a cluster failure, a new primary has been promoted. The always snapshot mode ensures that the connector does not miss any changes that were made after the new primary had been promoted but before the connector was restarted on the new primary.</li></ul> |
| `never` | The connector never performs snapshots. When a connector is configured this way, its behavior when it starts is as follows. If there is a previously stored LSN in the Kafka offsets topic, the connector continues streaming changes from that position. If no LSN has been stored, the connector starts streaming changes from the point in time when the YugabyteDB logical replication slot was created on the server. The `never` snapshot mode is useful only when you know all data of interest is still reflected in the WAL. |
| `initial` (default) | The connector performs a database snapshot when no Kafka offsets topic exists. After the database snapshot completes the Kafka offsets topic is written. If there is a previously stored LSN in the Kafka offsets topic, the connector continues streaming changes from that position. |
| `initial_only` | The connector performs a database snapshot and stops before streaming any change event records. If the connector had started but did not complete a snapshot before stopping, the connector restarts the snapshot process and stops when the snapshot completes. |
| `custom` | The `custom` snapshot mode lets you inject your own implementation of the `io.debezium.connector.YugabyteDB.spi.Snapshotter` interface. Set the `snapshot.custom.class` configuration property to the class on the classpath of your Kafka Connect cluster or included in the JAR if using the `EmbeddedEngine`. For more details, see [custom snapshotter SPI](#reminder). |

<!-- YB note Skip the section for incremental snapshots -->

### Incremental snapshots

To provide flexibility in managing snapshots, Debezium includes a supplementary snapshot mechanism, known as *incremental snapshotting*. Incremental snapshots rely on the Debezium mechanism for [sending signals to a Debezium connector](#reminder). Incremental snapshots are based on the [DDD-3](#reminder) design document.

In an incremental snapshot, instead of capturing the full state of a database all at once, as in an initial snapshot, Debezium captures each table in phases, in a series of configurable chunks. You can specify the tables that you want the snapshot to capture and the [size of each chunk](#reminder). The chunk size determines the number of rows that the snapshot collects during each fetch operation on the database. The default chunk size for incremental snapshots is 1024 rows.

As an incremental snapshot proceeds, Debezium uses watermarks to track its progress, maintaining a record of each table row that it captures. This phased approach to capturing data provides the following advantages over the standard initial snapshot process:
* You can run incremental snapshots in parallel with streamed data capture, instead of postponing streaming until the snapshot completes. The connector continues to capture near real-time events from the change log throughout the snapshot process, and neither operation blocks the other.
* If the progress of an incremental snapshot is interrupted, you can resume it without losing any data. After the process resumes, the snapshot begins at the point where it stopped, rather than recapturing the table from the beginning.
* You can run an incremental snapshot on demand at any time, and repeat the process as needed to adapt to database updates. For example, you might re-run a snapshot after you modify the connector configuration to add a table to its `table.include.list` property.

#### Incremental snapshot process

When you run an incremental snapshot, Debezium sorts each table by primary key and then splits the table into chunks based on the [configured chunk size](#reminder). Working chunk by chunk, it then captures each table row in a chunk. For each row that it captures, the snapshot emits a `READ` event. That event represents the value of the row when the snapshot for the chunk began.

As a snapshot proceeds, it’s likely that other processes continue to access the database, potentially modifying table records. To reflect such changes,`INSERT`, `UPDATE`, or `DELETE` operations are committed to the transaction log as per usual. Similarly, the ongoing Debezium streaming process continues to detect these change events and emits corresponding change event records to Kafka.

#### How Debezium resolves collisions among records with the same primary key

In some cases, the `UPDATE` or `DELETE` events that the streaming process emits are received out of sequence. That is, the streaming process might emit an event that modifies a table row before the snapshot captures the chunk that contains the `READ` event for that row. When the snapshot eventually emits the corresponding `READ` event for the row, its value is already superseded. To ensure that incremental snapshot events that arrive out of sequence are processed in the correct logical order, Debezium employs a buffering scheme for resolving collisions. Only after collisions between the snapshot events and the streamed events are resolved does Debezium emit an event record to Kafka.

#### Snapshot window

To assist in resolving collisions between late-arriving `READ` events and streamed events that modify the same table row, Debezium employs a so-called *snapshot window*. The snapshot windows demarcates the interval during which an incremental snapshot captures data for a specified table chunk. Before the snapshot window for a chunk opens, Debezium follows its usual behavior and emits events from the transaction log directly downstream to the target Kafka topic. But from the moment that the snapshot for a particular chunk opens, until it closes, Debezium performs a de-duplication step to resolve collisions between events that have the same primary key..

For each data collection, the Debezium emits two types of events, and stores the records for them both in a single destination Kafka topic. The snapshot records that it captures directly from a table are emitted as `READ` operations. Meanwhile, as users continue to update records in the data collection, and the transaction log is updated to reflect each commit, Debezium emits `UPDATE` or `DELETE` operations for each change.

As the snapshot window opens, and Debezium begins processing a snapshot chunk, it delivers snapshot records to a memory buffer. During the snapshot windows, the primary keys of the `READ` events in the buffer are compared to the primary keys of the incoming streamed events. If no match is found, the streamed event record is sent directly to Kafka. If Debezium detects a match, it discards the buffered `READ` event, and writes the streamed record to the destination topic, because the streamed event logically supersede the static snapshot event. After the snapshot window for the chunk closes, the buffer contains only `READ` events for which no related transaction log events exist. Debezium emits these remaining `READ` events to the table’s Kafka topic.

The connector repeats the process for each snapshot chunk.

{{< warning title="Warning" >}}

The Debezium connector for YugabyteDB does not support schema changes while an incremental snapshot is running. If a schema change is performed *before* the incremental snapshot start but *after* sending the signal then passthrough config option `database.autosave` is set to `conservative` to correctly process the schema change.

{{< /warning >}}

#### Triggering an incremental snapshot

Currently, the only way to initiate an incremental snapshot is to send an [ad hoc snapshot signal](#reminder) to the signaling table on the source database.

You submit a signal to the signaling table as SQL `INSERT` queries.

After Debezium detects the change in the signaling table, it reads the signal, and runs the requested snapshot operation.

The query that you submit specifies the tables to include in the snapshot, and, optionally, specifies the kind of snapshot operation. Currently, the only valid option for snapshots operations is the default value, `incremental`.

To specify the tables to include in the snapshot, provide a `data-collections` array that lists the tables or an array of regular expressions used to match tables, for example,

`{"data-collections": ["public.MyFirstTable", "public.MySecondTable"]}`

The `data-collections` array for an incremental snapshot signal has no default value. If the `data-collections` array is empty, Debezium detects that no action is required and does not perform a snapshot.

{{< note title="Note" >}}

If the name of a table that you want to include in a snapshot contains a dot (`.`) in the name of the database, schema, or table, to add the table to the `data-collections` array, you must escape each part of the name in double quotes.<br/><br/>

For example, to include a table that exists in the **public** schema and that has the name **My.Table**, use the following format: **"public"."My.Table"**.

{{< /note >}}

Prerequisites:

<!-- todo vaibhav require sub-bullets -->
* [Signaling is enabled](#reminder).
    * A signaling data collection exists on the source database.
    * The signaling data collection is specified in the `signal.data.collection` property.

<!-- todo vaibhav add rest of incremental snapshot and custom snapshotter SPI content -->

### Streaming changes

The YugabyteDB connector typically spends the vast majority of its time streaming changes from the YugabyteDB server to which it is connected. This mechanism relies on [YugabyteDB’s replication protocol](#reminder). This protocol enables clients to receive changes from the server as they are committed in the server’s transaction log at certain positions, which are referred to as Log Sequence Numbers (LSNs).

Whenever the server commits a transaction, a separate server process invokes a callback function from the [logical decoding plug-in](#reminder). This function processes the changes from the transaction, converts them to a specific format (Protobuf or JSON in the case of Debezium plug-in) and writes them on an output stream, which can then be consumed by clients.

The Debezium YugabyteDB connector acts as a YugabyteDB client. When the connector receives changes it transforms the events into Debezium *create*, *update*, or *delete* events that include the LSN of the event. The YugabyteDB connector forwards these change events in records to the Kafka Connect framework, which is running in the same process. The Kafka Connect process asynchronously writes the change event records in the same order in which they were generated to the appropriate Kafka topic.

Periodically, Kafka Connect records the most recent *offset* in another Kafka topic. The offset indicates source-specific position information that Debezium includes with each event. For the YugabyteDB connector, the LSN recorded in each change event is the offset.

When Kafka Connect gracefully shuts down, it stops the connectors, flushes all event records to Kafka, and records the last offset received from each connector. When Kafka Connect restarts, it reads the last recorded offset for each connector, and starts each connector at its last recorded offset. When the connector restarts, it sends a request to the YugabyteDB server to send the events starting just after that position.

{{< note title="Note" >}}

The YugabyteDB connector retrieves schema information as part of the events sent by the logical decoding plug-in. However, the connector does not retrieve information about which columns compose the primary key. The connector obtains this information from the JDBC metadata (side channel). If the primary key definition of a table changes (by adding, removing or renaming primary key columns), there is a tiny period of time when the primary key information from JDBC is not synchronized with the change event that the logical decoding plug-in generates. During this tiny period, a message could be created with an inconsistent key structure. To prevent this inconsistency, update primary key structures as follows:
1. Put the database or an application into a read-only mode.
2. Let Debezium process all remaining events.
3. Stop Debezium.
4. Update the primary key definition in the relevant table.
5. Put the database or the application into read/write mode.
6. Restart Debezium.

{{< /note >}}

### YugabyteDB 10+ logical decoding support (pgoutput)

As of YugabyteDB 10+, there is a logical replication stream mode, called pgoutput that is natively supported by YugabyteDB. This means that a Debezium YugabyteDB connector can consume that replication stream without the need for additional plug-ins. This is particularly valuable for environments where installation of plug-ins is not supported or not allowed.

For more information, see [Setting up YugabyteDB](#reminder).

### Topic names

By default, the YugabyteDB connector writes change events for all `INSERT`, `UPDATE`, and `DELETE` operations that occur in a table to a single Apache Kafka topic that is specific to that table. The connector names change event topics as _topicPrefix.schemaName.tableName_.

The components of a topic name are as follows:

* _topicPrefix_ - the topic prefix as specified by the `topic.prefix` configuration property.
* _schemaName_ - the name of the database schema in which the change event occurred.
* _tableName_ - the name of the database table in which the change event occurred.

For example, suppose that `dbserver` is the logical server name in the configuration for a connector that is capturing changes in a YugabyteDB installation that has a `yugabyte` database and an `inventory` schema that contains four tables: `products`, `products_on_hand`, `customers`, and `orders`. The connector would stream records to these four Kafka topics:

* `dbserver.inventory.products`
* `dbserver.inventory.products_on_hand`
* `dbserver.inventory.customers`
* `dbserver.inventory.orders`

Now suppose that the tables are not part of a specific schema but were created in the default public YugabyteDB schema. The names of the Kafka topics would be:

* `dbserver.public.products`
* `dbserver.public.products_on_hand`
* `dbserver.public.customers`
* `dbserver.public.orders`

The connector applies similar naming conventions to label its [transaction metadata topics](todo vaubhav).

If the default topic names don't meet your requirements, you can configure custom topic names. To configure custom topic names, you specify regular expressions in the logical topic routing SMT. For more information about using the logical topic routing SMT to customize topic naming, see the Debezium documentation on [Topic routing](#reminder).

### Transaction metadata

Debezium can generate events that represent transaction boundaries and that enrich data change event messages.

{{< note title="Limits on when Debezium receives transaction metadata" >}}

Debezium registers and receives metadata only for transactions that occur _after you deploy the connector_. Metadata for transactions that occur before you deploy the connector is not available.

{{< /note >}}

For every transaction `BEGIN` and `END`, Debezium generates an event containing the following fields:

* `status` - `BEGIN` or `END`
* `id` - String representation of the unique transaction identifier composed of Postgres transaction ID itself and LSN of given operation separated by colon, i.e. the format is `txID:LSN`
* `ts_ms` - The time of a transaction boundary event (`BEGIN` or `END` event) at the data source. If the data source does not provide Debezium with the event time, then the field instead represents the time at which Debezium processes the event.
* `event_count` (for `END` events) - total number of events emitted by the transaction
* `data_collections` (for `END` events) - an array of pairs of `data_collection` and `event_count` that provides the number of events emitted by changes originating from given data collection

For example:

```output.json
{
  "status": "BEGIN",
  "id": "571:53195829",
  "ts_ms": 1486500577125,
  "event_count": null,
  "data_collections": null
}

{
  "status": "END",
  "id": "571:53195832",
  "ts_ms": 1486500577691,
  "event_count": 2,
  "data_collections": [
    {
      "data_collection": "s1.a",
      "event_count": 1
    },
    {
      "data_collection": "s2.a",
      "event_count": 1
    }
  ]
}
```

Unless overridden via the `transaction.topic` option, transaction events are written to the topic and named _<topic.prefix>_.transaction.

#### Change data event enrichment

When transaction metadata is enabled the data message `Envelope` is enriched with a new `transaction` field. This field provides information about every event in the form of a composite of fields:

* `id` - string representation of unique transaction identifier
* `total_order` - absolute position of the event among all events generated by the transaction
* `data_collection_order` - the per-data collection position of the event among all events emitted by the transaction

Following is an example of a message:

```output.json
{
  "before": null,
  "after": {
    "pk": "2",
    "aa": "1"
  },
  "source": {
   ...
  },
  "op": "c",
  "ts_ms": "1580390884335",
  "transaction": {
    "id": "571:53195832",
    "total_order": "1",
    "data_collection_order": "1"
  }
}
```

## Data change events

The Debezium YugabyteDB connector generates a data change event for each row-level `INSERT`, `UPDATE`, and `DELETE` operation. Each event contains a key and a value. The structure of the key and the value depends on the table that was changed.

Debezium and Kafka Connect are designed around *continuous streams of event messages*. However, the structure of these events may change over time, which can be difficult for consumers to handle. To address this, each event contains the schema for its content or, if you are using a schema registry, a schema ID that a consumer can use to obtain the schema from the registry. This makes each event self-contained.

The following skeleton JSON shows the basic four parts of a change event. However, how you configure the Kafka Connect converter that you choose to use in your application determines the representation of these four parts in change events. A `schema` field is in a change event only when you configure the converter to produce it. Likewise, the event key and event payload are in a change event only if you configure a converter to produce it. If you use the JSON converter and you configure it to produce all four basic change event parts, change events have this structure:

```output.json
{
 "schema": { --> 1
   ...
  },
 "payload": { --> 2
   ...
 },
 "schema": { --> 3
   ...
  },
 "payload": { --> 4
   ...
 }
}
```

*Overview of change event basic content:*

| Item | Field name | Description |
| :--: | :--------- | :---------- |
| 1 | `schema` | The first `schema` field is part of the event key. It specifies a Kafka Connect schema that describes what is in the event key's `payload` portion. In other words, the first `schema` field describes the structure of the primary key, or the unique key if the table does not have a primary key, for the table that was changed. |
| 2 | `payload` | The first `payload` field is part of the event key. It has the structure described by the previous `schema` field and it contains the key for the row that was changed. |
| 3 | `schema` | The second `schema` field is part of the event value. It specifies the Kafka Connect schema that describes what is in the event value's `payload` portion. In other words, the second `schema` describes the structure of the row that was changed. Typically, this schema contains nested schemas. |
| 4 | `payload` | The second `payload` field is part of the event value. It has the structure described by the previous `schema` field and it contains the actual data for the row that was changed. |

By default behavior is that the connector streams change event records to [topics with names that are the same as the event’s originating table](#reminder).

{{< note title="Note" >}}

Starting with Kafka 0.10, Kafka can optionally record the event key and value with the [timestamp](#reminder) at which the message was created (recorded by the producer) or written to the log by Kafka.

{{< /note >}}

{{< warning title="Warning" >}}

The YugabyteDB connector ensures that all Kafka Connect schema names adhere to the Avro schema name format. This means that the logical server name must start with a Latin letter or an underscore, that is, a-z, A-Z, or _. Each remaining character in the logical server name and each character in the schema and table names must be a Latin letter, a digit, or an underscore, that is, a-z, A-Z, 0-9, or \_. If there is an invalid character it is replaced with an underscore character.

This can lead to unexpected conflicts if the logical server name, a schema name, or a table name contains invalid characters, and the only characters that distinguish names from one another are invalid and thus replaced with underscores.

{{< /warning >}}

### Change event keys

For a given table, the change event’s key has a structure that contains a field for each column in the primary key of the table at the time the event was created. Alternatively, if the table has `REPLICA IDENTITY` set to `FULL` there is a field for each unique key constraint.

Consider a `customers` table defined in the `public` database schema and the example of a change event key for that table.

**Example table:**

```sql
CREATE TABLE customers (
  id SERIAL,
  first_name VARCHAR(255) NOT NULL,
  last_name VARCHAR(255) NOT NULL,
  email VARCHAR(255) NOT NULL,
  PRIMARY KEY(id)
);
```

#### Example change event key

If the `topic.prefix` connector configuration property has the value `YugabyteDB_server`, every change event for the `customers` table while it has this definition has the same key structure, which in JSON looks like this:

```output.json
{
  "schema": { --> 1
    "type": "struct",
    "name": "YugabyteDB_server.public.customers.Key", --> 2
    "optional": false, --> 3
    "fields": [ --> 4
          {
              "name": "id",
              "index": "0",
              "schema": {
                  "type": "INT32",
                  "optional": "false"
              }
          }
      ]
  },
  "payload": { --> 5
      "id": "1"
  },
}
```

**Description of a change event key:**

| Item | Field name | Description |
| :--- | :--------- | :---------- |
| 1 | schema | The schema portion of the key specifies a Kafka Connect schema that describes what is in the key's `payload` portion. |
| 2 | YugabyteDB_server.public.customers.Key | Name of the schema that defines the structure of the key's payload. This schema describes the structure of the primary key for the table that was changed. Key schema names have the format _connector-name.database-name.table-name.Key_. In this example: <br/> `YugabyteDB_server` is the name of the connector that generated this event. <br/> `public` is the schema which contains the table that was changed. <br/> `customers` is the table that was updated. |
| 3 | optional | Indicates whether the event key must contain a value in its `payload` field. In this example, a value in the key's payload is required. |
| 4 | fields | Specifies each field that is expected in the payload, including each field's name, index, and schema. |
| 5 | payload | Contains the key for the row for which this change event was generated. In this example, the key, contains a single `id` field whose value is `1`. |

{{< note title="Note" >}}

Although the `column.exclude.list` and `column.include.list` connector configuration properties allow you to capture only a subset of table columns, all columns in a primary or unique key are always included in the event’s key.

{{< /note >}}

{{< warning title="Warning" >}}

<!-- YB Note -->
CDC is not supported for tables without primary keys.

{{< /warning >}}

### Change event values

The value in a change event is a bit more complicated than the key. Like the key, the value has a `schema` section and a `payload` section. The `schema` section contains the schema that describes the `Envelope` structure of the `payload` section, including its nested fields. Change events for operations that create, update or delete data all have a value payload with an envelope structure.

Consider the same sample table that was used to show an example of a change event key:

```sql
CREATE TABLE customers (
  id SERIAL,
  first_name VARCHAR(255) NOT NULL,
  last_name VARCHAR(255) NOT NULL,
  email VARCHAR(255) NOT NULL,
  PRIMARY KEY(id)
);
```

The value portion of a change event for a change to this table varies according to the `REPLICA IDENTITY` setting and the operation that the event is for.

### Replica Identity

[REPLICA IDENTITY](#reminder) is a YugabyteDB-specific table-level setting that determines the amount of information that is available to the logical decoding plug-in for `UPDATE` and `DELETE` events. More specifically, the setting of `REPLICA IDENTITY` controls what (if any) information is available for the previous values of the table columns involved, whenever an `UPDATE` or `DELETE` event occurs.

<!-- YB Note changes ahead -->
There are 4 possible values for `REPLICA IDENTITY`:
* `CHANGE` - Emitted events for `UPDATE` operations will only contain the value of the changed column along with the primary key column with no previous values present. `DELETE` operations will only contain the previous value of the primary key column in the table.
* `DEFAULT` - The default behavior is that only `DELETE` events contain the previous values for the primary key columns of a table. For an `UPDATE` event, no previous values will be present.
* `NOTHING` - Emitted events for `UPDATE` and `DELETE` operations do not contain any information about the previous value of any table column.
* `FULL` - Emitted events for `UPDATE` and `DELETE` operations contain the previous values of all columns in the table.

### *create* events

The following example shows the value portion of a change event that the connector generates for an operation that creates data in the `customers` table:

```output.json
{
    "schema": { --> 1
        "type": "struct",
        "fields": [
            {
                "type": "struct",
                "fields": [
                    {
                        "type": "int32",
                        "optional": false,
                        "field": "id"
                    },
                    {
                        "type": "string",
                        "optional": false,
                        "field": "first_name"
                    },
                    {
                        "type": "string",
                        "optional": false,
                        "field": "last_name"
                    },
                    {
                        "type": "string",
                        "optional": false,
                        "field": "email"
                    }
                ],
                "optional": true,
                "name": "YugabyteDB_server.inventory.customers.Value", --> 2
                "field": "before"
            },
            {
                "type": "struct",
                "fields": [
                    {
                        "type": "int32",
                        "optional": false,
                        "field": "id"
                    },
                    {
                        "type": "string",
                        "optional": false,
                        "field": "first_name"
                    },
                    {
                        "type": "string",
                        "optional": false,
                        "field": "last_name"
                    },
                    {
                        "type": "string",
                        "optional": false,
                        "field": "email"
                    }
                ],
                "optional": true,
                "name": "YugabyteDB_server.inventory.customers.Value",
                "field": "after"
            },
            {
                "type": "struct",
                "fields": [
                    {
                        "type": "string",
                        "optional": false,
                        "field": "version"
                    },
                    {
                        "type": "string",
                        "optional": false,
                        "field": "connector"
                    },
                    {
                        "type": "string",
                        "optional": false,
                        "field": "name"
                    },
                    {
                        "type": "int64",
                        "optional": false,
                        "field": "ts_ms"
                    },
                    {
                        "type": "boolean",
                        "optional": true,
                        "default": false,
                        "field": "snapshot"
                    },
                    {
                        "type": "string",
                        "optional": false,
                        "field": "db"
                    },
                    {
                        "type": "string",
                        "optional": false,
                        "field": "schema"
                    },
                    {
                        "type": "string",
                        "optional": false,
                        "field": "table"
                    },
                    {
                        "type": "int64",
                        "optional": true,
                        "field": "txId"
                    },
                    {
                        "type": "int64",
                        "optional": true,
                        "field": "lsn"
                    },
                    {
                        "type": "int64",
                        "optional": true,
                        "field": "xmin"
                    }
                ],
                "optional": false,
                "name": "io.debezium.connector.YugabyteDB.Source", --> 3
                "field": "source"
            },
            {
                "type": "string",
                "optional": false,
                "field": "op"
            },
            {
                "type": "int64",
                "optional": true,
                "field": "ts_ms"
            }
        ],
        "optional": false,
        "name": "YugabyteDB_server.public.customers.Envelope" --> 4
    },
    "payload": { --> 5
        "before": null, --> 6 
        "after": { --> 7
            "id": 1,
            "first_name": "Anne",
            "last_name": "Kretchmar",
            "email": "annek@noanswer.org"
        },
        "source": { --> 8
            "version": "2.5.2.Final",
            "connector": "YugabyteDB",
            "name": "YugabyteDB_server",
            "ts_ms": 1559033904863,
            "snapshot": true,
            "db": "postgres",
            "sequence": "[\"24023119\",\"24023128\"]",
            "schema": "public",
            "table": "customers",
            "txId": 555,
            "lsn": 24023128,
            "xmin": null
        },
        "op": "c", --> 9
        "ts_ms": 1559033904863 --> 10
    }
}
```

*Descriptions of create event value fields:*

| Item | Field name | Description |
| :---- | :------ | :------------ |
| 1 | schema | The value’s schema, which describes the structure of the value’s payload. A change event’s value schema is the same in every change event that the connector generates for a particular table. |
| 2 | name | In the schema section, each name field specifies the schema for a field in the value’s payload.<br/><br/>`YugabyteDB_server.inventory.customers.Value` is the schema for the payload’s *before* and *after* fields. This schema is specific to the customers table.<br/><br/>Names of schemas for *before* and *after* fields are of the form *logicalName.tableName.Value*, which ensures that the schema name is unique in the database. This means that when using the [Avro converter](#reminder), the resulting Avro schema for each table in each logical source has its own evolution and history. |
| 3 | name | `io.debezium.connector.YugabyteDB.Source` is the schema for the payload’s `source` field. This schema is specific to the YugabyteDB connector. The connector uses it for all events that it generates. |
| 4 | name | `YugabyteDB_server.inventory.customers.Envelope` is the schema for the overall structure of the payload, where `YugabyteDB_server` is the connector name, `public` is the schema, and `customers` is the table. |
| 5 | payload | The value’s actual data. This is the information that the change event is providing.<br/><br/>It may appear that the JSON representations of the events are much larger than the rows they describe. This is because the JSON representation must include the schema and the payload portions of the message. However, by using the [Avro converter](#reminder), you can significantly decrease the size of the messages that the connector streams to Kafka topics. |
| 6 | before | An optional field that specifies the state of the row before the event occurred. When the op field is `c` for create, as it is in this example, the `before` field is `null` since this change event is for new content.<br/>{{< note title="Note" >}}Whether or not this field is available is dependent on the [REPLICA IDENTITY](#replica-identity) setting for each table.{{< /note >}} |
| 7 | after | An optional field that specifies the state of the row after the event occurred. In this example, the `after` field contains the values of the new row’s `id`, `first_name`, `last_name`, and `email` columns. |
| 8 | source | Mandatory field that describes the source metadata for the event. This field contains information that you can use to compare this event with other events, with regard to the origin of the events, the order in which the events occurred, and whether events were part of the same transaction. The source metadata includes:<br/><ul><li>Debezium version</li><li>Connector type and name</li><li>Database and table that contains the new row</li><li>Stringified JSON array of additional offset information. The first value is always the last committed LSN, the second value is always the current LSN. Either value may be null.</li><li>Schema name</li><li>If the event was part of a snapshot</li><li>ID of the transaction in which the operation was performed</li><li>Offset of the operation in the database log</li><li>Timestamp for when the change was made in the database</li></ul> |
| 9 | op | Mandatory string that describes the type of operation that caused the connector to generate the event. In this example, `c` indicates that the operation created a row. Valid values are: <ul><li> `c` = create <li> `r` = read (applies to only snapshots) <li> `u` = update <li> `d` = delete <li> `m` = message</ul> |
| 10 | ts_ms | Optional field that displays the time at which the connector processed the event. The time is based on the system clock in the JVM running the Kafka Connect task.<br/><br/>In the `source` object, `ts_ms` indicates the time that the change was made in the database. By comparing the value for `payload.source.ts_ms` with the value for `payload.ts_ms`, you can determine the lag between the source database update and Debezium. |

### *update* events

The value of a change event for an update in the sample `customers` table has the same schema as a create event for that table. Likewise, the event value’s payload has the same structure. However, the event value payload contains different values in an update event. Here is an example of a change event value in an event that the connector generates for an update in the `customers` table:

<!-- YB Note removed before image for DEFAULT replica identity -->

```sql
{
    "schema": { ... },
    "payload": {
        "before": null, --> 1
        "after": { --> 2
            "id": 1,
            "first_name": "Anne Marie",
            "last_name": "Kretchmar",
            "email": "annek@noanswer.org"
        },
        "source": { --> 3
            "version": "2.5.2.Final",
            "connector": "YugabyteDB",
            "name": "YugabyteDB_server",
            "ts_ms": 1559033904863,
            "snapshot": false,
            "db": "postgres",
            "schema": "public",
            "table": "customers",
            "txId": 556,
            "lsn": 24023128,
            "xmin": null
        },
        "op": "u", --> 4
        "ts_ms": 1465584025523 --> 5
    }
}
```

*Descriptions of update event value fields:*

| Item | Field name | Description |
| :---- | :------ | :------------ |
| 1 | before | An optional field that contains values that were in the row before the database commit. In this example, no previous value for any of the columns, is present because the table’s [REPLICA IDENTITY](#replica-identity) setting is, by default, `DEFAULT`. + For an update event to contain the previous values of all columns in the row, you would have to change the `customers` table by running `ALTER TABLE customers REPLICA IDENTITY FULL`. |
| 2 | after | An optional field that specifies the state of the row after the event occurred. In this example, the `first_name` value is now `Anne Marie`. |
| 3 | source | Mandatory field that describes the source metadata for the event. The `source` field structure has the same fields as in a create event, but some values are different. The source metadata includes:<br/<br/> <ul><li>Debezium version</li><li>Connector type and name</li><li>Database and table that contains the new row</li><li>Schema name</li><li>If the event was part of a snapshot (always `false` for *update* events)</li><li>ID of the transaction in which the operation was performed</li><li>Offset of the operation in the database log</li><li>Timestamp for when the change was made in the database</li></ul> |
| 4 | op | Mandatory string that describes the type of operation. In an update event value, the `op` field value is `u`, signifying that this row changed because of an update. |
| 5 | ts_ms | Optional field that displays the time at which the connector processed the event. The time is based on the system clock in the JVM running the Kafka Connect task.<br/><br/>In the `source` object, `ts_ms` indicates the time that the change was made in the database. By comparing the value for `payload.source.ts_ms` with the value for `payload.ts_ms`, you can determine the lag between the source database update and Debezium. |

{{< note title="Note" >}}

Updating the columns for a row’s primary/unique key changes the value of the row’s key. When a key changes, Debezium outputs three events: a `DELETE` event and a [tombstone event]() with the old key for the row, followed by an event with the new key for the row. Details are in the next section.

{{< /note >}}

### Primary key updates

An `UPDATE` operation that changes a row’s primary key field(s) is known as a primary key change. For a primary key change, in place of sending an `UPDATE` event record, the connector sends a `DELETE` event record for the old key and a `CREATE` event record for the new (updated) key.

### *delete* events

The value in a *delete* change event has the same `schema` portion as create and update events for the same table. The `payload` portion in a delete event for the sample `customers` table looks like this:

```output.json
{
    "schema": { ... },
    "payload": {
        "before": { --> 1
            "id": 1
        },
        "after": null, --> 2
        "source": { --> 3
            "version": "2.5.4.Final",
            "connector": "YugabyteDB",
            "name": "YugabyteDB_server",
            "ts_ms": 1559033904863,
            "snapshot": false,
            "db": "postgres",
            "schema": "public",
            "table": "customers",
            "txId": 556,
            "lsn": 46523128,
            "xmin": null
        },
        "op": "d", --> 4
        "ts_ms": 1465581902461 --> 5
    }
}
```

*Descriptions of delete event value fields:*

| Item | Field name | Description |
| :---- | :------ | :------------ |
| 1 | before | Optional field that specifies the state of the row before the event occurred. In a *delete* event value, the `before` field contains the values that were in the row before it was deleted with the database commit.<br/><br/>In this example, the before field contains only the primary key column because the table’s [REPLICA IDENTITY](#replica-identity) setting is `DEFAULT`. |
| 2 | after | Optional field that specifies the state of the row after the event occurred. In a delete event value, the `after` field is `null`, signifying that the row no longer exists. |
| 3 | source | Mandatory field that describes the source metadata for the event. In a delete event value, the source field structure is the same as for create and update events for the same table. Many source field values are also the same. In a delete event value, the ts_ms and lsn field values, as well as other values, might have changed. But the source field in a delete event value provides the same metadata:<br/><ul><li>Debezium version</li><li>Connector type and name</li><li>Database and table that contained the deleted row</li><li>Schema name</li><li>If the event was part of a snapshot (always false for delete events)</li><li>ID of the transaction in which the operation was performed</li><li>Offset of the operation in the database log</li><li>Timestamp for when the change was made in the database</li></ul> |
| 4 | op | Mandatory string that describes the type of operation. The `op` field value is `d`, signifying that this row was deleted. |
| 5 | ts_ms | Optional field that displays the time at which the connector processed the event. The time is based on the system clock in the JVM running the Kafka Connect task.<br/><br/>In the `source` object, `ts_ms` indicates the time that the change was made in the database. By comparing the value for `payload.source.ts_ms` with the value for `payload.ts_ms`, you can determine the lag between the source database update and Debezium. |

A *delete* change event record provides a consumer with the information it needs to process the removal of this row.

YugabyteDB connector events are designed to work with [Kafka log compaction](https://kafka.apache.org/documentation#compaction). Log compaction enables removal of some older messages as long as at least the most recent message for every key is kept. This lets Kafka reclaim storage space while ensuring that the topic contains a complete data set and can be used for reloading key-based state.

#### Tombstone events

When a row is deleted, the *delete* event value still works with log compaction, because Kafka can remove all earlier messages that have that same key. However, for Kafka to remove all messages that have that same key, the message value must be `null`. To make this possible, the YugabyteDB connector follows a *delete* event with a special tombstone event that has the same key but a `null` value.

<!-- YB Note skipping content for truncate and message events -->

## Data type mappings

<!-- YB Note specific changes for YugabyteDB -->
## Setting up YugabyteDB

## Deployment

To deploy a Debezium YugabyteDB connector, you install the Debezium YugabyteDB connector archive, configure the connector, and start the connector by adding its configuration to Kafka Connect.

Prerequisites
* [Zookeeper](https://zookeeper.apache.org/), [Kafka](http://kafka.apache.org/), and [Kafka Connect](https://kafka.apache.org/documentation.html#connect) are installed.
* YugabyteDB is installed and is [set up to run the Debezium connector](#reminder).

Procedure
1. Download the [Debezium YugabyteDB connector plug-in archive](#reminder).
2. Extract the files into your Kafka Connect environment.
3. Add the directory with the JAR files to [Kafka Connect’s `plugin.path`](https://kafka.apache.org/documentation/#connectconfigs).
4. Restart your Kafka Connect process to pick up the new JAR files.

If you are working with immutable containers, see [Debezium’s Container images](#reminder) for Zookeeper, Kafka, YugabyteDB and Kafka Connect with the YugabyteDB connector already installed and ready to run. You can also [run Debezium on Kubernetes and OpenShift](#reminder).

### Connector configuration example

Following is an example of the configuration for a YugabyteDB connector that connects to a YugabyteDB server on port `5433` at `192.168.99.100`, whose logical name is `fulfillment`. Typically, you configure the Debezium YugabyteDB connector in a JSON file by setting the configuration properties available for the connector.

You can choose to produce events for a subset of the schemas and tables in a database. Optionally, you can ignore, mask, or truncate columns that contain sensitive data, are larger than a specified size, or that you do not need.

```output.json
{
  "name": "fulfillment-connector",  --> 1
  "config": {
    "connector.class": "io.debezium.connector.YugabyteDB.PostgresConnector", --> 2
    "database.hostname": "192.168.99.100", --> 3
    "database.port": "5432", --> 4
    "database.user": "postgres", --> 5
    "database.password": "postgres", --> 6
    "database.dbname" : "postgres", --> 7
    "topic.prefix": "fulfillment", --> 8
    "table.include.list": "public.inventory" --> 9
  }
}
```

1. The name of the connector when registered with a Kafka Connect service.
2. The name of this YugabyteDB connector class.
3. The address of the YugabyteDB server.
4. The port number of the YugabyteDB server.
5. The name of the YugabyteDB user that has the [required privileges](#reminder).
6. The password for the YugabyteDB user that has the [required privileges](#reminder).
7. The name of the YugabyteDB database to connect to
8. The topic prefix for the YugabyteDB server/cluster, which forms a namespace and is used in all the names of the Kafka topics to which the connector writes, the Kafka Connect schema names, and the namespaces of the corresponding Avro schema when the Avro converter is used.
9. A list of all tables hosted by this server that this connector will monitor. This is optional, and there are other properties for listing the schemas and tables to include or exclude from monitoring.

See the [complete list of YugabyteDB connector properties](#reminder) that can be specified in these configurations.

You can send this configuration with a `POST` command to a running Kafka Connect service. The service records the configuration and starts one connector task that performs the following actions:
* Connects to the YugabyteDB database.
* Reads the transaction log.
* Streams change event records to Kafka topics.

### Adding connector configuration

To run a Debezium YugabyteDB connector, create a connector configuration and add the configuration to your Kafka Connect cluster.

Prerequisites
* [YugabyteDB is configured to support logical replication.](#setting-up-yugabytedb)
* The [logical decoding plug-in](#reminder) is installed.
* The YugabyteDB connector is installed.

Procedure
1. Create a configuration for the YugabyteDB connector.
2. Use the [Kafka Connect REST API](https://kafka.apache.org/documentation/#connect_rest) to add that connector configuration to your Kafka Connect cluster.

#### Results

After the connector starts, it [performs a consistent snapshot](#reminder) of the YugabyteDB server databases that the connector is configured for. The connector then starts generating data change events for row-level operations and streaming change event records to Kafka topics.

### Connector properties

The Debezium YugabyteDB connector has many configuration properties that you can use to achieve the right connector behavior for your application. Many properties have default values. Information about the properties is organized as follows:
* [Required configuration properties](#required-configuration-properties)
* [Advanced configuration properties](#advanced-configuration-properties)
* [Pass-through configuration properties](#pass-through-configuration-properties)

The following configuration properties are *required* unless a default value is available.

#### Required configuration properties

| Property | Default value | Description |
| :------- | :------------ | :---------- |
| name | No default | Unique name for the connector. Attempting to register again with the same name will fail. This property is required by all Kafka Connect connectors. |
| connector.class | No default | The name of the Java class for the connector. Always use a value of `io.debezium.connector.YugabyteDB.YBPostgresConnector` for the YugabyteDB connector. |
| tasks.max | 1 | The maximum number of tasks that should be created for this connector. The YugabyteDB connector always uses a single task and therefore does not use this value, so the default is always acceptable. |
| plugin.name | decoderbufs | The name of the YugabyteDB [logical decoding plug-in](#reminder) installed on the YugabyteDB server.<br/>Supported values are `yboutput`, and `pgoutput`. |
| slot.name | debezium | The name of the YugabyteDB logical decoding slot that was created for streaming changes from a particular plug-in for a particular database/schema. The server uses this slot to stream events to the Debezium connector that you are configuring.<br/>Slot names must conform to [YugabyteDB replication slot naming rules](#reminder), which state: *"Each replication slot has a name, which can contain lower-case letters, numbers, and the underscore character."* |
| slot.drop.on.stop | false | Whether or not to delete the logical replication slot when the connector stops in a graceful, expected way. The default behavior is that the replication slot remains configured for the connector when the connector stops. When the connector restarts, having the same replication slot enables the connector to start processing where it left off.<br/>Set to true in only testing or development environments. Dropping the slot allows the database to discard WAL segments. When the connector restarts it performs a new snapshot or it can continue from a persistent offset in the Kafka Connect offsets topic. |
| publication.name | dbz_publication | The name of the YugabyteDB publication created for streaming changes when using pgoutput.<br/>This publication is created at start-up if it does not already exist and it includes all tables. Debezium then applies its own include/exclude list filtering, if configured, to limit the publication to change events for the specific tables of interest. The connector user must have superuser permissions to create this publication, so it is usually preferable to create the publication before starting the connector for the first time.<br/>If the publication already exists, either for all tables or configured with a subset of tables, Debezium uses the publication as it is defined. |
| database.hostname | No default | IP address or hostname of the YugabyteDB database server. |
| database.port | 5433 | Integer port number of the YugabyteDB database server. |
| database.user | No default | Name of the YugabyteDB database user for connecting to the YugabyteDB database server. |
| database.password | No default | Password to use when connecting to the YugabyteDB database server. |
| database.dbname | No default | The name of the YugabyteDB database from which to stream the changes. |
| topic.prefix | No default | Topic prefix that provides a namespace for the particular YugabyteDB database server or cluster in which Debezium is capturing changes. The prefix should be unique across all other connectors, since it is used as a topic name prefix for all Kafka topics that receive records from this connector. Only alphanumeric characters, hyphens, dots and underscores must be used in the database server logical name. {{< warning title="Warning" >}} Do not change the value of this property. If you change the name value, after a restart, instead of continuing to emit events to the original topics, the connector emits subsequent events to topics whose names are based on the new value. {{< /warning >}} |
| schema.include.list | No default | An optional, comma-separated list of regular expressions that match names of schemas for which you **want** to capture changes. Any schema name not included in `schema.include.list` is excluded from having its changes captured. By default, all non-system schemas have their changes captured.<br/>To match the name of a schema, Debezium applies the regular expression that you specify as an *anchored* regular expression. That is, the specified expression is matched against the entire identifier for the schema; it does not match substrings that might be present in a schema name.<br/>If you include this property in the configuration, do not also set the `schema.exclude.list` property. |
| schema.exclude.list | No default | An optional, comma-separated list of regular expressions that match names of schemas for which you **do not** want to capture changes. Any schema whose name is not included in `schema.exclude.list` has its changes captured, with the exception of system schemas.<br/>To match the name of a schema, Debezium applies the regular expression that you specify as an *anchored* regular expression. That is, the specified expression is matched against the entire identifier for the schema; it does not match substrings that might be present in a schema name.<br/>If you include this property in the configuration, do not set the `schema.include.list` property. |
| table.include.list | No default | An optional, comma-separated list of regular expressions that match fully-qualified table identifiers for tables whose changes you want to capture. When this property is set, the connector captures changes only from the specified tables. Each identifier is of the form *schemaName.tableName*. By default, the connector captures changes in every non-system table in each schema whose changes are being captured.<br/>To match the name of a table, Debezium applies the regular expression that you specify as an anchored regular expression. That is, the specified expression is matched against the entire identifier for the table; it does not match substrings that might be present in a table name.<br/>If you include this property in the configuration, do not also set the `table.exclude.list` property. |
| table.exclude.list | No default | An optional, comma-separated list of regular expressions that match fully-qualified table identifiers for tables whose changes you do not want to capture. Each identifier is of the form *schemaName.tableName*. When this property is set, the connector captures changes from every table that you do not specify.<br/>To match the name of a table, Debezium applies the regular expression that you specify as an *anchored* regular expression. That is, the specified expression is matched against the entire identifier for the table; it does not match substrings that might be present in a table name.<br/>If you include this property in the configuration, do not set the `table.include.list` property. |
| column.include.list | No default | An optional, comma-separated list of regular expressions that match the fully-qualified names of columns that should be included in change event record values. Fully-qualified names for columns are of the form *schemaName.tableName.columnName*.<br/>To match the name of a column, Debezium applies the regular expression that you specify as an *anchored* regular expression. That is, the expression is used to match the entire name string of the column; it does not match substrings that might be present in a column name.<br/>If you include this property in the configuration, do not also set the `column.exclude.list` property. |
| column.exclude.list | No default | An optional, comma-separated list of regular expressions that match the fully-qualified names of columns that should be excluded from change event record values. Fully-qualified names for columns are of the form *schemaName.tableName.columnName*.<br/>To match the name of a column, Debezium applies the regular expression that you specify as an *anchored* regular expression. That is, the expression is used to match the entire name string of the column; it does not match substrings that might be present in a column name.<br/>If you include this property in the configuration, do not set the `column.include.list` property.
| skip.messages.without.change | false | Specifies whether to skip publishing messages when there is no change in included columns. This would essentially filter messages if there is no change in columns included as per `column.include.list` or `column.exclude.list` properties.<br/>Note: Only works when REPLICA IDENTITY of the table is set to FULL |
| time.precision.mode | adaptive | Time, date, and timestamps can be represented with different kinds of precision:<br/><br/>`adaptive` captures the time and timestamp values exactly as in the database using either millisecond, microsecond, or nanosecond precision values based on the database column’s type.<br/><br/>`adaptive_time_microseconds` captures the date, datetime and timestamp values exactly as in the database using either millisecond, microsecond, or nanosecond precision values based on the database column’s type. An exception is `TIME` type fields, which are always captured as microseconds.<br/><br/>`connect` always represents time and timestamp values by using Kafka Connect’s built-in representations for `Time`, `Date`, and `Timestamp`, which use millisecond precision regardless of the database columns' precision. For more information, see [temporal values](#reminder). |
| decimal.handling.mode | precise | Specifies how the connector should handle values for `DECIMAL` and `NUMERIC` columns:<br/><br/>`precise` represents values by using `java.math.BigDecimal` to represent values in binary form in change events.<br/><br/>`double` represents values by using double values, which might result in a loss of precision but which is easier to use.<br/><br/>`string` encodes values as formatted strings, which are easy to consume but semantic information about the real type is lost. For more information, see Decimal types. |
| interval.handling.mode | numeric | Specifies how the connector should handle values for interval columns:<br/><br/>`numeric` represents intervals using approximate number of microseconds.<br/><br/>`string` represents intervals exactly by using the string pattern representation `P<years>Y<months>M<days>DT<hours>H<minutes>M<seconds>S`. For example: `P1Y2M3DT4H5M6.78S`. For more information, see [YugabyteDB basic types](#reminder). |
| database.sslmode | prefer | Whether to use an encrypted connection to the YugabyteDB server. Options include:<br/><br/>`disable` uses an unencrypted connection.<br/><br/>`allow` attempts to use an unencrypted connection first and, failing that, a secure (encrypted) connection.<br/><br/>`prefer` attempts to use a secure (encrypted) connection first and, failing that, an unencrypted connection.<br/><br/>`require` uses a secure (encrypted) connection, and fails if one cannot be established.<br/><br/>`verify-ca` behaves like require but also verifies the server TLS certificate against the configured Certificate Authority (CA) certificates, or fails if no valid matching CA certificates are found.<br/><br/>`verify-full` behaves like verify-ca but also verifies that the server certificate matches the host to which the connector is trying to connect. For more information, see the [YugabyteDB documentation](https://www.YugabyteDB.org/docs/current/static/libpq-connect.html). |
| database.sslcert | No default | The path to the file that contains the SSL certificate for the client. For more information, see the [YugabyteDB documentation](https://www.YugabyteDB.org/docs/current/static/libpq-connect.html). |
| database.sslkey | No default | The path to the file that contains the SSL private key of the client. For more information, see the [YugabyteDB documentation](https://www.YugabyteDB.org/docs/current/static/libpq-connect.html). |
| database.sslpassword | No default | The password to access the client private key from the file specified by `database.sslkey`. For more information, see the [YugabyteDB documentation](https://www.YugabyteDB.org/docs/current/static/libpq-connect.html). |
| database.sslrootcert | No default | The path to the file that contains the root certificate(s) against which the server is validated. For more information, see the [YugabyteDB documentation](https://www.YugabyteDB.org/docs/current/static/libpq-connect.html). |
| database.tcpKeepAlive | true | Enable TCP keep-alive probe to verify that the database connection is still alive. For more information, see the [YugabyteDB documentation](https://www.YugabyteDB.org/docs/current/static/libpq-connect.html). |
| tombstones.on.delete | true | Controls whether a delete event is followed by a tombstone event.<br/><br/>`true` - a delete operation is represented by a delete event and a subsequent tombstone event.<br/><br/>`false` - only a delete event is emitted.<br/><br/>After a source record is deleted, emitting a tombstone event (the default behavior) allows Kafka to completely delete all events that pertain to the key of the deleted row in case log compaction is enabled for the topic. |
| column.truncate.to.length.chars | n/a | An optional, comma-separated list of regular expressions that match the fully-qualified names of character-based columns. Set this property if you want to truncate the data in a set of columns when it exceeds the number of characters specified by the length in the property name. Set `length` to a positive integer value, for example, `column.truncate.to.20.chars`.<br/><br/>The fully-qualified name of a column observes the following format: *<schemaName>.<tableName>.<columnName>*. To match the name of a column, Debezium applies the regular expression that you specify as an anchored regular expression. That is, the specified expression is matched against the entire name string of the column; the expression does not match substrings that might be present in a column name.<br/><br/>You can specify multiple properties with different lengths in a single configuration. |
| column.mask.with.length.chars | n/a | An optional, comma-separated list of regular expressions that match the fully-qualified names of character-based columns. Set this property if you want the connector to mask the values for a set of columns, for example, if they contain sensitive data. Set `length` to a positive integer to replace data in the specified columns with the number of asterisk (`*`) characters specified by the length in the property name. Set length to `0` (zero) to replace data in the specified columns with an empty string.<br/><br/>The fully-qualified name of a column observes the following format: schemaName.tableName.columnName. To match the name of a column, Debezium applies the regular expression that you specify as an anchored regular expression. That is, the specified expression is matched against the entire name string of the column; the expression does not match substrings that might be present in a column name.<br/><br/>You can specify multiple properties with different lengths in a single configuration. |
| column.mask.hash.hashAlgorithm.with.salt.*salt*;<br/>column.mask.hash.v2.hashAlgorithm.with.salt.*salt* | n/a | An optional, comma-separated list of regular expressions that match the fully-qualified names of character-based columns. Fully-qualified names for columns are of the form *<schemaName>.<tableName>.<columnName>*.<br/>To match the name of a column Debezium applies the regular expression that you specify as an anchored regular expression. That is, the specified expression is matched against the entire name string of the column; the expression does not match substrings that might be present in a column name. In the resulting change event record, the values for the specified columns are replaced with pseudonyms.<br/>A pseudonym consists of the hashed value that results from applying the specified hashAlgorithm and salt. Based on the hash function that is used, referential integrity is maintained, while column values are replaced with pseudonyms. Supported hash functions are described in the [MessageDigest](https://docs.oracle.com/javase/7/docs/technotes/guides/security/StandardNames.html#MessageDigest) section of the Java Cryptography Architecture Standard Algorithm Name Documentation.<br/><br/>In the following example, `CzQMA0cB5K` is a randomly selected salt.<br/><br/>```column.mask.hash.SHA-256.with.salt.CzQMA0cB5K = inventory.orders.customerName, inventory.shipment.customerName```<br/>If necessary, the pseudonym is automatically shortened to the length of the column. The connector configuration can include multiple properties that specify different hash algorithms and salts.<br/><br/>Depending on the `hashAlgorithm` used, the salt selected, and the actual data set, the resulting data set might not be completely masked.<br/><br/>Hashing strategy version 2 should be used to ensure fidelity if the value is being hashed in different places or systems. |
| column.propagate.source.type | n/a | An optional, comma-separated list of regular expressions that match the fully-qualified names of columns for which you want the connector to emit extra parameters that represent column metadata. When this property is set, the connector adds the following fields to the schema of event records:<ul><li><span style="font-family: monospace;">__debezium.source.column.type</span></li><li><span style="font-family: monospace;">__debezium.source.column.length</span></li><li><span style="font-family: monospace;">__debezium.source.column.scale</span></li></ul>These parameters propagate a column’s original type name and length (for variable-width types), respectively.<br/>Enabling the connector to emit this extra data can assist in properly sizing specific numeric or character-based columns in sink databases.<br/>The fully-qualified name of a column observes one of the following formats: *databaseName.tableName.columnName*, or *databaseName.schemaName.tableName.columnName*.<br/>To match the name of a column, Debezium applies the regular expression that you specify as an *anchored* regular expression. That is, the specified expression is matched against the entire name string of the column; the expression does not match substrings that might be present in a column name. |
| datatype.propagate.source.type | n/a | An optional, comma-separated list of regular expressions that specify the fully-qualified names of data types that are defined for columns in a database. When this property is set, for columns with matching data types, the connector emits event records that include the following extra fields in their schema:<ul><li><span style="font-family: monospace;">__debezium.source.column.type</span></li><li><span style="font-family: monospace;">__debezium.source.column.length</span></li><li><span style="font-family: monospace;">__debezium.source.column.scale</span></li></ul>These parameters propagate a column’s original type name and length (for variable-width types), respectively.<br/>Enabling the connector to emit this extra data can assist in properly sizing specific numeric or character-based columns in sink databases.<br/>The fully-qualified name of a column observes one of the following formats: *databaseName.tableName.typeName*, or *databaseName.schemaName.tableName.typeName*.<br/>To match the name of a data type, Debezium applies the regular expression that you specify as an *anchored* regular expression. That is, the specified expression is matched against the entire name string of the data type; the expression does not match substrings that might be present in a type name.<br/>For the list of YugabyteDB-specific data type names, see the [YugabyteDB data type mappings](#reminder). |

#### Advanced configuration properties
#### Pass-through configuration properties

## Monitoring

The Debezium YugabyteDB connector provides two types of metrics that are in addition to the built-in support for JMX metrics that Zookeeper, Kafka, and Kafka Connect provide.

* [Snapshot metrics](#snapshot-metrics) provide information about connector operation while performing a snapshot.
* [Streaming metrics](#streaming-metrics) provide information about connector operation when the connector is capturing changes and streaming change event records.

[Debezium monitoring documentation](https://debezium.io/documentation/reference/2.5/operations/monitoring.html#monitoring-debezium) provides details for how to expose these metrics by using JMX.

### Snapshot metrics

The **MBean** is `debezium.postgres:type=connector-metrics,context=snapshot,server=<topic.prefix>`.

Snapshot metrics are not exposed unless a snapshot operation is active, or if a snapshot has occurred since the last connector start.

The following table lists the shapshot metrics that are available.

| Attributes | Type | Description |
| :--------- | :--- | :---------- |
| `LastEvent` | string | The last snapshot event that the connector has read. |
| `MilliSecondsSinceLastEvent` | long | The number of milliseconds since the connector has read and processed the most recent event. |
| `TotalNumberOfEventsSeen` | long | The total number of events that this connector has seen since last started or reset. |
| `NumberOfEventsFiltered` | long | The number of events that have been filtered by include/exclude list filtering rules configured on the connector. |
| `CapturedTables` | string[] | The list of tables that are captured by the connector. |
| `QueueTotalCapacity` | int | The length the queue used to pass events between the snapshotter and the main Kafka Connect loop. |
| `QueueRemainingCapacity` | int | The free capacity of the queue used to pass events between the snapshotter and the main Kafka Connect loop. |
| `TotalTableCount` | int | The total number of tables that are being included in the snapshot. |
| `RemainingTableCount` | int | The number of tables that the snapshot has yet to copy. |
| `SnapshotRunning` | boolean | Whether the snapshot was started. |
| `SnapshotPaused` | boolean | Whether the snapshot was paused. |
| `SnapshotAborted` | boolean | Whether the snapshot was aborted. |
| `SnapshotCompleted` | boolean | Whether the snapshot completed. |
| `SnapshotDurationInSeconds` | long | The total number of seconds that the snapshot has taken so far, even if not complete. Includes also time when snapshot was paused. |
| `SnapshotPausedDurationInSeconds` | long | The total number of seconds that the snapshot was paused. If the snapshot was paused several times, the paused time adds up. |
| `RowsScanned` | Map<String, Long> | Map containing the number of rows scanned for each table in the snapshot. Tables are incrementally added to the Map during processing. Updates every 10,000 rows scanned and upon completing a table. |
| `MaxQueueSizeInBytes` | long | The maximum buffer of the queue in bytes. This metric is available if `max.queue.size.in.bytes` is set to a positive long value. |
| `CurrentQueueSizeInBytes` | long | The current volume, in bytes, of records in the queue. |

The connector also provides the following additional snapshot metrics when an incremental snapshot is executed:

| Attributes | Type | Description |
| :--------- | :--- | :---------- |
| `ChunkId` | string | The identifier of the current snapshot chunk. |
| `ChunkFrom` | string | The lower bound of the primary key set defining the current chunk. |
| `ChunkTo` | string | The upper bound of the primary key set defining the current chunk. |
| `TableFrom` | string | The lower bound of the primary key set of the currently snapshotted table. |
| `TableTo` | string | The upper bound of the primary key set of the currently snapshotted table. |

### Streaming metrics

The **MBean** is `debezium.postgres:type=connector-metrics,context=streaming,server=<topic.prefix>`.

The following table lists the streaming metrics that are available.

| Attributes | Type | Description |
| :--------- | :--- | :---------- |
| `LastEvent` | string | The last streaming event that the connector has read. |
| `MilliSecondsSinceLastEvent` | long | The number of milliseconds since the connector has read and processed the most recent event. |
| `TotalNumberOfEventsSeen` | long | The total number of events that this connector has seen since the last start or metrics reset. |
| `TotalNumberOfCreateEventsSeen` | long | The total number of create events that this connector has seen since the last start or metrics reset. |
| `TotalNumberOfUpdateEventsSeen` | long | The total number of update events that this connector has seen since the last start or metrics reset. |
| `TotalNumberOfDeleteEventsSeen` | long | The total number of delete events that this connector has seen since the last start or metrics reset. |
| `NumberOfEventsFiltered` | long | The number of events that have been filtered by include/exclude list filtering rules configured on the connector. |
| `CapturedTables` | string[] | The list of tables that are captured by the connector. |
| `QueueTotalCapacity` | int | The length the queue used to pass events between the streamer and the main Kafka Connect loop. |
| `QueueRemainingCapacity` | int | The free capacity of the queue used to pass events between the streamer and the main Kafka Connect loop. |
| `Connected` | boolean | Flag that denotes whether the connector is currently connected to the database server. |
| `MilliSecondsBehindSource` | long | The number of milliseconds between the last change event’s timestamp and the connector processing it. The values will incoporate any differences between the clocks on the machines where the database server and the connector are running. |
| `NumberOfCommittedTransactions` | long | The number of processed transactions that were committed. |
| `SourceEventPosition` | Map<String, String> | The coordinates of the last received event. |
| `LastTransactionId` | string | Transaction identifier of the last processed transaction. |
| `MaxQueueSizeInBytes` | long | The maximum buffer of the queue in bytes. This metric is available if `max.queue.size.in.bytes` is set to a positive long value. |
| `CurrentQueueSizeInBytes` | long | The current volume, in bytes, of records in the queue. |

## Behavior when things go wrong

Debezium is a distributed system that captures all changes in multiple upstream databases; it never misses or loses an event. When the system is operating normally or being managed carefully then Debezium provides *exactly once* delivery of every change event record. If a fault does happen then the system does not lose any events. However, while it is recovering from the fault, it’s possible that the connector might emit some duplicate change events. In these abnormal situations, Debezium, like Kafka, provides *at least once* delivery of change events.

The rest of this section describes how Debezium handles various kinds of faults and problems.

### Configuration and startup errors

In the following situations, the connector fails when trying to start, reports an error/exception in the log, and stops running:

* The connector’s configuration is invalid.
* The connector cannot successfully connect to YugabyteDB by using the specified connection parameters.
* The connector is restarting from a previously-recorded position in the YugabyteDB WAL (by using the LSN) and YugabyteDB no longer has that history available.

In these cases, the error message has details about the problem and possibly a suggested workaround. After you correct the configuration or address the YugabyteDB problem, restart the connector.

### YugabyteDB becomes unavailable

<!-- todo Vaibhav: what will be the behavior of the connector when tserver goes down. talk about the resiliency in terms of smart driver -->

When the connector is running, the YugabyteDB server that it is connected to could become unavailable for any number of reasons. If this happens, the connector fails with an error and stops. When the server is available again, restart the connector.

The YugabyteDB connector externally stores the last processed offset in the form of a YugabyteDB LSN. After a connector restarts and connects to a server instance, the connector communicates with the server to continue streaming from that particular offset. This offset is available as long as the Debezium replication slot remains intact. Never drop a replication slot on the primary server or you will lose data.

<!-- YB Note removed from above: For information about failure cases in which a slot has been removed, see the next section. -->

### Cluster failures

### Kafka Connect process stops gracefully

Suppose that Kafka Connect is being run in distributed mode and a Kafka Connect process is stopped gracefully. Prior to shutting down that process, Kafka Connect migrates the process’s connector tasks to another Kafka Connect process in that group. The new connector tasks start processing exactly where the prior tasks stopped. There is a short delay in processing while the connector tasks are stopped gracefully and restarted on the new processes.

### Kafka Connect process crashes

If the Kafka Connector process stops unexpectedly, any connector tasks it was running terminate without recording their most recently processed offsets. When Kafka Connect is being run in distributed mode, Kafka Connect restarts those connector tasks on other processes. However, YugabyteDB connectors resume from the last offset that was recorded by the earlier processes. This means that the new replacement tasks might generate some of the same change events that were processed just prior to the crash. The number of duplicate events depends on the offset flush period and the volume of data changes just before the crash.

Because there is a chance that some events might be duplicated during a recovery from failure, consumers should always anticipate some duplicate events. Debezium changes are idempotent, so a sequence of events always results in the same state.

In each change event record, Debezium connectors insert source-specific information about the origin of the event, including the YugabyteDB server’s time of the event, the ID of the server transaction, and the position in the write-ahead log where the transaction changes were written. Consumers can keep track of this information, especially the LSN, to determine whether an event is a duplicate.

### Kafka becomes unavailable

As the connector generates change events, the Kafka Connect framework records those events in Kafka by using the Kafka producer API. Periodically, at a frequency that you specify in the Kafka Connect configuration, Kafka Connect records the latest offset that appears in those change events. If the Kafka brokers become unavailable, the Kafka Connect process that is running the connectors repeatedly tries to reconnect to the Kafka brokers. In other words, the connector tasks pause until a connection can be re-established, at which point the connectors resume exactly where they left off.

### Connector is stopped for a duration

If the connector is gracefully stopped, the database can continue to be used. Any changes are recorded in the YugabyteDB WAL. When the connector restarts, it resumes streaming changes where it left off. That is, it generates change event records for all database changes that were made while the connector was stopped.

A properly configured Kafka cluster is able to handle massive throughput. Kafka Connect is written according to Kafka best practices, and given enough resources a Kafka Connect connector can also handle very large numbers of database change events. Because of this, after being stopped for a while, when a Debezium connector restarts, it is very likely to catch up with the database changes that were made while it was stopped. How quickly this happens depends on the capabilities and performance of Kafka and the volume of changes being made to the data in YugabyteDB.

