---
title: Get started with CDC in YugabyteDB
headerTitle: Get started
linkTitle: Get started
description: Get started with Change Data Capture in YugabyteDB.
headcontent: Get set up for using CDC in YugabyteDB
menu:
  stable:
    parent: explore-change-data-capture-grpc-replication
    identifier: cdc-get-started
    weight: 10
type: docs
---

## Set up YugabyteDB for CDC

The following steps are necessary to set up YugabyteDB for use with the YugabyteDB gRPC connector:

- Create a DB stream ID.

    Before you use the YugabyteDB connector to retrieve data change events from YugabyteDB, create a stream ID using the yb-admin CLI command. Refer to the [yb-admin](../../../../admin/yb-admin/#change-data-capture-cdc-commands) CDC command reference documentation for more details.

- Make sure the YB-Master and YB-TServer ports are open.

    The connector connects to the YB-Master and YB-TServer processes running on the YugabyteDB server. Make sure the ports on which these processes are running are open. The [default ports](../../../../reference/configuration/default-ports/) on which the processes run are `7100` and `9100` respectively.

- Monitor available disk space.

    The change records for CDC are read from the WAL. YugabyteDB CDC maintains checkpoints internally for each DB stream ID and garbage collects the WAL entries if those have been streamed to the CDC clients.

    In case CDC is lagging or away for some time, the disk usage may grow and cause YugabyteDB cluster instability. To avoid this scenario, if a stream is inactive for a configured amount of time, the WAL is garbage collected. This is configurable using a [YB-TServer flag](../../../../reference/configuration/yb-tserver/#change-data-capture-cdc-flags).

## Deploying the YugabyteDB gRPC Connector

To stream data change events from YugabyteDB databases, follow these steps to deploy the YugabyteDB gRPC Connector:

- Download the Connector: You can download the connector from the [GitHub releases](https://github.com/yugabyte/debezium-connector-yugabytedb/releases)
- Install the Connector: Extract and install the connector archive in your Kafka Connect environment.
- Configure the Connector: Modify the connector configuration to suit your specific requirements.
- Start the Connector: Add the connector's configuration to Kafka Connect and start the connector.

For more details on connector configuration and deployment steps, refer to the [YugabyteDB gRPC Connector documentation](../debezium-connector-yugabytedb/).

## Serialization

{{< tabpane text=true >}}

  {{% tab header="Avro" lang="avro" %}}

The YugabyteDB source connector also supports AVRO serialization with schema registry. To use AVRO serialization, add the following configuration to your connector:

```json
{
  ...
  "key.converter":"io.confluent.connect.avro.AvroConverter",
  "key.converter.schema.registry.url":"http://host-url-for-schema-registry:8081",
  "value.converter":"io.confluent.connect.avro.AvroConverter",
  "value.converter.schema.registry.url":"http://host-url-for-schema-registry:8081"
  ...
}
```

  {{% /tab %}}

  {{% tab header="JSON" lang="json" %}}

For JSON schema serialization, you can use the [Kafka JSON Serializer](https://mvnrepository.com/artifact/io.confluent/kafka-json-serializer) and equivalent de-serializer. After downloading and including the required `JAR` file in the Kafka-Connect environment, you can directly configure the CDC source and sink connectors to use this converter.

For source connectors:

```json
{
  ...
  "value.serializer":"io.confluent.kafka.serializers.KafkaJsonSerializer",
  ...
}
```

For sink connectors:

```json
{
  ...
  "value.deserializer":"io.confluent.kafka.serializers.KafkaJsonDeserializer",
  ...
}
```

  {{% /tab %}}

  {{% tab header="Protobuf" lang="protobuf" %}}

To use the [protobuf](http://protobuf.dev) format for the serialization/de-serialization of the Kafka messages, you can use the [Protobuf Converter](https://www.confluent.io/hub/confluentinc/kafka-connect-protobuf-converter). After downloading and including the required `JAR` files in the Kafka-Connect environment, you can directly configure the CDC source and sink connectors to use this converter.

```json
{
  ...,
  config: {
    ...,
     "key.converter": "io.confluent.connect.protobuf.ProtobufConverter",
     "value.converter": "io.confluent.connect.protobuf.ProtobufConverter"
  }
}
```

  {{% /tab %}}

{{< /tabpane >}}

## Before image

Before image refers to the state of the row _before_ the change event occurred. The YugabyteDB connector sends the before image of the row when it will be configured using a stream ID enabled with before image. It is populated for UPDATE and DELETE events. For INSERT events, before image doesn't make sense as the change record itself is in the context of new row insertion.

Yugabyte uses multi-version concurrency control (MVCC) mechanism, and compacts data at regular intervals. The compaction or the history retention is controlled by the [history retention interval flag](../../../../reference/configuration/yb-tserver/#timestamp-history-retention-interval-sec). However, when before image is enabled for a database, YugabyteDB adjusts the history retention for that database based on the most lagging active CDC stream so that the previous row state is retained, and available. Consequently, in the case of a lagging CDC stream, the amount of space required for the database grows as more data is retained. On the other hand, older rows that are not needed for any of the active CDC streams are identified and garbage collected.

Schema version that is currently being used by a CDC stream will be used to frame before and current row images. The before image functionality is disabled by default unless it is specifically turned on during the CDC stream creation. The [yb-admin](../../../../admin/yb-admin/#enabling-before-image) `create_change_data_stream` command can be used to create a CDC stream with before image enabled.

{{< tip title="Use transformers" >}}

Add a transformer in the source connector while using with before image; you can add the following property directly to your configuration:

```properties
...
"transforms":"unwrap,extract",
"transforms.unwrap.type":"io.debezium.connector.yugabytedb.transforms.PGCompatible",
"transforms.unwrap.drop.tombstones":"false",
"transforms.extract.type":"io.debezium.transforms.ExtractNewRecordState",
"transforms.extract.drop.tombstones":"false",
...
```

{{< /tip >}}

After you've enabled before image and are using the suggested transformers, the effect of an update statement with the record structure is as follows:

```sql
UPDATE customers SET email = 'service@example.com' WHERE id = 1;
```

```output.json {hl_lines=[4,9,14,28]}
{
  "schema": {...},
  "payload": {
    "before": { --> 1
      "id": 1,
      "name": "Vaibhav Kushwaha",
      "email": "vaibhav@example.com"
    }
    "after": { --> 2
      "id": 1,
      "name": "Vaibhav Kushwaha",
      "email": "service@example.com"
    },
    "source": { --> 3
      "version": "1.9.5.y.11",
      "connector": "yugabytedb",
      "name": "dbserver1",
      "ts_ms": -8881476960074,
      "snapshot": "false",
      "db": "yugabyte",
      "sequence": "[null,\"1:5::0:0\"]",
      "schema": "public",
      "table": "customers",
      "txId": "",
      "lsn": "1:5::0:0",
      "xmin": null
    },
    "op": "u", --> 4
    "ts_ms": 1646149134341,
    "transaction": null
  }
}
```

The highlighted fields in the update event are:

| Item | Field name | Description |
| :--- | :--------- | :---------- |
| 1 | before | The value of the row before the update operation. |
| 2 | after | Specifies the state of the row after the change event occurred. In this example, the value of `email` has changed to `service@example.com`. |
| 3 | source | Mandatory field that describes the source metadata for the event. This has the same fields as a create event, but some values are different. The source metadata includes: <ul><li> Debezium version <li> Connector type and name <li> Database and table that contains the new row <li> Schema name <li> If the event was part of a snapshot (always `false` for update events) <li> ID of the transaction in which the operation was performed <li> Offset of the operation in the database log <li> Timestamp for when the change was made in the database </ul> |
| 4 | op | In an update event, this field's value is `u`, signifying that this row changed because of an update. |

### Before image modes

YugabyteDB supports the following record types in the context of before image:

- ALL
- FULL_ROW_NEW_IMAGE
- MODIFIED_COLUMNS_OLD_AND_NEW_IMAGES
- CHANGE

Consider the following employee table into which a row is inserted, subsequently updated, and deleted:

```sql
create table employee (employee_id int primary key, employee_name varchar, employee_dept text);

insert into employee values(1001, 'Alice', 'Packaging');

update employee set employee_name='Bob' where employee_id=1001;

delete from employee where employee_id=1001;
```

CDC records for update and delete statements without enabling before image (that is, the default record type `CHANGE`) would be as follows:

<table>
<tr>
<td> CDC record for UPDATE: </td> <td> CDC record for DELETE: </td>
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

</td> </tr> </table>

For record type `ALL`, the update and delete records look like the following:

<table>
<tr>
<td> CDC record for UPDATE: </td> <td> CDC record for DELETE: </td>
</tr>

<tr>
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
  },
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

</td> </tr> </table>

For record type `FULL_ROW_NEW_IMAGE`, the update and delete records look like the following:

<table>
<tr>
<td> CDC record for UPDATE: </td> <td> CDC record for DELETE: </td>
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
      "value": "Bob",
      "set": true
    },
    "employee_dept": {
      "value": "Packaging",
      "set": true
    }
  },
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

</td> </tr> </table>

For record type `MODIFIED_COLUMNS_OLD_AND_NEW_IMAGES`, the update and delete records look like the following:

<table>
<tr>
<td> CDC record for UPDATE: </td> <td> CDC record for DELETE: </td>
</tr>

<tr>
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
    }
  },
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
    }
  },
  "after": null,
  "op": "d"
}
</pre>

