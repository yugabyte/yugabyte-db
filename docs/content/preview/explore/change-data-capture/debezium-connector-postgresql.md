---
title: Debezium connector for YugabyteDB (Logical Replication)
headerTitle: Debezium connector for YugabyteDB (Logical Replication)
linkTitle: Debezium connector
description: Debezium is an open source distributed platform used to capture the changes in a database.
aliases:
  - /preview/explore/change-data-capture/debezium-connector-yugabytedb-ysql-pg
  - /preview/explore/change-data-capture/debezium-connector
  - /preview/explore/change-data-capture/debezium
  - /preview/explore/change-data-capture/debezium-connector-postgres
  - /preview/explore/change-data-capture/debezium-postgresql
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

The Debezium PostgreSQL connector captures row-level changes in the schemas of a PostgreSQL database.

The first time it connects to a PostgreSQL server or cluster, the connector takes a consistent snapshot of all schemas. After that snapshot is complete, the connector continuously captures row-level changes that insert, update, and delete database content and that were committed to a PostgreSQL database. The connector generates data change event records and streams them to Kafka topics. For each table, the default behavior is that the connector streams all generated events to a separate Kafka topic for that table. Applications and services consume data change event records from that topic.

## Overview

PostgreSQL’s [logical decoding](todo vaibhav) feature was introduced in version 9.4. It is a mechanism that allows the extraction of the changes that were committed to the transaction log and the processing of these changes in a user-friendly manner with the help of an [output plug-in](todo vaibhav). The output plug-in enables clients to consume the changes.

The PostgreSQL connector contains two main parts that work together to read and process database changes:

<!-- todo vaibhav: need sub-bullets -->
* A logical decoding output plug-in. You might need to install the output plug-in that you choose to use. You must configure a replication slot that uses your chosen output plug-in before running the PostgreSQL server. The plug-in can be one of the following:

    <!-- YB specific -->
    * `yboutput` is the plugin packaged with YugabyteDB. 

    * `pgoutput` is the standard logical decoding output plug-in in PostgreSQL 10+. It is maintained by the PostgreSQL community, and used by PostgreSQL itself for logical replication. This plug-in is always present so no additional libraries need to be installed. The Debezium connector interprets the raw replication event stream directly into change events.

<!-- YB note driver part -->
* Java code (the actual Kafka Connect connector) that reads the changes produced by the chosen logical decoding output plug-in. It uses PostgreSQL’s [streaming replication protocol](todo vaibhav), by means of the YugabyteDB JDBC driver

The connector produces a change event for every row-level insert, update, and delete operation that was captured and sends change event records for each table in a separate Kafka topic. Client applications read the Kafka topics that correspond to the database tables of interest, and can react to every row-level event they receive from those topics.

PostgreSQL normally purges write-ahead log (WAL) segments after some period of time. This means that the connector does not have the complete history of all changes that have been made to the database. Therefore, when the PostgreSQL connector first connects to a particular PostgreSQL database, it starts by performing a consistent snapshot of each of the database schemas. After the connector completes the snapshot, it continues streaming changes from the exact point at which the snapshot was made. This way, the connector starts with a consistent view of all of the data, and does not omit any changes that were made while the snapshot was being taken.

The connector is tolerant of failures. As the connector reads changes and produces events, it records the WAL position for each event. If the connector stops for any reason (including communication failures, network problems, or crashes), upon restart the connector continues reading the WAL where it last left off. This includes snapshots. If the connector stops during a snapshot, the connector begins a new snapshot when it restarts.

{{< tip title="Behaviour with Logical Decoding" >}}

The connector relies on and reflects the PostgreSQL logical decoding feature, which has the following limitations:

* Logical decoding does not support DDL changes. This means that the connector is unable to report DDL change events back to consumers.

* Logical decoding replication slots are supported on only primary servers. When there is a cluster of PostgreSQL servers, the connector can run on only the active primary server. It cannot run on hot or warm standby replicas. If the primary server fails or is demoted, the connector stops. After the primary server has recovered, you can restart the connector. If a different PostgreSQL server has been promoted to primary, adjust the connector configuration before restarting the connector.

Additionally, the pgoutput logical decoding output plug-in does not capture values for generated columns, resulting in missing data for these columns in the connector’s output.

[Behavior when things go wrong](todo vaibhav) describes how the connector responds if there is a problem.

{{< /tip >}}

{{< tip title="Use UTF-8 encoding" >}}

