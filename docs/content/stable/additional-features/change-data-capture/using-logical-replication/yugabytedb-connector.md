---
title: YugabyteDB connector
headerTitle: YugabyteDB connector
linkTitle: YugabyteDB connector
description: YugabyteDB connector for Change Data Capture in YugabyteDB.
aliases:
  - /stable/explore/change-data-capture/using-logical-replication/yugabytedb-connector/
menu:
  stable:
    parent: explore-change-data-capture-logical-replication
    identifier: yugabytedb-connector
    weight: 70
type: docs
rightNav:
  hideH4: true
---

The YugabyteDB Connector is based on the Debezium Connector, and captures row-level changes in the schemas of a YugabyteDB database using the PostgreSQL replication protocol.

The first time it connects to a YugabyteDB server, the connector takes a consistent snapshot of all schemas. After that snapshot is complete, the connector continuously captures row-level changes that insert, update, and delete database content, and that were committed to a YugabyteDB database. The connector generates data change event records and streams them to Kafka topics. For each table, the default behavior is that the connector streams all generated events to a separate Kafka topic for that table. Applications and services consume data change event records from that topic.

## Overview

YugabyteDB CDC using logical decoding is a mechanism that allows the extraction of changes that were committed to the transaction log and the processing of these changes in a user-friendly manner with the help of a [PostgreSQL output plugin](https://www.postgresql.org/docs/15/logicaldecoding-output-plugin.html). The output plugin enables clients to consume the changes.

The YugabyteDB connector contains two main parts that work together to read and process database changes:

* You must configure a replication slot that uses your chosen output plugin before running the YugabyteDB server. The plugin can be one of the following:

  <!-- YB specific -->
  * `yboutput` is the plugin packaged with YugabyteDB. It is maintained by Yugabyte and is always present with the distribution.

  * `pgoutput` is the standard logical decoding output plugin in PostgreSQL 10+. It is maintained by the PostgreSQL community, and used by PostgreSQL itself for logical replication. YugabyteDB bundles this plugin with the standard distribution so it is always present and no additional libraries need to be installed. The YugabyteDB connector interprets the raw replication event stream directly into change events.

<!-- YB note driver part -->
* Java code (the actual Kafka Connect connector) that reads the changes produced by the chosen logical decoding output plugin. It uses the [streaming replication protocol](https://www.postgresql.org/docs/15/protocol-replication.html), by means of the YugabyteDB JDBC driver.

The connector produces a change event for every row-level insert, update, and delete operation that was captured, and sends change event records for each table in a separate Kafka topic. Client applications read the Kafka topics that correspond to the database tables of interest, and can react to every row-level event they receive from those topics.

YugabyteDB normally purges write-ahead log (WAL) segments after some period of time. This means that the connector does not have the complete history of all changes that have been made to the database. Therefore, when the YugabyteDB connector first connects to a particular YugabyteDB database, it starts by performing a consistent snapshot of each of the configured tables. After the connector completes the snapshot, it continues streaming changes from the exact point at which the snapshot was made. This way, the connector starts with a consistent view of all of the data, and does not omit any changes that were made while the snapshot was being taken.

The connector is tolerant of failures. As the connector reads changes and produces events, it records the Log Sequence Number ([LSN](../key-concepts/#lsn-type)) for each event. If the connector stops for any reason (including communication failures, network problems, or crashes), upon restart the connector continues reading the WAL where it last left off.

{{< tip title="Use UTF-8 encoding" >}}

Debezium supports databases with UTF-8 character encoding only. With a single-byte character encoding, it's not possible to correctly process strings that contain extended ASCII code characters.

{{< /tip >}}

## How the connector works

To optimally configure and run a Debezium connector, it is helpful to understand how the connector performs snapshots, streams change events, determines Kafka topic names, and uses metadata.

### Security

To use the Debezium connector to stream changes from a YugabyteDB database, the connector must operate with specific privileges in the database. Although one way to grant the necessary privileges is to provide the user with `superuser` privileges, doing so potentially exposes your YugabyteDB data to unauthorized access. Rather than granting excessive privileges to the Debezium user, it is best to create a dedicated Debezium replication user to which you grant specific privileges.

For more information about configuring privileges for the Debezium replication user, see [Setting up permissions](#setting-up-permissions).

### Snapshots

Most YugabyteDB servers are configured to not retain the complete history of the database in the WAL segments. This means that the YugabyteDB connector would be unable to see the entire history of the database by reading only the WAL. Consequently, the first time that the connector starts, it performs an initial consistent snapshot of the database.

#### Default workflow behavior of initial snapshots

The default behavior for performing a snapshot consists of the following steps. You can change this behavior by setting the `snapshot.mode` [connector configuration property](../yugabytedb-connector-properties/#advanced-configuration-properties) to a value other than `initial`.

1. Start a transaction.
2. Set the transaction read time to the [consistent point](../../../../architecture/docdb-replication/cdc-logical-replication/#initial-snapshot) associated with the replication slot.
3. Execute snapshot through the execution of a `SELECT` query.
4. Generate a `READ` event for each row and write to the appropriate table-specific Kafka topic.
5. Record successful completion of the snapshot in the connector offsets.

If the connector fails, is rebalanced, or stops after Step 1 begins but before Step 5 completes, upon restart the connector begins a new snapshot. After the connector completes its initial snapshot, the YugabyteDB connector continues streaming from the position that it read in Step 2. This ensures that the connector does not miss any updates. If the connector stops again for any reason, upon restart, the connector continues streaming changes from where it previously left off.

The following table describes the options for the `snapshot.mode` connector configuration property.

| Option | Description |
| :--- | :--- |
| `never` | The connector never performs snapshots. When a connector is configured this way, its behavior when it starts is as follows. If there is a previously stored LSN in the Kafka offsets topic, the connector continues streaming changes from that position. If no LSN has been stored, the connector starts streaming changes from the point in time when the YugabyteDB logical replication slot was created on the server. The `never` snapshot mode is beneficial only when you know all data of interest is still reflected in the WAL. |
| `initial` (default) | The connector performs a database snapshot when no Kafka offsets topic exists. After the database snapshot completes the Kafka offsets topic is written. If there is a previously stored LSN in the Kafka offsets topic, the connector continues streaming changes from that position. |
| `initial_only` | The connector performs a database snapshot and stops before streaming any change event records. If the connector had started but did not complete a snapshot before stopping, the connector restarts the snapshot process and stops when the snapshot completes. |

### Streaming changes

The YugabyteDB connector typically spends the vast majority of its time streaming changes from the YugabyteDB server to which it is connected. This mechanism relies on [PostgreSQL's replication protocol](https://www.postgresql.org/docs/15/protocol-replication.html). This protocol enables clients to receive changes from the server as they are committed in the server's transaction logs.

Whenever the server commits a transaction, a separate server process invokes a callback function from the [logical decoding plugin](../key-concepts/#output-plugin). This function processes the changes from the transaction, converts them to a specific format and writes them on an output stream, which can then be consumed by clients.

The YugabyteDB connector acts as a YugabyteDB client. When the connector receives changes it transforms the events into Debezium create, update, or delete events that include the LSN of the event. The YugabyteDB connector forwards these change events in records to the Kafka Connect framework, which is running in the same process. The Kafka Connect process asynchronously writes the change event records in the same order in which they were generated to the appropriate Kafka topic.

Periodically, Kafka Connect records the most recent offset in another Kafka topic. The offset indicates source-specific position information that Debezium includes with each event. For the YugabyteDB connector, the LSN recorded in each change event is the offset.

When Kafka Connect gracefully shuts down, it stops the connectors, flushes all event records to Kafka, and records the last offset received from each connector. When Kafka Connect restarts, it reads the last recorded offset for each connector, and starts each connector at its last recorded offset. When the connector restarts, it sends a request to the YugabyteDB server to send the events starting just after that position.

### Logical decoding plugin support

As of YugabyteDB v2024.1.1 and later, YugabyteDB supports the [yboutput plugin](../key-concepts/#output-plugin), a native output plugin for logical decoding.

Additionally, YugabyteDB also supports the PostgreSQL `pgoutput` plugin natively. This means that the YugabyteDB connector can work with an existing setup configured using `pgoutput`.

### Topic names

By default, the YugabyteDB connector writes change events for all `INSERT`, `UPDATE`, and `DELETE` operations that occur in a table to a single Apache Kafka topic that is specific to that table. The connector names change event topics as _topicPrefix.schemaName.tableName_.

The components of a topic name are as follows:

* _topicPrefix_ - the topic prefix as specified by the `topic.prefix` configuration property.
* _schemaName_ - the name of the database schema in which the change event occurred.
* _tableName_ - the name of the database table in which the change event occurred.

For example, suppose that `dbserver` is the topic prefix in the configuration for a connector that is capturing changes in a YugabyteDB installation that has a `yugabyte` database and an `inventory` schema that contains four tables: `products`, `products_on_hand`, `customers`, and `orders`. The connector would stream records to these four Kafka topics:

* `dbserver.inventory.products`
* `dbserver.inventory.products_on_hand`
* `dbserver.inventory.customers`
* `dbserver.inventory.orders`

Now suppose that the tables are not part of a specific schema but were created in the default public YugabyteDB schema. The names of the Kafka topics would be:

* `dbserver.public.products`
* `dbserver.public.products_on_hand`
* `dbserver.public.customers`
* `dbserver.public.orders`

The connector applies similar naming conventions to label its [transaction metadata topics](#transaction-metadata).

If the default topic names don't meet your requirements, you can configure custom topic names. To configure custom topic names, you specify regular expressions in the logical topic routing SMT. For more information about using the logical topic routing SMT to customize topic naming, see the Debezium documentation on [Topic routing](https://debezium.io/documentation/reference/2.5/transformations/topic-routing.html).

### Transaction metadata

Debezium can generate events that represent transaction boundaries and that enrich data change event messages.

{{< note title="Limits on when Debezium receives transaction metadata" >}}

Debezium registers and receives metadata only for transactions that occur _after you deploy the connector_. Metadata for transactions that occur before you deploy the connector is not available.

{{< /note >}}

For every transaction `BEGIN` and `END`, Debezium generates an event containing the following fields:

* `status` - `BEGIN` or `END`.
* `id` - String representation of the unique transaction identifier composed of YugabyteDB transaction ID itself and LSN of given operation separated by colon, that is, the format is `txID:LSN`.
* `ts_ms` - The time of a transaction boundary event (`BEGIN` or `END` event) at the data source. If the data source does not provide Debezium with the event time, then the field instead represents the time at which Debezium processes the event.
* `event_count` (for `END` events) - total number of events emitted by the transaction.
* `data_collections` (for `END` events) - an array of pairs of `data_collection` and `event_count` that provides the number of events emitted by changes originating from given data collection.

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

The YugabyteDB connector generates a data change event for each row-level `INSERT`, `UPDATE`, and `DELETE` operation. Each event contains a key and a value. The structure of the key and the value depends on the table that was changed.

Debezium and Kafka Connect are designed around _continuous streams of event messages_. However, the structure of these events may change over time, which can be difficult for consumers to handle. To address this, each event contains the schema for its content or, if you are using a schema registry, a schema ID that a consumer can use to obtain the schema from the registry. This makes each event self-contained.

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

The following table describes the content of the change events.

| Item | Field&nbsp;name | Description |
| :--: | :--------- | :---------- |
| 1 | `schema` | The first `schema` field is part of the event key. It specifies a Kafka Connect schema that describes what is in the event key's `payload` portion. In other words, the first `schema` field describes the structure of the primary key, or the unique key if the table does not have a primary key, for the table that was changed. |
| 2 | `payload` | The first `payload` field is part of the event key. It has the structure described by the previous `schema` field and it contains the key for the row that was changed. |
| 3 | `schema` | The second `schema` field is part of the event value. It specifies the Kafka Connect schema that describes what is in the event value's `payload` portion. In other words, the second `schema` describes the structure of the row that was changed. Typically, this schema contains nested schemas. |
| 4 | `payload` | The second `payload` field is part of the event value. It has the structure described by the previous `schema` field and it contains the actual data for the row that was changed. |

By default, the connector streams change event records to [Kafka topics](#topic-names) with names that are the same as the event's originating table.

{{< note title="Note" >}}

Starting with Kafka 0.10, Kafka can optionally record the event key and value with the timestamp at which the message was created (recorded by the producer) or written to the log by Kafka.

{{< /note >}}

{{< warning title="Warning" >}}

The YugabyteDB connector ensures that all Kafka Connect schema names adhere to the Avro schema name format. This means that the logical server name must start with a Latin letter or an underscore, that is, `a-z`, `A-Z`, or `_`. Each remaining character in the logical server name and each character in the schema and table names must be a Latin letter, a digit, or an underscore, that is, `a-z`, `A-Z`, `0-9`, or `_`. If there is an invalid character it is replaced with an underscore character.

This can lead to unexpected conflicts if the topic prefix, a schema name, or a table name contains invalid characters, and the only characters that distinguish names from one another are invalid and thus replaced with underscores.

{{< /warning >}}

### Change event keys

For a given table, the change event's key has a structure that contains a field for each column in the primary key of the table at the time the event was created. Alternatively, if the table has `REPLICA IDENTITY` set to `FULL` there is a field for each unique key constraint.

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

| Item | Field&nbsp;name | Description |
| :--- | :--------- | :---------- |
| 1 | schema | The schema portion of the key specifies a Kafka Connect schema that describes what is in the key's `payload` portion. |
| 2 | YugabyteDB_server.public.customers.Key | Name of the schema that defines the structure of the key's payload. This schema describes the structure of the primary key for the table that was changed. Key schema names have the format _connector-name.database-name.table-name.Key_. In this example: <br/> `YugabyteDB_server` is the name of the connector that generated this event. <br/> `public` is the schema which contains the table that was changed. <br/> `customers` is the table that was updated. |
| 3 | optional | Indicates whether the event key must contain a value in its `payload` field. In this example, a value in the key's payload is required. |
| 4 | fields | Specifies each field that is expected in the payload, including each field's name, index, and schema. |
| 5 | payload | Contains the key for the row for which this change event was generated. In this example, the key, contains a single `id` field whose value is `1`. |

{{< note title="Note" >}}

Although the `column.exclude.list` and `column.include.list` connector configuration properties allow you to capture only a subset of table columns, all columns in a primary or unique key are always included in the event's key.

{{< /note >}}

{{< warning title="Warning" >}}

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

[REPLICA IDENTITY](https://www.postgresql.org/docs/15/sql-altertable.html#SQL-ALTERTABLE-REPLICA-IDENTITY) is a YugabyteDB-specific table-level setting that determines the amount of information that is available to the logical decoding plugin for `UPDATE` and `DELETE` events. More specifically, the `REPLICA IDENTITY` setting controls what (if any) information is available for the previous values of the table columns involved, whenever an `UPDATE` or `DELETE` event occurs.

There are 4 possible values for `REPLICA IDENTITY`:

* `CHANGE` - Emitted events for `UPDATE` operations will only contain the value of the changed column along with the primary key column with no previous values present. `DELETE` operations will only contain the previous value of the primary key column in the table.
* `DEFAULT` - The default behavior is that only `DELETE` events contain the previous values for the primary key columns of a table. For an `UPDATE` event, no previous values will be present and the new values will be present for all the columns in the table.
* `FULL` - Emitted events for `UPDATE` and `DELETE` operations contain the previous values of all columns in the table.
* `NOTHING` - Emitted events for `UPDATE` and `DELETE` operations do not contain any information about the previous value of any table column.

{{< note title="Note">}}

The pgoutput plugin does not support replica identity CHANGE.

The PostgreSQL replica identity `INDEX` is not supported in YugabyteDB.

{{< /note >}}

For information on setting the replica identity of tables, refer to [Replica identity](../key-concepts/#replica-identity).

#### Message formats for replica identities

Consider the following employee table into which a row is inserted, subsequently updated, and deleted:

```sql
CREATE TABLE employee (
  employee_id INT PRIMARY KEY,
  employee_name VARCHAR,
  employee_dept TEXT);

INSERT INTO employee VALUES (1001, 'Alice', 'Packaging');

UPDATE employee SET employee_name = 'Bob' WHERE employee_id = 1001;

DELETE FROM employee WHERE employee_id = 1001;
```

{{< tabpane text=true >}}

  {{% tab header="CHANGE" lang="change" %}}

**yboutput plugin**

<table>
<tr>
<td> <b>INSERT</b> </td> <td> <b>UPDATE</b> </td> <td> <b>DELETE</b> </td>
</tr>

<tr>
<td>
<pre>
{
  "before": null,
  "after": {
    "employee_id": {
        "value": 1001,
        "set": true
    },
    "employee_name": {
      "value": "Alice",
      "set": true
    },
    "employee_dept": {
        "value": "Packaging",
        "set": true
    }
  }
  "op": "c"
}
</pre>
</td>

<td>
<pre>
{
  "before": null,
  "after": {
    "employee_id": {
        "value": 1001,
        "set": true
    },
    "employee_name": {
      "value": "Bob",
      "set": true
    },
    "employee_dept": null
  }
  "op": "u"
}
</pre>
</td>

<td>
<pre>
{
  "before": {
    "employee_id": {
      "value": 1001,
      "set": true
    },
    "employee_name": null,
    "employee_dept": null
  },
  "after": null,
  "op": "d"
}
</pre>
</td>
</tr>

</table>

  {{% /tab %}}

  {{% tab header="DEFAULT" lang="default" %}}

**yboutput plugin**

<table>
<tr>
<td> <b>INSERT</b> </td> <td> <b>UPDATE</b> </td> <td> <b>DELETE</b> </td>
</tr>

<tr>

<td>
<pre>
{
  "before": null,
  "after": {
    "employee_id": {
        "value": 1001,
        "set": true
    },
    "employee_name": {
      "value": "Alice",
      "set": true
    },
    "employee_dept": {
        "value": "Packaging",
        "set": true
    }
  }
  "op": "c"
}
</pre>
</td>

<td>
<pre>
{
  "before": null,
  "after": {
    "employee_id": {
        "value": 1001,
        "set": true
    },
    "employee_name": {
      "value": "Bob",
      "set": true
    },
    "employee_dept": null
  }
  "op": "u"
}
</pre>
</td>

<td>
<pre>
{
  "before": {
    "employee_id": {
      "value": 1001,
      "set": true
    },
    "employee_name": null,
    "employee_dept": null
  },
  "after": null,
  "op": "d"
}
</pre>
</td>
</tr>
</table>

**pgoutput plugin**

<table>
<tr>
<td> <b>INSERT</b> </td> <td> <b>UPDATE</b> </td> <td> <b>DELETE</b> </td>
</tr>

<tr>

<td>
<pre>
{
  "before": null,
  "after": {
    "employee_id": 1001,
    "employee_name": "Alice",
    "employee_dept": "Packaging"
  }
  "op": "c"
}
</pre>
</td>

<td>
<pre>
{
  "before": null,
  "after": {
    "employee_id": 1001,
    "employee_name": "Bob",
    "employee_dept": "Packaging"
  }
  "op": "u"
}
</pre>
</td>

<td>
<pre>
{
  "before": {
    "employee_id": 1001
  },
  "after": null,
  "op": "d"
}
</pre>
</td>
</tr>

</table>

  {{% /tab %}}

  {{% tab header="FULL" lang="full" %}}

**yboutput plugin**

<table>
<tr>
<td> <b>INSERT</b> </td> <td> <b>UPDATE</b> </td> <td> <b>DELETE</b> </td>
</tr>

<tr>

<td>
<pre>
{
  "before": null,
  "after": {
    "employee_id": {
        "value": 1001,
        "set": true
    },
    "employee_name": {
      "value": "Alice",
      "set": true
    },
    "employee_dept": {
        "value": "Packaging",
        "set": true
    }
  }
  "op": "c"
}
</pre>
</td>

<td>
<pre>
{
  "before": {
    "employee_id": {
        "value": 1001,
        "set": true
    },
    "employee_name": {
      "value": "Alice",
      "set": true
    },
    "employee_dept": {
      "value": "Packaging",
      "set": true
    }
  },
  "after": {
    "employee_id": {
        "value": 1001,
        "set": true
    },
    "employee_name": {
      "value": "Bob",
      "set": true
    },
    "employee_dept": {
      "value": "Packaging",
      "set": true
    }
  }
  "op": "u"
}
</pre>
</td>

<td>
<pre>
{
  "before": {
    "employee_id": {
    "employee_id": {
        "value": 1001,
        "set": true
    },
    "employee_name": {
      "value": "Bob",
      "set": true
    },
    "employee_dept": {
      "value": "Packaging",
      "set": true
    }
  },
  "after": null,
  "op": "d"
}
</pre>
</td>
</tr>
</table>

**pgoutput plugin**

<table>
<tr>
<td> <b>INSERT</b> </td> <td> <b>UPDATE</b> </td> <td> <b>DELETE</b> </td>
</tr>

<tr>
<td>
<pre>
{
  "before": null,
  "after": {
    "employee_id": 1001,
    "employee_name": "Alice",
    "employee_dept": "Packaging",
    }
  }
  "op": "c"
}
</pre>
</td>

<td>
<pre>
{
  "before": {
    "employee_id": 1001,
    "employee_name": "Alice",
    "employee_dept": "Packaging"
  },
  "after": {
    "employee_id": 1001,
    "employee_name": "Bob",
    "employee_dept": "Packaging"
  }
  "op": "u"
}
</pre>
</td>

<td>
<pre>
{
  "before": {
    "employee_id": 1001,
    "employee_name": "Bob",
    "employee_dept": "Packaging"
  },
  "after": null,
  "op": "d"
}
</pre>
</td>
</tr>

</table>

  {{% /tab %}}

  {{% tab header="NOTHING" lang="nothing" %}}

**yboutput plugin**

<table>
<tr>
<td> <b>INSERT</b> </td>
</tr>

<tr>

<td>
<pre>
{
  "before": null,
  "after": {
    "employee_id": {
        "value": 1001,
        "set": true
    },
    "employee_name": {
      "value": "Alice",
      "set": true
    },
    "employee_dept": {
        "value": "Packaging",
        "set": true
    }
  }
  "op": "c"
}
</pre>
</td>
</tr>
</table>

**pgoutput plugin**

<table>
<tr>
<td> <b>INSERT</b> </td>
</tr>

<tr>
<td>
<pre>
{
  "before": null,
  "after": {
    "employee_id": 1001,
    "employee_name": "Alice",
    "employee_dept": "Packaging",
  }
  "op": "c"
}
</pre>
</td>
</tr>

</table>

  {{% /tab %}}

{{< /tabpane >}}

{{< note title="Note" >}}

If `UPDATE` and `DELETE` operations will be performed on a table in publication without any replica identity (that is, `REPLICA IDENTITY` set to `NOTHING`), then the operations will cause an error on the publisher. For more details, see [Publication](https://www.postgresql.org/docs/15/logical-replication-publication.html).

{{< /note >}}

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

The following table describes the create event value fields.

| Item | Field&nbsp;name | Description |
| :---- | :------ | :------------ |
| 1 | schema | The value's schema, which describes the structure of the value's payload. A change event's value schema is the same in every change event that the connector generates for a particular table. |
| 2 | name | In the schema section, each name field specifies the schema for a field in the value's payload.<br/><br/>`YugabyteDB_server.inventory.customers.Value` is the schema for the payload's _before_ and _after_ fields. This schema is specific to the customers table.<br/><br/>Names of schemas for _before_ and _after_ fields are of the form `logicalName.tableName.Value`, which ensures that the schema name is unique in the database. This means that when using the [Avro Converter](https://www.confluent.io/hub/confluentinc/kafka-connect-avro-converter), the resulting Avro schema for each table in each logical source has its own evolution and history. |
| 3 | name | `io.debezium.connector.postgresql.Source` is the schema for the payload's `source` field. This schema is specific to the YugabyteDB connector. The connector uses it for all events that it generates. |
| 4 | name | `YugabyteDB_server.inventory.customers.Envelope` is the schema for the overall structure of the payload, where `YugabyteDB_server` is the connector name, `public` is the schema, and `customers` is the table. |
| 5 | payload | The value's actual data. This is the information that the change event is providing.<br/><br/>It may appear that the JSON representations of the events are much larger than the rows they describe. This is because the JSON representation must include the schema and the payload portions of the message. However, by using the Avro converter, you can significantly decrease the size of the messages that the connector streams to Kafka topics. |
| 6 | before | An optional field that specifies the state of the row before the event occurred. When the op field is `c` for create, as it is in this example, the `before` field is `null` as this change event is for new content.<br/>{{< note title="Note" >}}Whether or not this field is available is dependent on the [REPLICA IDENTITY](#replica-identity) setting for each table.{{< /note >}} |
| 7 | after | An optional field that specifies the state of the row after the event occurred. In this example, the `after` field contains the values of the new row's `id`, `first_name`, `last_name`, and `email` columns. |
| 8 | source | Mandatory field that describes the source metadata for the event. This field contains information that you can use to compare this event with other events, with regard to the origin of the events, the order in which the events occurred, and whether events were part of the same transaction. The source metadata includes:<br/><ul><li>Debezium version</li><li>Connector type and name</li><li>Database and table that contains the new row</li><li>Stringified JSON array of additional offset information. The first value is always the last committed LSN, the second value is always the current LSN. Either value may be null.</li><li>Schema name</li><li>If the event was part of a snapshot</li><li>ID of the transaction in which the operation was performed</li><li>Offset of the operation in the database log</li><li>Timestamp for when the change was made in the database</li></ul> |
| 9 | op | Mandatory string that describes the type of operation that caused the connector to generate the event. In this example, `c` indicates that the operation created a row. Valid values are: <ul><li> `c` = create <li> `r` = read (applies to only snapshots) <li> `u` = update <li> `d` = delete</ul> |
| 10 | ts_ms | Optional field that displays the time at which the connector processed the event. The time is based on the system clock in the JVM running the Kafka Connect task.<br/><br/>In the `source` object, `ts_ms` indicates the time that the change was made in the database. By comparing the value for `payload.source.ts_ms` with the value for `payload.ts_ms`, you can determine the lag between the source database update and Debezium. |

### *update* events

The value of a change event for an update in the sample `customers` table has the same schema as a create event for that table. Likewise, the event value's payload has the same structure. However, the event value payload contains different values in an update event. The following is an example of a change event value in an event that the connector generates for an update in the `customers` table:

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

The following table describes the update event value fields.

| Item | Field&nbsp;name | Description |
| :---- | :------ | :------------ |
| 1 | before | An optional field that contains values that were in the row before the database commit. In this example, no previous value for any of the columns, is present because the table's [REPLICA IDENTITY](#replica-identity) setting is, `DEFAULT`. For an update event to contain the previous values of all columns in the row, you would have to change the `customers` table by running `ALTER TABLE customers REPLICA IDENTITY FULL`. |
| 2 | after | An optional field that specifies the state of the row after the event occurred. In this example, the `first_name` value is now `Anne Marie`. |
| 3 | source | Mandatory field that describes the source metadata for the event. The `source` field structure has the same fields as in a create event, but some values are different. The source metadata includes:<br/<br/> <ul><li>Debezium version</li><li>Connector type and name</li><li>Database and table that contains the new row</li><li>Schema name</li><li>If the event was part of a snapshot (always `false` for _update_ events)</li><li>ID of the transaction in which the operation was performed</li><li>Offset of the operation in the database log</li><li>Timestamp for when the change was made in the database</li></ul> |
| 4 | op | Mandatory string that describes the type of operation. In an update event value, the `op` field value is `u`, signifying that this row changed because of an update. |
| 5 | ts_ms | Optional field that displays the time at which the connector processed the event. The time is based on the system clock in the JVM running the Kafka Connect task.<br/><br/>In the `source` object, `ts_ms` indicates the time that the change was made in the database. By comparing the value for `payload.source.ts_ms` with the value for `payload.ts_ms`, you can determine the lag between the source database update and Debezium. |

{{< note title="Note" >}}

Updating the columns for a row's primary/unique key changes the value of the row's key. When a key changes, Debezium outputs three events: a `DELETE` event and a [tombstone event](#tombstone-events) with the old key for the row, followed by an event with the new key for the row. Details are in the next section.

{{< /note >}}

### Primary key updates

An `UPDATE` operation that changes a row's primary key field(s) is known as a primary key change. For a primary key change, in place of sending an `UPDATE` event record, the connector sends a `DELETE` event record for the old key and a `CREATE` event record for the new (updated) key.

### _delete_ events

The value in a _delete_ change event has the same `schema` portion as create and update events for the same table. The `payload` portion in a delete event for the sample `customers` table looks like this:

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

The following table describes the delete event value fields.

| Item | Field&nbsp;name | Description |
| :---- | :------ | :------------ |
| 1 | before | Optional field that specifies the state of the row before the event occurred. In a _delete_ event value, the `before` field contains the values that were in the row before it was deleted with the database commit.<br/><br/>In this example, the before field contains only the primary key column because the table's [REPLICA IDENTITY](#replica-identity) setting is `DEFAULT`. |
| 2 | after | Optional field that specifies the state of the row after the event occurred. In a delete event value, the `after` field is `null`, signifying that the row no longer exists. |
| 3 | source | Mandatory field that describes the source metadata for the event. In a delete event value, the source field structure is the same as for create and update events for the same table. Many source field values are also the same. In a delete event value, the `ts_ms` and `lsn` field values, as well as other values, might have changed. But the source field in a delete event value provides the same metadata:<br/><ul><li>Debezium version</li><li>Connector type and name</li><li>Database and table that contained the deleted row</li><li>Schema name</li><li>If the event was part of a snapshot (always false for delete events)</li><li>ID of the transaction in which the operation was performed</li><li>Offset of the operation in the database log</li><li>Timestamp for when the change was made in the database</li></ul> |
| 4 | op | Mandatory string that describes the type of operation. The `op` field value is `d`, signifying that this row was deleted. |
| 5 | ts_ms | Optional field that displays the time at which the connector processed the event. The time is based on the system clock in the JVM running the Kafka Connect task.<br/><br/>In the `source` object, `ts_ms` indicates the time that the change was made in the database. By comparing the value for `payload.source.ts_ms` with the value for `payload.ts_ms`, you can determine the lag between the source database update and Debezium. |

A _delete_ change event record provides a consumer with the information it needs to process the removal of this row.

YugabyteDB connector events are designed to work with [Kafka log compaction](https://kafka.apache.org/documentation#compaction). Log compaction enables removal of some older messages as long as at least the most recent message for every key is kept. This lets Kafka reclaim storage space while ensuring that the topic contains a complete data set and can be used for reloading key-based state.

#### Tombstone events

When a row is deleted, the _delete_ event value still works with log compaction, because Kafka can remove all earlier messages that have that same key. However, for Kafka to remove all messages that have that same key, the message value must be `null`. To make this possible, the YugabyteDB connector follows a _delete_ event with a special tombstone event that has the same key but a `null` value.

If the downstream consumer from the topic relies on tombstone events to process deletions and uses the [YBExtractNewRecordState transformer](../transformers/#ybextractnewrecordstate) (SMT), it is recommended to set the `delete.tombstone.handling.mode` SMT configuration property to `tombstone`. This ensures that the connector converts the delete records to tombstone events and drops the tombstone events.

To set the property, follow the SMT configuration conventions. For example:

```json
"transforms": "flatten",
"transforms.flatten.type": "io.debezium.connector.postgresql.transforms.yugabytedb.YBExtractNewRecordState",
"transforms.flatten.delete.tombstone.handling.mode": "tombstone"
```

### Updating or deleting a row inserted in the same transaction

If a row is updated or deleted in the same transaction in which it was inserted, CDC cannot retrieve the before-image values for the UPDATE / DELETE event. If the replica identity is not CHANGE, then CDC will throw an error while processing such events.

To handle such updates/deletes with a non-CHANGE replica identity, set the YB-TServer flag [cdc_send_null_before_image_if_not_exists](../../../../reference/configuration/yb-tserver/#cdc-send-null-before-image-if-not-exists) to true. With this flag enabled, CDC will send a null before-image instead of failing with an error.

<!-- YB Note skipping content for truncate and message events -->

## Data type mappings

The YugabyteDB connector represents changes to rows with events that are structured like the table in which the row exists. The event contains a field for each column value. How that value is represented in the event depends on the YugabyteDB data type of the column. The following sections describe how the connector maps YugabyteDB data types to a literal type and a semantic type in event fields.

* `literal` type describes how the value is literally represented using Kafka Connect schema types: `INT8`, `INT16`, `INT32`, `INT64`, `FLOAT32`, `FLOAT64`, `BOOLEAN`, `STRING`, `BYTES`, `ARRAY`, `MAP`, and `STRUCT`.
* `semantic` type describes how the Kafka Connect schema captures the meaning of the field using the name of the Kafka Connect schema for the field.

If the default data type conversions do not meet your needs, you can [create a custom converter](https://debezium.io/documentation/reference/2.5/development/converters.html#custom-converters) for the connector.

### Basic types

| YugabyteDB data type| Literal type (schema type) | Semantic type (schema name) and Notes |
| :------------------ | :------------------------- | :-------------------------- |
| `BOOLEAN` | `BOOLEAN` | N/A |
| `BIT(1)` | `BOOLEAN` | N/A |
| `BIT( > 1)` | `BYTES` | `io.debezium.data.Bits`<br/>The `length` schema parameter contains an integer that represents the number of bits. The resulting `byte[]` contains the bits in little-endian form and is sized to contain the specified number of bits. For example, `numBytes = n/8 + (n % 8 == 0 ? 0 : 1)` where `n` is the number of bits. |
| `BIT VARYING[(M)]` | `BYTES` | `io.debezium.data.Bits`<br/>The `length` schema parameter contains an integer that represents the number of bits (2^31 - 1 in case no length is given for the column). The resulting `byte[]` contains the bits in little-endian form and is sized based on the content. The specified size (`M`) is stored in the length parameter of the `io.debezium.data.Bits` type. |
| `SMALLINT`, `SMALLSERIAL` | `INT16` | N/A |
| `INTEGER`, `SERIAL` | `INT32` | N/A |
| `BIGINT`, `BIGSERIAL`, `OID` | `INT64` | N/A |
| `REAL` | `FLOAT32` | N/A |
| `DOUBLE PRECISION` | `FLOAT64` | N/A |
| `CHAR [(M)]` | `STRING` | N/A |
| `VARCHAR [(M)]` | `STRING` | N/A |
| `CHARACTER [(M)]` | `STRING` | N/A |
| `CHARACTER VARYING [(M)]` | `STRING` | N/A |
| `TIMESTAMPTZ`, `TIMESTAMP WITH TIME ZONE` | `STRING` | `io.debezium.time.ZonedTimestamp` <br/> A string representation of a timestamp with timezone information, where the timezone is GMT. |
| `TIMETZ`, `TIME WITH TIME ZONE` | `STRING` | `io.debezium.time.ZonedTime` <br/> A string representation of a time value with timezone information, where the timezone is GMT. |
| `INTERVAL [P]` | `INT64` | `io.debezium.time.MicroDuration` (default) <br/> The approximate number of microseconds for a time interval using the `365.25 / 12.0` formula for days per month average. |
| `INTERVAL [P]` | `STRING` | `io.debezium.time.Interval` <br/> (when `interval.handling.mode` is `string`) <br/> The string representation of the interval value that follows the pattern <br/> P\<years>Y\<months>M\<days>DT\<hours>H\<minutes>M\<seconds>S. <br/> For example, `P1Y2M3DT4H5M6.78S`. |
| `BYTEA` | `BYTES` or `STRING` | n/a<br/><br/>Either the raw bytes (the default), a base64-encoded string, or a base64-url-safe-encoded String, or a hex-encoded string, based on the connector's `binary handling mode` setting.<br/><br/>Debezium only supports Yugabyte `bytea_output` configuration of value `hex`. For more information about PostgreSQL binary data types, see the [Binary data types](../../../../api/ysql/datatypes/type_binary/). |
| `JSON`, `JSONB` | `STRING` | `io.debezium.data.Json` <br/> Contains the string representation of a JSON document, array, or scalar. |
| `UUID` | `STRING` | `io.debezium.data.Uuid` <br/> Contains the string representation of a YugabyteDB UUID value. |
| `INT4RANGE` | `STRING` | Range of integer. |
| `INT8RANGE` | `STRING` | Range of `bigint`. |
| `NUMRANGE` | `STRING` | Range of `numeric`. |
| `TSRANGE` | `STRING` | n/a<br/><br/>The string representation of a timestamp range without a time zone. |
| `TSTZRANGE` | `STRING` | n/a<br/><br/>The string representation of a timestamp range with the local system time zone. |
| `DATERANGE` | `STRING` | n/a<br/><br/>The string representation of a date range. Always has an _exclusive_ upper bound. |
| `ENUM` | `STRING` | `io.debezium.data.Enum`<br/><br/>Contains the string representation of the YugabyteDB `ENUM` value. The set of allowed values is maintained in the allowed schema parameter. |

### Temporal types

Other than YugabyteDB's `TIMESTAMPTZ` and `TIMETZ` data types, which contain time zone information, how temporal types are mapped depends on the value of the `time.precision.mode` connector configuration property. The following sections describe these mappings:

* `time.precision.mode=adaptive`
* `time.precision.mode=adaptive_time_microseconds`
* `time.precision.mode=connect`

#### time.precision.mode=adaptive

When the `time.precision.mode` property is set to `adaptive`, the default, the connector determines the literal type and semantic type based on the column's data type definition. This ensures that events _exactly_ represent the values in the database.

| YugabyteDB data type | Literal type (schema type) | Semantic type (schema name) and Notes |
| :----- | :----- | :----- |
| `DATE` | `INT32` | `io.debezium.time.Date`<br/>Represents the number of days since the epoch. |
| `TIME(1)`, `TIME(2)`, `TIME(3)` | `INT32` | `io.debezium.time.Time`<br/>Represents the number of milliseconds past midnight, and does not include timezone information. |
| `TIME(4)`, `TIME(5)`, `TIME(6)` | `INT64` | `io.debezium.time.MicroTime`<br/>Represents the number of microseconds past midnight, and does not include timezone information. |
| `TIMESTAMP(1)`, `TIMESTAMP(2)`, `TIMESTAMP(3)` | `INT64` | `io.debezium.time.Timestamp`<br/>Represents the number of milliseconds since the epoch, and does not include timezone information. |
| `TIMESTAMP(4)`, `TIMESTAMP(5)`, `TIMESTAMP(6)`, `TIMESTAMP` | `INT64` | `io.debezium.time.MicroTimestamp`<br/>Represents the number of microseconds since the epoch, and does not include timezone information. |

#### time.precision.mode=adaptive_time_microseconds

When the `time.precision.mode` configuration property is set to `adaptive_time_microseconds`, the connector determines the literal type and semantic type for temporal types based on the column's data type definition. This ensures that events _exactly_ represent the values in the database, except all `TIME` fields are captured as microseconds.

| YugabyteDB data type | Literal type (schema type) | Semantic type (schema name) and Notes |
| :----- | :----- | :----- |
| `DATE` | `INT32` | `io.debezium.time.Date`<br/>Represents the number of days since the epoch. |
| `TIME([P])` | `INT64` | `io.debezium.time.MicroTime`<br/>Represents the time value in microseconds and does not include timezone information. YugabyteDB allows precision `P` to be in the range 0-6 to store up to microsecond precision. |
| `TIMESTAMP(1)` , `TIMESTAMP(2)`, `TIMESTAMP(3)` | `INT64` | `io.debezium.time.Timestamp`<br/>Represents the number of milliseconds past the epoch, and does not include timezone information. |
| `TIMESTAMP(4)`, `TIMESTAMP(5)`, `TIMESTAMP(6)`, `TIMESTAMP` | `INT64` | `io.debezium.time.MicroTimestamp`<br/>Represents the number of microseconds past the epoch, and does not include timezone information. |

#### time.precision.mode=connect

When the `time.precision.mode` configuration property is set to `connect`, the connector uses Kafka Connect logical types. This may be useful when consumers can handle only the built-in Kafka Connect logical types and are unable to handle variable-precision time values. However, because YugabyteDB supports microsecond precision, the events generated by a connector with the connect time precision mode results in a loss of precision when the database column has a fractional second precision value that is greater than 3.

| YugabyteDB data type | Literal type (schema type) | Semantic type (schema name) and Notes |
| :----- | :----- | :----- |
| `DATE` | `INT32` | `org.apache.kafka.connect.data.Date`<br/>Represents the number of days since the epoch. |
| `TIME([P])` | `INT64` | `org.apache.kafka.connect.data.Time`<br/>Represents the number of milliseconds since midnight, and does not include timezone information. YugabyteDB allows `P` to be in the range 0-6 to store up to microsecond precision, though this mode results in a loss of precision when `P` is greater than 3. |
| `TIMESTAMP([P])` | `INT64` | `org.apache.kafka.connect.data.Timestamp`<br/>Represents the number of milliseconds since the epoch, and does not include timezone information. YugabyteDB allows `P` to be in the range 0-6 to store up to microsecond precision, though this mode results in a loss of precision when `P` is greater than 3. |

### TIMESTAMP type

The `TIMESTAMP` type represents a timestamp without time zone information. Such columns are converted into an equivalent Kafka Connect value based on UTC. For example, the `TIMESTAMP` value "2018-06-20 15:13:16.945104" is represented by an `io.debezium.time.MicroTimestamp` with the value "1529507596945104" when `time.precision.mode` is not set to `connect`.

The timezone of the JVM running Kafka Connect and Debezium does not affect this conversion.

YugabyteDB supports using +/-infinite values in `TIMESTAMP` columns. These special values are converted to timestamps with value `9223372036825200000` in case of positive infinity or `-9223372036832400000` in case of negative infinity. This behavior mimics the standard behavior of the YugabyteDB JDBC driver. For reference, see the [`org.postgresql.PGStatement`](https://jdbc.postgresql.org/documentation/publicapi/org/postgresql/PGStatement.html) interface.

### Decimal types

The setting of the YugabyteDB connector configuration property `decimal.handling.mode` determines how the connector maps decimal types.

#### decimal.handling.mode=double

When the `decimal.handling.mode` property is set to `double`, the connector represents all `DECIMAL`, `NUMERIC` and `MONEY` values as Java double values and encodes them as shown in the following table.

| YugabyteDB data type | Literal type (schema type) | Semantic type (schema name) and Notes |
| :----- | :----- | :----- |
| `NUMERIC[(M[,D])]` | `FLOAT64` | |
| `DECIMAL[(M[,D])]` | `FLOAT64` | |
| `MONEY[(M[,D])]` | `FLOAT64` | |

#### decimal.handling.mode=string

The last possible setting for the `decimal.handling.mode` configuration property is `string`. In this case, the connector represents `DECIMAL`, `NUMERIC` and `MONEY` values as their formatted string representation, and encodes them as shown in the following table.

| YugabyteDB data type | Literal type (schema type) | Semantic type (schema name) and Notes |
| :----- | :----- | :----- |
| `NUMERIC[(M[,D])]` | `STRING` | |
| `DECIMAL[(M[,D])]` | `STRING` | |
| `MONEY[(M[,D])]` | `STRING` | |

{{< note title="Note" >}}

Decimal handling mode `precise` is not yet supported by `YugabyteDBConnector`.

{{< /note >}}

### HSTORE types

The setting of the YugabyteDB connector configuration property `hstore.handling.mode` determines how the connector maps `HSTORE` values.

When the `hstore.handling.mode` property is set to json (the default), the connector represents `HSTORE` values as string representations of `JSON` values and encodes them as shown in the following table. When the `hstore.handling.mode` property is set to map, the connector uses the `MAP` schema type for `HSTORE` values.

| YugabyteDB data type | Literal type (schema type) | Semantic type (schema name) and Notes |
| :----- | :----- | :----- |
| `HSTORE` | `STRING` | `io.debezium.data.Json`<br/><br/>Example: output representation using the JSON converter is `{"key" : "val"}` |
| `HSTORE` | `MAP` | n/a<br/><br/>Example: output representation using the `JSON` converter is `{"key" : "val"}` |

### Domain types

YugabyteDB supports user-defined types that are based on other underlying types. When such column types are used, Debezium exposes the column's representation based on the full type hierarchy.

{{< note title="Note" >}}

Capturing changes in columns that use YugabyteDB domain types requires special consideration. When a column is defined to contain a domain type that extends one of the default database types and the domain type defines a custom length or scale, the generated schema inherits that defined length or scale.

When a column is defined to contain a domain type that extends another domain type that defines a custom length or scale, the generated schema does not inherit the defined length or scale because that information is not available in the YugabyteDB driver's column metadata.

{{< /note >}}

### Network address types

YugabyteDB has data types that can store IPv4, IPv6, and MAC addresses. It is better to use these types instead of plain text types to store network addresses. Network address types offer input error checking and specialized operators and functions.

| YugabyteDB data type | Literal type (schema type) | Semantic type (schema name) and Notes |
| :----- | :----- | :----- |
| `INET` | `STRING` | n/a<br/><br/> IPv4 and IPv6 networks |
| `CIDR` | `STRING` | n/a<br/><br/>IPv4 and IPv6 hosts and networks |
| `MACADDR` | `STRING` | n/a<br/><br/>MAC addresses |
| `MACADDR8` | `STRING` | n/a<br/><br/>MAC addresses in EUI-64 format |

<!-- todo Vaibhav: this default section needs verification -->
<!-- ### Default values

If a default value is specified for a column in the database schema, the YugabyteDB connector will attempt to propagate this value to the Kafka schema whenever possible. Most common data types are supported, including:
* `BOOLEAN`
* Numeric types (`INT`, `FLOAT`, `NUMERIC`, etc.)
* Text types (`CHAR`, `VARCHAR`, `TEXT`, etc.)
* Temporal types (`DATE`, `TIME`, `INTERVAL`, `TIMESTAMP`, `TIMESTAMPTZ`)
* `JSON`, `JSONB`
* `UUID`

Note that for temporal types, parsing of the default value is provided by YugabyteDB libraries; therefore, any string representation which is normally supported by YugabyteDB should also be supported by the connector.

In the case that the default value is generated by a function rather than being directly specified in-line, the connector will instead export the equivalent of `0` for the given data type. These values include:
* `FALSE` for `BOOLEAN`
* `0` with appropriate precision, for numeric types
* Empty string for text types
* `{}` for `JSON` types
* `1970-01-01` for `DATE`, `TIMESTAMP`, `TIMESTAMPTZ` types
* `00:00` for `TIME`
* `EPOCH` for `INTERVAL`
* `0000000-0000-0000-0000-000000000000` for `UUID`

This support currently extends only to explicit usage of functions. For example, `CURRENT_TIMESTAMP(6)` is supported with parentheses, but `CURRENT_TIMESTAMP` is not.

{{<warning title="Warning">}}

Support for the propagation of default values exists primarily to allow for safe schema evolution when using the YugabyteDB connector with a schema registry which enforces compatibility between schema versions. Due to this primary concern, as well as the refresh behaviours of the different plugins, the default value present in the Kafka schema is not guaranteed to always be in-sync with the default value in the database schema.
* Default values may appear 'late' in the Kafka schema, depending on when/how a given plugin triggers refresh of the in-memory schema. Values may never appear/be skipped in the Kafka schema if the default changes multiple times in-between refreshes
* Default values may appear 'early' in the Kafka schema, if a schema refresh is triggered while the connector has records waiting to be processed. This is due to the column metadata being read from the database at refresh time, rather than being present in the replication message. This may occur if the connector is behind and a refresh occurs, or on connector start if the connector was stopped for a time while updates continued to be written to the source database.

This behaviour may be unexpected, but it is still safe. Only the schema definition is affected, while the real values present in the message will remain consistent with what was written to the source database.

{{< /warning >}} -->

## Setting up YugabyteDB

### Setting up permissions

Setting up a YugabyteDB server to run the connector requires a database user that can perform replications. Replication can be performed only by a database user that has appropriate permissions and only for a configured number of hosts.

Although, by default, superusers have the necessary `REPLICATION` and `LOGIN` attributes, as mentioned in [Security](#security), it is best not to provide the replication user with elevated privileges. Instead, create a Debezium user that has the minimum required privileges.

**Prerequisites:**

* YugabyteDB administrative permissions.

**Procedure:**

To provide a user with replication permissions, define a YugabyteDB role that has at least the `REPLICATION` and `LOGIN` attributes. For example:

```sql
CREATE ROLE <name> REPLICATION LOGIN;
```

{{< tip title="REPLICATION is non-inheritable" >}}

Like other PostgreSQL role attributes, REPLICATION is not inheritable. Being a member of a role with REPLICATION will not allow the member to connect to the server in replication mode, even if the membership grant has the INHERIT attribute. You must actually [SET ROLE](../../../../api/ysql/the-sql-language/statements/dcl_set_role/) to a specific role having the REPLICATION attribute in order to make use of the attribute.

{{< /tip >}}

### Setting privileges to enable the connector to create YugabyteDB publications when you use `pgoutput` or `yboutput`

If you use `pgoutput` or `yboutput` as the logical decoding plugin, the connector must operate in the database as a user with specific privileges.

The connector streams change events for YugabyteDB source tables from publications that are created for the tables. Publications contain a filtered set of change events that are generated from one or more tables. The data in each publication is filtered based on the publication specification. The specification can be created by the `YugabyteDB` database administrator or by the connector. To permit the connector to create publications and specify the data to replicate to them, the connector must operate with specific privileges in the database.

There are several options for determining how publications are created. In general, it is best to manually create publications for the tables that you want to capture, before you set up the connector. However, you can configure your environment in a way that permits the connector to create publications automatically, and to specify the data that is added to them.

Debezium uses include list and exclude list properties to specify how data is inserted in the publication. For more information about the options for enabling the connector to create publications, see `publication.autocreate.mode`.

For the connector to create a YugabyteDB publication, it must run as a user that has the following privileges:

* Replication privileges in the database to add the table to a publication.
* `CREATE` privileges on the database to add publications.
* `SELECT` privileges on the tables to copy the initial table data. Table owners automatically have `SELECT` permission for the table.

To add tables to a publication, the user must be an owner of the table. But because the source table already exists, you need a mechanism to share ownership with the original owner. To enable shared ownership, create a YugabyteDB replication group, then add the existing table owner and the replication user to the group.

Procedure

1. Create a replication group.

   ```sql
   CREATE ROLE <replication_group>;
   ```

2. Add the original owner of the table to the group.

   ```sql
   GRANT REPLICATION_GROUP TO <original_owner>;
   ```

3. Add the Debezium replication user to the group.

   ```sql
   GRANT REPLICATION_GROUP TO <replication_user>;
   ```

4. Transfer ownership of the table to `<replication_group>`.

   ```sql
   ALTER TABLE <table_name> OWNER TO REPLICATION_GROUP;
   ```

For Debezium to specify the capture configuration, the value of `publication.autocreate.mode` must be set to `filtered`.

### Configuring YugabyteDB to allow replication with the connector host

To enable Debezium to replicate YugabyteDB data, you must configure the database to permit replication with the host that runs the YugabyteDB connector. To specify the clients that are permitted to replicate with the database, add entries to the YugabyteDB host-based authentication file, `ysql_hba.conf`. For more information about the pg_hba.conf file, see the [YugabyteDB documentation](../../../../secure/authentication/host-based-authentication/#ysql-hba-conf-file).

Procedure

* Add entries to the `ysql_hba.conf` file to specify the connector hosts that can replicate with the database host. For example,

```sh
--ysql_hba_conf_csv="local replication <yourUser> trust, local replication <yourUser> 127.0.0.1/32 trust, host replication <yourUser> ::1/128 trust"
```

### Supported YugabyteDB topologies

As mentioned in the beginning, YugabyteDB (for all versions > 2024.1.1) supports logical replication slots. The YugabyteDB connector can communicate with the server by connecting to any node using the [YugabyteDB Java driver](/stable/develop/drivers-orms/java/yugabyte-jdbc-reference/). Should any node fail, the connector receives an error and restarts. Upon restart, the connector connects to any available node and continues streaming from that node.

### Setting up multiple connectors for same database server

Debezium uses [replication slots](https://www.postgresql.org/docs/15/logicaldecoding-explanation.html#LOGICALDECODING-REPLICATION-SLOTS) to stream changes from a database. These replication slots maintain the current position in form of a LSN. This helps YugabyteDB keep the WAL available until it is processed by Debezium. A single replication slot can exist only for a single consumer or process - as different consumer might have different state and may need data from different position.

Because a replication slot can only be used by a single connector, it is essential to create a unique replication slot for each connector. Although when a connector is not active, YugabyteDB may allow other connectors to consume the replication slot - which could be dangerous as it may lead to data loss as a slot will emit each change just once.

In addition to replication slot, the connector uses publication to stream events when using the `pgoutput`or `yboutput` plugin. Similar to replication slot, publication is at database level and is defined for a set of tables. Thus, you'll need a unique publication for each connector, unless the connectors work on same set of tables. For more information about the options for enabling the connector to create publications, see `publication.autocreate.mode`.

See `slot.name` and `publication.name` on how to set a unique replication slot name and publication name for each connector.

## Deployment

To deploy the connector, you install the connector archive, configure the connector, and start the connector by adding its configuration to Kafka Connect.

**Prerequisites**

* [Zookeeper](https://zookeeper.apache.org/), [Kafka](http://kafka.apache.org/), and [Kafka Connect](https://kafka.apache.org/documentation.html#connect) are installed.
* YugabyteDB is installed and is [set up to run the connector](#setting-up-yugabytedb).

**Procedure**

1. Download the latest [YugabyteDB connector plugin archive](https://github.com/yugabyte/debezium/releases/).
2. Extract the files into your Kafka Connect environment.
3. Add the directory with the JAR files to the [Kafka Connect `plugin.path`](https://kafka.apache.org/documentation/#connectconfigs).
4. Restart your Kafka Connect process to pick up the new JAR files.

{{< warning title="Warning" >}}

On connector version dz.2.5.2.yb.2025.2, users may face the following error while deploying the connector:

`ERROR: cannot export or import snapshot when ysql_enable_pg_export_snapshot is disabled.`

Use connector version dz.2.5.2.yb.2025.2.2 or dz.2.5.2.yb.2025.1.2 and below versions instead.

{{< /warning >}}

### Creating Kafka topics

If [auto creation of topics](https://debezium.io/documentation/reference/2.5/configuration/topic-auto-create-config.html) is not enabled in the Kafka Connect cluster then you will need to create the following topics manually:

* Topic for each table in the format `<topic.prefix>.<schemaName>.<tableName>`
* Heartbeat topic in the format `<topic.heartbeat.prefix>.<topic.prefix>`. The [topic.heartbeat.prefix](../yugabytedb-connector-properties/#topic-heartbeat-prefix) has a default value of `__debezium-heartbeat`.

### Connector configuration example

Following is an example of the configuration for a YugabyteDB connector that connects to a YugabyteDB server on port `5433` at `192.168.99.100`, whose topic prefix is `fulfillment`. Typically, you configure the YugabyteDB connector in a JSON file by setting the configuration properties available for the connector.

You can choose to produce events for a subset of the schemas and tables in a database. Optionally, you can ignore, mask, or truncate columns that contain sensitive data, are larger than a specified size, or that you do not need.

```output.json
{
  "name": "fulfillment-connector",  --> 1
  "config": {
    "connector.class": "io.debezium.connector.postgresql.YugabyteDBConnector", --> 2
    "database.hostname": "192.168.99.100:5433,192.168.1.10:5433,192.168.1.68:5433", --> 3
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
3. The addresses of the YugabyteDB YB-TServer nodes. This can take a value of multiple addresses in the format `IP1:PORT1,IP2:PORT2,IP3:PORT3`.
4. The port number of the YugabyteDB server.
5. The name of the YugabyteDB user that has the [required privileges](#setting-up-yugabytedb).
6. The password for the YugabyteDB user that has the [required privileges](#setting-up-yugabytedb).
7. The name of the YugabyteDB database to connect to
8. The topic prefix for the YugabyteDB server/cluster, which forms a namespace and is used in all the names of the Kafka topics to which the connector writes, the Kafka Connect schema names, and the namespaces of the corresponding Avro schema when the Avro converter is used.
9. A list of all tables hosted by this server that this connector will monitor. This is optional, and there are other properties for listing the schemas and tables to include or exclude from monitoring.

See the [complete list of YugabyteDB connector properties](../yugabytedb-connector-properties/) that can be specified in these configurations.

You can send this configuration with a `POST` command to a running Kafka Connect service. The service records the configuration and starts one connector task that performs the following actions:

* Connects to the YugabyteDB database.
* Reads the transaction log.
* Streams change event records to Kafka topics.

### Adding connector configuration

To run the connector, create a connector configuration and add the configuration to your Kafka Connect cluster.

**Prerequisites**

* [YugabyteDB is configured to support logical replication.](#setting-up-yugabytedb)
* The YugabyteDB connector is installed.

**Procedure**

1. Create a configuration for the YugabyteDB connector.
2. Use the [Kafka Connect REST API](https://kafka.apache.org/documentation/#connect_rest) to add that connector configuration to your Kafka Connect cluster.

#### Results

After the connector starts, it performs a consistent snapshot of the YugabyteDB server databases that the connector is configured for. The connector then starts generating data change events for row-level operations and streaming change event records to Kafka topics.

## Monitoring

The YugabyteDB connector provides two metrics in addition to the built-in support for JMX metrics that Zookeeper, Kafka, and Kafka Connect provide:

* [Snapshot metrics](#snapshot-metrics) provide information about connector operation while performing a snapshot.
* [Streaming metrics](#streaming-metrics) provide information about connector operation when the connector is capturing changes and streaming change event records.

[Debezium monitoring documentation](https://debezium.io/documentation/reference/2.5/operations/monitoring.html#monitoring-debezium) provides details for how to expose these metrics by using JMX.

### Snapshot metrics

The **MBean** is `debezium.postgres:type=connector-metrics,context=snapshot,server=<topic.prefix>`.

Snapshot metrics are not exposed unless a snapshot operation is active, or if a snapshot has occurred since the last connector start.

The following table lists the snapshot metrics that are available.

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
| `MilliSecondsBehindSource` | long | The number of milliseconds between the last change event's timestamp and the connector processing it. The values will incorporate any differences between the clocks on the machines where the database server and the connector are running. |
| `NumberOfCommittedTransactions` | long | The number of processed transactions that were committed. |
| `SourceEventPosition` | Map<String, String> | The coordinates of the last received event. |
| `LastTransactionId` | string | Transaction identifier of the last processed transaction. |
| `MaxQueueSizeInBytes` | long | The maximum buffer of the queue in bytes. This metric is available if `max.queue.size.in.bytes` is set to a positive long value. |
| `CurrentQueueSizeInBytes` | long | The current volume, in bytes, of records in the queue. |

## Advanced

### Parallel streaming

{{<tags/feature/tp idea="1549">}} YugabyteDB also supports parallel streaming of a single table using logical replication. This means that you can start the replication for the table using parallel tasks, where each task polls on specific tablets.

To enable the feature, set the `ysql_enable_pg_export_snapshot` and `ysql_yb_enable_consistent_replication_from_hash_range` flags to true.

Use the following steps to configure parallel streaming using the YugabyteDB Connector.

#### Step 1: Decide on the number of tasks

This is important, as you need to create the same number of replication slots and publications. Note that the number of tasks cannot be greater than the number of tablets you have in the table to be streamed.

For example, if you have a table `test` with 3 tablets, you will create 3 tasks.

#### Step 2: Create publication and replication slots

If you are creating a slot and publication yourself, ensure that a publication is created before you create the replication slot.

If you do not want to create the publication and slots, decide on names so that the connector can create the publication and slots.

```sql
CREATE PUBLICATION pb FOR TABLE test;
CREATE PUBLICATION pb2 FOR TABLE test;
CREATE PUBLICATION pb3 FOR TABLE test;

CREATE_REPLICATION_SLOT rs LOGICAL yboutput;
CREATE_REPLICATION_SLOT rs2 LOGICAL yboutput;
CREATE_REPLICATION_SLOT rs3 LOGICAL yboutput;
```

#### Step 3: Get hash ranges

Execute the following query in YSQL for a `table_name` and number of tasks to get the ranges. Replace `num_ranges` and `table_name` as appropriate.

```sql
WITH params AS (
  SELECT
    num_ranges::int AS num_ranges,
    'table_name'::text AS table_name
),
yb_local_tablets_cte AS (
  SELECT *,
    COALESCE(('x' || encode(partition_key_start, 'hex'))::BIT(16)::INT, 0) AS partition_key_start_int,
    COALESCE(('x' || encode(partition_key_end, 'hex'))::BIT(16)::INT, 65536) AS partition_key_end_int
  FROM yb_local_tablets
  WHERE table_name = (SELECT table_name FROM params)
),

grouped AS (
  SELECT
    yt.*,
    NTILE((SELECT num_ranges FROM params)) OVER (ORDER BY partition_key_start_int) AS bucket_num
  FROM yb_local_tablets_cte yt
),

buckets AS (
  SELECT
    bucket_num,
    MIN(partition_key_start_int) AS bucket_start,
    MAX(partition_key_end_int) AS bucket_end
  FROM grouped
  GROUP BY bucket_num
),
distinct_ranges AS (
  SELECT DISTINCT
    b.bucket_start,
    b.bucket_start || ',' || b.bucket_end AS partition_range
  FROM grouped g
  JOIN buckets b ON g.bucket_num = b.bucket_num
)
SELECT STRING_AGG(partition_range, ';' ORDER BY bucket_start) AS concatenated_ranges
FROM distinct_ranges;
```

The output is in a format that can be added as ranges in the connector configuration:

```output
       concatenated_ranges
---------------------------------
 0,21845;21845,43690;43690,65536
```

Copy the output as you will need it later on.

#### Step 4: Build connector configuration

Using the output from the preceding step, add the following additional configuration properties to the connector and deploy it:

```json
{
  ...
  "streaming.mode":"parallel",
  "slot.names":"rs,rs2,rs3",
  "publication.names":"pb,pb2,pb3",
  "slot.ranges":"0,21845;21845,43690;43690,65536"
  ...
}
```

If you have to take the snapshot, you'll need to add 2 other configuration properties:

```json
{
  ...
  "snapshot.mode":"initial",
  "primary.key.hash.columns":"id"
  ...
}
```

For information on parallel streaming configuration properties, refer to [Advanced connector properties](../yugabytedb-connector-properties/#streaming-mode).

{{< warning title="Warning" >}}

The order of slot names, publication names, and slot ranges is important as the assignment of ranges to slots is sequential, and you want the same range assigned to the same slot across restarts.

The configuration for the connector shouldn't change on restart.

{{< /warning >}}

{{< note title="Important" >}}

Adding the configuration value for `primary.key.hash.columns` is important, as you will need the columns that form the hash part of the primary key. The connector relies on the column names to figure out the appropriate range each task should be polling.

{{< /note >}}

## Behavior when things go wrong

Debezium is a distributed system that captures all changes in multiple upstream databases; it never misses or loses an event. When the system is operating normally or being managed carefully then Debezium provides _exactly once_ delivery of every change event record. If a fault does happen then the system does not lose any events. However, while it is recovering from the fault, it's possible that the connector might emit some duplicate change events. In these abnormal situations, Debezium, like Kafka, provides _at least once_ delivery of change events.

The rest of this section describes how Debezium handles various kinds of faults and problems.

### Configuration and startup errors

In the following situations, the connector fails when trying to start, reports an error/exception in the log, and stops running:

* The connector's configuration is invalid.
* The connector cannot successfully connect to YugabyteDB by using the specified connection parameters.
* The connector is restarting from a previously-recorded LSN and YugabyteDB no longer has that history available.

In these cases, the error message has details about the problem and possibly a suggested workaround. After you correct the configuration or address the YugabyteDB problem, restart the connector.

### YB-TServer becomes unavailable

When the connector is running, the YB-TServer that it is connected to could become unavailable for any number of reasons. If this happens, the connector fails with an error and retries to connect to the YugabyteDB server. Because the connector uses the [YugabyteDB Java driver](/stable/develop/drivers-orms/java/), the connection is handled internally and the connector restores the connection to another running node.

The YugabyteDB connector externally stores the last processed offset in the form of a YugabyteDB LSN. After a connector restarts and connects to a server instance, the connector communicates with the server to continue streaming from that particular offset. This offset is available as long as the Debezium replication slot remains intact.

{{< warning title="Warning" >}}

Never drop a replication slot on the server or you will lose data.

{{< /warning >}}

### Cluster failures

When the connector is running, it is possible that the YugabyteDB server becomes unavailable for any number of reasons. If that happens, the connector fails with and error and initiates retries but as the complete YugabyteDB server is unavailable, all the retries will fail.

When the YugabyteDB server is back up, restart the connector to continue streaming where it left off.

### Kafka Connect process stops gracefully

Suppose that Kafka Connect is being run in distributed mode and a Kafka Connect process is stopped gracefully. Prior to shutting down that process, Kafka Connect migrates the process's connector tasks to another Kafka Connect process in that group. The new connector tasks start processing exactly where the prior tasks stopped. There is a short delay in processing while the connector tasks are stopped gracefully and restarted on the new processes.

### Kafka Connect process crashes

If the Kafka Connector process stops unexpectedly, any connector tasks it was running terminate without recording their most recently processed offsets. When Kafka Connect is being run in distributed mode, Kafka Connect restarts those connector tasks on other processes. However, YugabyteDB connectors resume from the last offset that was recorded by the earlier processes. This means that the new replacement tasks might generate some of the same change events that were processed just prior to the crash. The number of duplicate events depends on the offset flush period and the volume of data changes just before the crash.

Because there is a chance that some events might be duplicated during a recovery from failure, consumers should always anticipate some duplicate events. Debezium changes are idempotent, so a sequence of events always results in the same state.

In each change event record, Debezium connectors insert source-specific information about the origin of the event, including the YugabyteDB server's time of the event, the ID of the server transaction, and the position in the write-ahead log where the transaction changes were written. Consumers can keep track of this information, especially the LSN, to determine whether an event is a duplicate.

### Kafka becomes unavailable

As the connector generates change events, the Kafka Connect framework records those events in Kafka by using the Kafka producer API. Periodically, at a frequency that you specify in the Kafka Connect configuration, Kafka Connect records the latest offset that appears in those change events. If the Kafka brokers become unavailable, the Kafka Connect process that is running the connectors repeatedly tries to reconnect to the Kafka brokers. In other words, the connector tasks pause until a connection can be re-established, at which point the connectors resume exactly where they left off.

### Connector is stopped for a duration

If the connector is gracefully stopped, the database can continue to be used. Any changes are recorded in the YugabyteDB WAL. When the connector restarts, it resumes streaming changes where it left off. That is, it generates change event records for all database changes that were made while the connector was stopped.

A properly configured Kafka cluster is able to handle massive throughput. Kafka Connect is written according to Kafka best practices, and given enough resources a Kafka Connect connector can also handle very large numbers of database change events. Because of this, after being stopped for a while, when a Debezium connector restarts, it is very likely to catch up with the database changes that were made while it was stopped. How quickly this happens depends on the capabilities and performance of Kafka and the volume of changes being made to the data in YugabyteDB.