</td> </tr> </table>

## Schema evolution

Table schema is needed for decoding and processing the changes and populating CDC records. Thus, older schemas are retained if CDC streams are lagging. Also, older schemas that are not needed for any of the existing active CDC streams are garbage collected. In addition, if before image is enabled, the schema needed for populating before image is also retained. The YugabyteDB source connector caches schema at the tablet level. This means that for every tablet the connector has a copy of the current schema for the tablet it is polling the changes for. As soon as a DDL command is executed on the source table, the CDC service emits a record with the new schema for all the tablets. The YugabyteDB source connector then reads those records and modifies its cached schema gracefully.

{{< warning title="No backfill support" >}}

If you alter the schema of the source table to add a default value for an existing column, the connector will NOT emit any event for the schema change. The default value will only be published in the records created after schema change is made. In such cases, it is recommended to alter the schema in your sinks to add the default value there as well.

{{< /warning >}}

Consider the following employee table (with schema version 0 at the time of table creation) into which a row is inserted, followed by a DDL resulting in schema version 1 and an update of the row inserted, and subsequently another DDL incrementing the schema version to 2. If a CDC stream created for the employee table lags and is in the process of streaming the update, corresponding schema version 1 is used for populating the update record.

```sql
create table employee(employee_id int primary key, employee_name varchar); // schema version 0

insert into employee values(1001, 'Alice');

alter table employee add dept_id int; // schema version 1

update employee set dept_id=9 where employee_id=1001; // currently streaming record corresponding to this update

alter table employee add dept_name varchar; // schema version 2
```