Debezium supports databases with UTF-8 character encoding only. With a single-byte character encoding, it's not possible to correctly process strings that contain extended ASCII code characters.

{{< /tip >}}

## How the connector works

To optimally configure and run a Debezium PostgreSQL connector, it is helpful to understand how the connector performs snapshots, streams change events, determines Kafka topic names, and uses metadata.

### Security

To use the Debezium connector to stream changes from a PostgreSQL database, the connector must operate with specific privileges in the database. Although one way to grant the necessary privileges is to provide the user with `superuser` privileges, doing so potentially exposes your PostgreSQL data to unauthorized access. Rather than granting excessive privileges to the Debezium user, it is best to create a dedicated Debezium replication user to which you grant specific privileges.

For more information about configuring privileges for the Debezium PostgreSQL user, see [Setting up permissions](todo vaibhav). For more information about PostgreSQL logical replication security, see the [PostgreSQL documentation](todo vaibhav).

### Snapshots

Most PostgreSQL servers are configured to not retain the complete history of the database in the WAL segments. This means that the PostgreSQL connector would be unable to see the entire history of the database by reading only the WAL. Consequently, the first time that the connector starts, it performs an initial consistent snapshot of the database.

#### Default workflow behavior of initial snapshots

The default behavior for performing a snapshot consists of the following steps. You can change this behavior by setting the `snapshot.mode` [connector configuration property](todo vaibhav) to a value other than `initial`.

1. Start a transaction with a [SERIALIZABLE, READ ONLY, DEFERRABLE](todo vaibhav) isolation level to ensure that subsequent reads in this transaction are against a single consistent version of the data. Any changes to the data due to subsequent `INSERT`, `UPDATE`, and `DELETE` operations by other clients are not visible to this transaction.
2. Read the current position in the server’s transaction log.
3. Scan the database tables and schemas, generate a `READ` event for each row and write that event to the appropriate table-specific Kafka topic.
4. Commit the transaction.
5. Record the successful completion of the snapshot in the connector offsets.

If the connector fails, is rebalanced, or stops after Step 1 begins but before Step 5 completes, upon restart the connector begins a new snapshot. After the connector completes its initial snapshot, the PostgreSQL connector continues streaming from the position that it read in Step 2. This ensures that the connector does not miss any updates. If the connector stops again for any reason, upon restart, the connector continues streaming changes from where it previously left off.

*Options for the **snapshot.mode** connector configuration property:*

| Option | Description |
| :--- | :--- |
| `always` | The connector always performs a snapshot when it starts. After the snapshot completes, the connector continues streaming changes from step 3 in the above sequence. This mode is useful in these situations:<br/><br/> <ul><li>It is known that some WAL segments have been deleted and are no longer available.</li> <li>After a cluster failure, a new primary has been promoted. The always snapshot mode ensures that the connector does not miss any changes that were made after the new primary had been promoted but before the connector was restarted on the new primary.</li></ul> |
| `never` | The connector never performs snapshots. When a connector is configured this way, its behavior when it starts is as follows. If there is a previously stored LSN in the Kafka offsets topic, the connector continues streaming changes from that position. If no LSN has been stored, the connector starts streaming changes from the point in time when the PostgreSQL logical replication slot was created on the server. The `never` snapshot mode is useful only when you know all data of interest is still reflected in the WAL. |
| `initial` (default) | The connector performs a database snapshot when no Kafka offsets topic exists. After the database snapshot completes the Kafka offsets topic is written. If there is a previously stored LSN in the Kafka offsets topic, the connector continues streaming changes from that position. |
| `initial_only` | The connector performs a database snapshot and stops before streaming any change event records. If the connector had started but did not complete a snapshot before stopping, the connector restarts the snapshot process and stops when the snapshot completes. |
| `custom` | The `custom` snapshot mode lets you inject your own implementation of the `io.debezium.connector.postgresql.spi.Snapshotter` interface. Set the `snapshot.custom.class` configuration property to the class on the classpath of your Kafka Connect cluster or included in the JAR if using the `EmbeddedEngine`. For more details, see [custom snapshotter SPI](todo vaibhav). |

<!-- YB note Skipping Ad-hoc snapshots -->

### Incremental snapshots

To provide flexibility in managing snapshots, Debezium includes a supplementary snapshot mechanism, known as *incremental snapshotting*. Incremental snapshots rely on the Debezium mechanism for [sending signals to a Debezium connector](todo vaibhav). Incremental snapshots are based on the [DDD-3](todo vaibhav) design document.