Update CDC record would be as follows:

```json
CDC record for UPDATE (using schema version 1):
{
  "before": {
    "public.employee.Value":{
      "employee_id": {
        "value": 1001
      },
      "employee_name": {
        "employee_name": {
          "value": {
            "string": "Alice"
          }
        }
      },
      "dept_id": null
    }
  },

  "after": {   "public.employee.Value":{
        "employee_id": {
          "value": 1001
        },
        "employee_name": {
          "employee_name": {
            "value": {
              "string": "Alice"
            }
          }
        },
        "dept_id": {
          "dept_id": {
            "value": {
              "int": 9
            }
          }
        }
      }
    },
    "op": "u"
}
```

## Colocated tables

YugabyteDB supports streaming of changes from [colocated tables](../../../../explore/colocation). The connector can be configured with regular configuration properties and deployed for streaming.

{{< note title="Note" >}}

If a connector is already streaming a set of colocated tables from a database and if a new table is created in the same database, you cannot deploy a new connector for this newly created table.

To stream the changes for the new table, delete the existing connector and deploy it again with the updated configuration property after adding the new table to `table.include.list`.

{{< /note >}}

## Important configuration settings

You can use several flags to fine-tune YugabyteDB's CDC behavior. These flags are documented in the [Change data capture flags](../../../../reference/configuration/yb-tserver/#change-data-capture-cdc-flags) section of the YB-TServer reference and [Change data capture flags](../../../../reference/configuration/yb-master/#change-data-capture-cdc-flags) section of the YB-Master reference. The following flags are particularly important for configuring CDC:

- [cdc_intent_retention_ms](../../../../reference/configuration/yb-tserver/#cdc-intent-retention-ms) - Controls retention of intents, in ms. If a request for change records is not received for this interval, un-streamed intents are garbage collected and the CDC stream is considered expired. This expiry is not reversible, and the only course of action would be to create a new CDC stream. The default value of this flag is 4 hours (4 x 3600 x 1000 ms).

- [cdc_wal_retention_time_secs](../../../../reference/configuration/yb-master/#cdc-wal-retention-time-secs) - Controls how long WAL is retained, in seconds. This is irrespective of whether a request for change records is received or not. The default value of this flag is 4 hours (14400 seconds).

- [cdc_snapshot_batch_size](../../../../reference/configuration/yb-tserver/#cdc-snapshot-batch-size) - This flag's default value is 250 records included per batch in response to an internal call to get the snapshot. If the table contains a very large amount of data, you may need to increase this value to reduce the amount of time it takes to stream the complete snapshot. You can also choose not to take a snapshot by modifying the [Debezium](../debezium-connector-yugabytedb/) configuration.

- [cdc_max_stream_intent_records](../../../../reference/configuration/yb-tserver/#cdc-max-stream-intent-records) - Controls how many intent records can be streamed in a single `GetChanges` call. Essentially, intents of large transactions are broken down into batches of size equal to this flag, hence this controls how many batches of `GetChanges` calls are needed to stream the entire large transaction. The default value of this flag is 1680, and transactions with intents less than this value are streamed in a single batch. The value of this flag can be increased, if the workload has larger transactions and CDC throughput needs to be increased. Note that high values of this flag can increase the latency of each `GetChanges` call.

## Retaining data for longer durations

To increase retention of data for CDC, change the two flags, `cdc_intent_retention_ms` and `cdc_wal_retention_time_secs` as required.

{{< warning title="Important" >}}

Longer values of `cdc_intent_retention_ms`, coupled with longer CDC lags (periods of downtime where the client is not requesting changes) can result in increased memory footprint in the YB-TServer and affect read performance.

{{< /warning >}}

## Content-based routing

By default, the connector streams all of the change events that it reads from a table to a single static topic. However, you may want to re-route the events into different Kafka topics based on the event's content. You can do this using the Debezium `ContentBasedRouter`. But first, two additional dependencies need to be placed in the Kafka-Connect environment. These are not included in the official _yugabyte-debezium-connector_ for security reasons. These dependencies are:

- Debezium routing SMT (Single Message Transform)
- Groovy JSR223 implementation (or other scripting languages that integrate with [JSR 223](https://jcp.org/en/jsr/detail?id=223))

To get started, you can rebuild the _yugabyte-debezium-connector_ image including these dependencies. The following shows what the Dockerfile would look like:

```Dockerfile
FROM quay.io/yugabyte/debezium-connector:latest
# Add the required jar files for content based routing
RUN cd $KAFKA_CONNECT_YB_DIR && curl -so debezium-scripting-2.1.2.Final.jar https://repo1.maven.org/maven2/io/debezium/debezium-scripting/2.1.2.Final/debezium-scripting-2.1.2.Final.jar
RUN cd $KAFKA_CONNECT_YB_DIR && curl -so groovy-4.0.9.jar  https://repo1.maven.org/maven2/org/apache/groovy/groovy/4.0.9/groovy-4.0.9.jar
RUN cd $KAFKA_CONNECT_YB_DIR && curl -so groovy-jsr223-4.0.9.jar  https://repo1.maven.org/maven2/org/apache/groovy/groovy-jsr223/4.0.9/groovy-jsr223-4.0.9.jar
```

To configure a content-based router, you need to add the following lines to your connector configuration:

```json
{
  ...,
  config: {
    ...,
    "transforms": "router",
    "transforms.router.type": "io.debezium.transforms.ContentBasedRouter",
    "transforms.router.language": "jsr223.groovy",
    "transforms.router.topic.expression": "<routing-expression>",
  }
}
```

The `<routing-expression>` contains the logic for routing of the events. For example, if you want to re-route the events based on the `country` column in user's table, you may use a expression similar to the following:

```regexp
value.after != null ? (value.after?.country?.value == '\''UK'\'' ? '\''uk_users'\'' : null) : (value.before?.country?.value == '\''UK'\'' ? '\''uk_users'\'' : null)"
```

This expression checks if the value of the row after the operation has the country set to `UK`. If _yes_, then the expression returns `uk_users`. If _no_, it returns _null_, and in case the row after the operation is _null_ (for example, in a "delete" operation), the expression also checks for the same condition on row values before the operation. The value that is returned determines which new Kafka Topic will receive the re-routed event. If it returns _null_, the event is sent to the default topic.

For more advanced routing configuration, refer to the [Debezium documentation](https://debezium.io/documentation/reference/stable/transformations/content-based-routing.html) on content-based routing.