In an incremental snapshot, instead of capturing the full state of a database all at once, as in an initial snapshot, Debezium captures each table in phases, in a series of configurable chunks. You can specify the tables that you want the snapshot to capture and the [size of each chunk](todo vaibhav). The chunk size determines the number of rows that the snapshot collects during each fetch operation on the database. The default chunk size for incremental snapshots is 1024 rows.

As an incremental snapshot proceeds, Debezium uses watermarks to track its progress, maintaining a record of each table row that it captures. This phased approach to capturing data provides the following advantages over the standard initial snapshot process:
* You can run incremental snapshots in parallel with streamed data capture, instead of postponing streaming until the snapshot completes. The connector continues to capture near real-time events from the change log throughout the snapshot process, and neither operation blocks the other.
* If the progress of an incremental snapshot is interrupted, you can resume it without losing any data. After the process resumes, the snapshot begins at the point where it stopped, rather than recapturing the table from the beginning.
* You can run an incremental snapshot on demand at any time, and repeat the process as needed to adapt to database updates. For example, you might re-run a snapshot after you modify the connector configuration to add a table to its `table.include.list` property.

#### Incremental snapshot process

When you run an incremental snapshot, Debezium sorts each table by primary key and then splits the table into chunks based on the [configured chunk size](todo vaibhav). Working chunk by chunk, it then captures each table row in a chunk. For each row that it captures, the snapshot emits a `READ` event. That event represents the value of the row when the snapshot for the chunk began.

As a snapshot proceeds, it’s likely that other processes continue to access the database, potentially modifying table records. To reflect such changes,`INSERT`, `UPDATE`, or `DELETE` operations are committed to the transaction log as per usual. Similarly, the ongoing Debezium streaming process continues to detect these change events and emits corresponding change event records to Kafka.

#### How Debezium resolves collisions among records with the same primary key

In some cases, the `UPDATE` or `DELETE` events that the streaming process emits are received out of sequence. That is, the streaming process might emit an event that modifies a table row before the snapshot captures the chunk that contains the `READ` event for that row. When the snapshot eventually emits the corresponding `READ` event for the row, its value is already superseded. To ensure that incremental snapshot events that arrive out of sequence are processed in the correct logical order, Debezium employs a buffering scheme for resolving collisions. Only after collisions between the snapshot events and the streamed events are resolved does Debezium emit an event record to Kafka.

#### Snapshot window

To assist in resolving collisions between late-arriving `READ` events and streamed events that modify the same table row, Debezium employs a so-called *snapshot window*. The snapshot windows demarcates the interval during which an incremental snapshot captures data for a specified table chunk. Before the snapshot window for a chunk opens, Debezium follows its usual behavior and emits events from the transaction log directly downstream to the target Kafka topic. But from the moment that the snapshot for a particular chunk opens, until it closes, Debezium performs a de-duplication step to resolve collisions between events that have the same primary key..

For each data collection, the Debezium emits two types of events, and stores the records for them both in a single destination Kafka topic. The snapshot records that it captures directly from a table are emitted as `READ` operations. Meanwhile, as users continue to update records in the data collection, and the transaction log is updated to reflect each commit, Debezium emits `UPDATE` or `DELETE` operations for each change.

As the snapshot window opens, and Debezium begins processing a snapshot chunk, it delivers snapshot records to a memory buffer. During the snapshot windows, the primary keys of the `READ` events in the buffer are compared to the primary keys of the incoming streamed events. If no match is found, the streamed event record is sent directly to Kafka. If Debezium detects a match, it discards the buffered `READ` event, and writes the streamed record to the destination topic, because the streamed event logically supersede the static snapshot event. After the snapshot window for the chunk closes, the buffer contains only `READ` events for which no related transaction log events exist. Debezium emits these remaining `READ` events to the table’s Kafka topic.

The connector repeats the process for each snapshot chunk.

{{< warning title="Warning" >}}

The Debezium connector for PostgreSQL does not support schema changes while an incremental snapshot is running. If a schema change is performed *before* the incremental snapshot start but *after* sending the signal then passthrough config option `database.autosave` is set to `conservative` to correctly process the schema change.

{{< /warning >}}

#### Triggering an incremental snapshot

Currently, the only way to initiate an incremental snapshot is to send an [ad hoc snapshot signal](todo vaibhav) to the signaling table on the source database.

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
* [Signaling is enabled](todo vaibhav).
    * A signaling data collection exists on the source database.
    * The signaling data collection is specified in the `signal.data.collection` property.

<!-- todo vaibhav add rest of incremental snapshot and custom snapshotter SPI content -->

### Streaming changes

The PostgreSQL connector typically spends the vast majority of its time streaming changes from the PostgreSQL server to which it is connected. This mechanism relies on [PostgreSQL’s replication protocol](todo vaibhav). This protocol enables clients to receive changes from the server as they are committed in the server’s transaction log at certain positions, which are referred to as Log Sequence Numbers (LSNs).

Whenever the server commits a transaction, a separate server process invokes a callback function from the [logical decoding plug-in](todo vaibhav). This function processes the changes from the transaction, converts them to a specific format (Protobuf or JSON in the case of Debezium plug-in) and writes them on an output stream, which can then be consumed by clients.

The Debezium PostgreSQL connector acts as a PostgreSQL client. When the connector receives changes it transforms the events into Debezium *create*, *update*, or *delete* events that include the LSN of the event. The PostgreSQL connector forwards these change events in records to the Kafka Connect framework, which is running in the same process. The Kafka Connect process asynchronously writes the change event records in the same order in which they were generated to the appropriate Kafka topic.

Periodically, Kafka Connect records the most recent *offset* in another Kafka topic. The offset indicates source-specific position information that Debezium includes with each event. For the PostgreSQL connector, the LSN recorded in each change event is the offset.

When Kafka Connect gracefully shuts down, it stops the connectors, flushes all event records to Kafka, and records the last offset received from each connector. When Kafka Connect restarts, it reads the last recorded offset for each connector, and starts each connector at its last recorded offset. When the connector restarts, it sends a request to the PostgreSQL server to send the events starting just after that position.

{{< note title="Note" >}}

The PostgreSQL connector retrieves schema information as part of the events sent by the logical decoding plug-in. However, the connector does not retrieve information about which columns compose the primary key. The connector obtains this information from the JDBC metadata (side channel). If the primary key definition of a table changes (by adding, removing or renaming primary key columns), there is a tiny period of time when the primary key information from JDBC is not synchronized with the change event that the logical decoding plug-in generates. During this tiny period, a message could be created with an inconsistent key structure. To prevent this inconsistency, update primary key structures as follows:
1. Put the database or an application into a read-only mode.
2. Let Debezium process all remaining events.
3. Stop Debezium.
4. Update the primary key definition in the relevant table.
5. Put the database or the application into read/write mode.
6. Restart Debezium.

{{< /note >}}

### PostgreSQL 10+ logical decoding support (pgoutput)

As of PostgreSQL 10+, there is a logical replication stream mode, called pgoutput that is natively supported by PostgreSQL. This means that a Debezium PostgreSQL connector can consume that replication stream without the need for additional plug-ins. This is particularly valuable for environments where installation of plug-ins is not supported or not allowed.

For more information, see [Setting up PostgreSQL](todo vaibhav).

### Topic names

By default, the PostgreSQL connector writes change events for all `INSERT`, `UPDATE`, and `DELETE` operations that occur in a table to a single Apache Kafka topic that is specific to that table. The connector names change event topics as _topicPrefix.schemaName.tableName_.

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

If the default topic names don't meet your requirements, you can configure custom topic names. To configure custom topic names, you specify regular expressions in the logical topic routing SMT. For more information about using the logical topic routing SMT to customize topic naming, see the Debezium documentation on [Topic routing](todo vaibhav).

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

The Debezium PostgreSQL connector generates a data change event for each row-level `INSERT`, `UPDATE`, and `DELETE` operation. Each event contains a key and a value. The structure of the key and the value depends on the table that was changed.

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

By default behavior is that the connector streams change event records to [topics with names that are the same as the event’s originating table](todo vaibhav).

{{< note title="Note" >}}

Starting with Kafka 0.10, Kafka can optionally record the event key and value with the [timestamp](todo vaibhav) at which the message was created (recorded by the producer) or written to the log by Kafka.

{{< /note >}}

{{< warning title="Warning" >}}

The PostgreSQL connector ensures that all Kafka Connect schema names adhere to the Avro schema name format. This means that the logical server name must start with a Latin letter or an underscore, that is, a-z, A-Z, or _. Each remaining character in the logical server name and each character in the schema and table names must be a Latin letter, a digit, or an underscore, that is, a-z, A-Z, 0-9, or \_. If there is an invalid character it is replaced with an underscore character.

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

If the `topic.prefix` connector configuration property has the value `PostgreSQL_server`, every change event for the `customers` table while it has this definition has the same key structure, which in JSON looks like this:

```output.json
{
  "schema": { --> 1
    "type": "struct",
    "name": "PostgreSQL_server.public.customers.Key", --> 2
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
| 2 | PostgreSQL_server.public.customers.Key | Name of the schema that defines the structure of the key's payload. This schema describes the structure of the primary key for the table that was changed. Key schema names have the format _connector-name.database-name.table-name.Key_. In this example: <br/> `PostgreSQL_server` is the name of the connector that generated this event. <br/> `public` is the schema which contains the table that was changed. <br/> `customers` is the table that was updated. |
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

[REPLICA IDENTITY](todo vaibhav) is a PostgreSQL-specific table-level setting that determines the amount of information that is available to the logical decoding plug-in for `UPDATE` and `DELETE` events. More specifically, the setting of `REPLICA IDENTITY` controls what (if any) information is available for the previous values of the table columns involved, whenever an `UPDATE` or `DELETE` event occurs.

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
                "name": "PostgreSQL_server.inventory.customers.Value", --> 2
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
                "name": "PostgreSQL_server.inventory.customers.Value",
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
                "name": "io.debezium.connector.postgresql.Source", --> 3
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
        "name": "PostgreSQL_server.public.customers.Envelope" --> 4
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
            "connector": "postgresql",
            "name": "PostgreSQL_server",
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
| 1 | schema | The value’s schema, which describes the structure of the value’s payload. A change event’s value schema is the same in every change event that the connector generates for a particular table. |
| 2 | name | In the schema section, each name field specifies the schema for a field in the value’s payload.<br/><br/>
`PostgreSQL_server.inventory.customers.Value` is the schema for the payload’s *before* and *after* fields. This schema is specific to the customers table.<br/><br/>Names of schemas for *before* and *after* fields are of the form *logicalName.tableName.Value*, which ensures that the schema name is unique in the database. This means that when using the [Avro converter](todo vaibhav), the resulting Avro schema for each table in each logical source has its own evolution and history. |
| 3 | name | `io.debezium.connector.postgresql.Source` is the schema for the payload’s `source` field. This schema is specific to the PostgreSQL connector. The connector uses it for all events that it generates. |
| 4 | name | `PostgreSQL_server.inventory.customers.Envelope` is the schema for the overall structure of the payload, where `PostgreSQL_server` is the connector name, `public` is the schema, and `customers` is the table. |
| 5 | payload | 	
The value’s actual data. This is the information that the change event is providing.<br/><br/>It may appear that the JSON representations of the events are much larger than the rows they describe. This is because the JSON representation must include the schema and the payload portions of the message. However, by using the [Avro converter](todo vaibhav), you can significantly decrease the size of the messages that the connector streams to Kafka topics. |
| 6 | before | An optional field that specifies the state of the row before the event occurred. When the op field is `c` for create, as it is in this example, the `before` field is `null` since this change event is for new content.<br/>
{{< note title="Note" >}}

Whether or not this field is available is dependent on the [REPLICA IDENTITY](#replica-identity) setting for each table.

{{< /note >}} |
| 7 | after | An optional field that specifies the state of the row after the event occurred. In this example, the `after` field contains the values of the new row’s `id`, `first_name`, `last_name`, and `email` columns. |
| 8 | source | Mandatory field that describes the source metadata for the event. This field contains information that you can use to compare this event with other events, with regard to the origin of the events, the order in which the events occurred, and whether events were part of the same transaction. The source metadata includes:<br/><ul><li>Debezium version</li><li>Connector type and name</li><li>Database and table that contains the new row</li><li>Stringified JSON array of additional offset information. The first value is always the last committed LSN, the second value is always the current LSN. Either value may be null.</li><li>Schema name</li><li>If the event was part of a snapshot</li><li>ID of the transaction in which the operation was performed</li><li>Offset of the operation in the database log</li><li>Timestamp for when the change was made in the database</li></ul> |
| 9 | op | Mandatory string that describes the type of operation that caused the connector to generate the event. In this example, `c` indicates that the operation created a row. Valid values are: <ul><li> `c` = create <li> `r` = read (applies to only snapshots) <li> `u` = update <li> `d` = delete <li> `m` = message</ul> |
| 10 | ts_ms | Optional field that displays the time at which the connector processed the event. The time is based on the system clock in the JVM running the Kafka Connect task.<br/><br/>In the `source` object, `ts_ms` indicates the time that the change was made in the database. By comparing the value for `payload.source.ts_ms` with the value for `payload.ts_ms`, you can determine the lag between the source database update and Debezium. |