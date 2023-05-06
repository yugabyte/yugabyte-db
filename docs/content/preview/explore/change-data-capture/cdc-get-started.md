---
title: Get started with CDC in YugabyteDB
headerTitle: Get started
linkTitle: Get started
description: Get started with Change Data Capture in YugabyteDB.
headcontent: Change Data Capture in YugabyteDB
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    parent: explore-change-data-capture
    identifier: cdc-get-started
    weight: 30
type: docs
---

In order to stream data change events from YugabyteDB databases, you need to use Debezium YugabyteDB connector. To deploy a Debezium YugabyteDB connector, you install the Debezium YugabyteDB connector archive, configure the connector, and start the connector by adding its configuration to Kafka Connect. You can download the connector from [GitHub releases](https://github.com/yugabyte/debezium-connector-yugabytedb/releases). The connector supports Kafka Connect version 2.x and above, and for YugabyteDB, it supports version 2.14 and above. For more connector configuration details and complete steps, refer to the Debezium connector doc

## Setting up YugabyteDB for CDC

The following steps are necessary to set up YugabyteDB for use with the Debezium YugabyteDB connector. 
1. Create a DB stream ID.

   Before you use the YugabyteDB connector to retriev data change events from YugabyteDB database, create a stream ID using the [yb-admin](../../../admin/yb-admin/#change-data-capture-cdc-commands) CLI command.

1. Make sure the master ports are open.

   The connector connects to the master processes running on the YugabyteDB server. Make sure the ports on which the YugabyteDB server's master processes are running are open. The default port on which the process runs is `7100`.

1. Monitor available disk space.

   The change records for CDC are read from the WAL. YugabyteDB CDC maintains checkpoint internally for each of the DB stream ID and garbage collects the WAL entries if those have been streamed to the CDC clients.

   In case CDC is lagging or away for some time, the disk usage may grow and may cause YugabyteDB cluster instability. To avoid a scenario like this if a stream is inactive for a configured amount of time we garbage collect the WAL. This is configurable by a [GFlag](../../../reference/configuration/yb-tserver/#change-data-capture-cdc-flags).


### Tablet splitting

YugabyteDB also supports [tablet splitting](../../../architecture/docdb-sharding/tablet-splitting). While streaming changes, if the YugabyteDB source connector detects that a tablet has been split, it gracefully handles the splitting and starts polling for the children tablets.

### Dynamic addition of new tables

If a new table is added to a namespace on which there is an active stream ID, then it will be added to the stream. The YugabyteDB source connector launches a poller thread at startup which continuously checks if there is a new table added to the stream ID it is configured to poll for, after the connector detects that there is a new table, it signals the Kafka Connect runtime to restart the connector so that the newly added table can be polled. The behaviour of this poller thread can be governed by the configuration properties `auto.add.new.tables` and `new.table.poll.interval.ms`, refer to [configuration properties](#connector-configuration-properties) for more details.

### Schema evolution

The YugabyteDB source connector caches schema at the tablet level, this means that for every tablet the connector has a copy of the current schema for the tablet it is polling the changes for. As soon as a DDL command is executed on the source table, CDC service emits a record with the new schema for all the tablets. The YugabyteDB source connector then reads those records and modifies its cached schema gracefully.

{{< warning title="No backfill support" >}}

If you alter the schema of the source table to add a default value for an existing column, the connector will NOT emit any event for the schema change. The default value will only be published in the records created after schema change is made. In such cases, it is recommended to alter the schema in your sinks to add the default value there as well.

{{< /warning >}}

## Avro serialization

## Protobuf serialization
To use the [protobuf](http://protobuf.dev) format for the serialization/de-serialization of the kafka messages, you can use the [Protobuf Converter](https://www.confluent.io/hub/confluentinc/kafka-connect-protobuf-converter). After downloading and including the required `JAR` files in the Kafka-Connect enviornment, you can directly configure the CDC source and sink connectors to use this converter.

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

### AVRO serialization

The YugabyteDB source connector also supports AVRO serialization with schema registry. To use AVRO serialization, simply add the following configuration to your connector:

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
## Before image

[Before image](../#before-image) refers to the state of the row _before_ the change event occurred. The YugabyteDB connector sends the before image of the row when it will be configured using a stream ID enabled with before image. For more information about how to create the stream ID for a before image, see [yb-admin](../../../admin/yb-admin/#change-data-capture-cdc-commands).

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

## Before image

Before image refers to the state of a row before the change event occurred. It is populated for UPDATE and DELETE events. For INSERT events, before image doesn't make sense as the change record itself is in the context of new row insertion. 

Yugabyte uses multiversion concurrency control(MVCC) mechanism, and compacts data at regular intervals. The compaction or the history retention is controlled by [history retention interval flag](../../reference/configuration/yb-tserver/#timestamp_history_retention_interval_sec). However, when before image is enabled for a database, YugabyteDB adjusts the history retention for that database based on the most lagging active CDC stream so that the previous row state is retained/available. Consequently, in the case of a lagging CDC stream, the amount of space required for the database grows as more data is retained. On the other hand, older rows that are not needed for any of the active CDC streams are identified and garbage collected.

Schema version that is currently being used by a CDC stream will be used to frame before and current row images.

The before image functionality is disabled by default unless it is specifically turned on during the CDC stream creation. [yb-admin](../../admin/yb-admin/#enabling-before-image) command can be used to create a CDC stream with before image enabled.

For example, let us consider the following employee table into which a row is inserted, subsquently updated and deleted.
```sh
create table employee(employee_id int primary key, employee_name varchar);

insert into employee values(1001, 'Alice');

update employee set employee_name='Bob' where employee_id=1001;

delete from employee where employee_id=1001;
```

CDC records for update and delete statements without enabling before image would be 
| CDC record for UPDATE | CDC record for DELETE |
| ----------- | ----------- |
| ```sh
{
  "before": null,
  "after": {
    "public.employee.Value":{
      "employee_id": {
        "value": 1001
      },
      "employee_name": {
        "employee_name": {
          "value": {
            "string": "Bob"
          }
        }
      }
    }
  },
  "op": "u"
}
``` | ```sh
{
  "before": {
    "public.employee.Value":{
      "employee_id": {
        "value": 1001
      },
      "employee_name": null
    }
  },
  "after": null,
  "op": "d"
}
``` |





With before image enabled, the update and delete records look like

```sh
CDC record for UPDATE:
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
      }
    }
  },
  "after": {
    "public.employee.Value":{
      "employee_id": {
        "value": 1001
      },
      "employee_name": {
        "employee_name": {
          "value": {
            "string": "Bob"
          }
        }
      }
    }
  },
  "op": "u"
}

CDC record for DELETE:
{
  "before": {
    "public.employee.Value":{
      "employee_id": {
        "value": 1001
      },
      "employee_name": {
        "employee_name": {
          "value": {
            "string": "Bob"
          }
        }
      }
    }
  },
  "after": null,
  "op": "d"
}
```

## Schema evolution

Table schema is needed for decoding and processing the changes and populating CDC records. Thus, older schemas are retained if CDC streams are lagging. Also, older schemas not needed for any of the existing active CDC streams are garbage collected. In addition, if before image is enabled, the schema needed for populating before image as well is retained. 

For example, let us consider the following employee table (with schema version 0 at the time of table creation) into which a row is inserted, followed by a DDL resulting in schema version 1 and an update of the row inserted, and subsequently another DDL incrementing the schema version to 2. If a CDC stream created for employee table lags and is in the process of streaming the update, corresponding schema version 1 is used for populating the update record. 

```sh
create table employee(employee_id int primary key, employee_name varchar); // schema version 0

insert into employee values(1001, 'Alice');

alter table employee add dept_id int; // schema version 1

update employee set dept_id=9 where employee_id=1001; // currently streaming record corresponding to this update

alter table employee add dept_name varchar; // schema version 2
```

Update CDC record would be 
```sh

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

## Transformations

## Content-based routing
By default, the Yugabyte Debezium connector streams all of the change events that it reads from a table to a single static topic. However, you may want to re-route the events into different kafka topics based on the event's content. It is possible through debezium's `ContentBasedRouter`. But first, there are two additional dependencies that need to be placed in the Kafka-Connect environment. These are not included in the official *yugabyte-debezium-connector* for security reasons. In particular, these dependencies are

- Debezium routing SMT (Single Message Transform)
- Groovy JSR223 implementation (or other scripting languages that integrate with [JSR 223](https://jcp.org/en/jsr/detail?id=223))

To get started, you can rebuild the *yugabyte-debezium-connector* image including these dependencies. Here’s what the Dockerfile would look like:

```Dockerfile
FROM quay.io/yugabyte/debezium-connector:latest
# Add the required jar files for content based routing
RUN cd $KAFKA_CONNECT_YB_DIR && curl -so debezium-scripting-2.1.2.Final.jar https://repo1.maven.org/maven2/io/debezium/debezium-scripting/2.1.2.Final/debezium-scripting-2.1.2.Final.jar
RUN cd $KAFKA_CONNECT_YB_DIR && curl -so groovy-4.0.9.jar  https://repo1.maven.org/maven2/org/apache/groovy/groovy/4.0.9/groovy-4.0.9.jar
RUN cd $KAFKA_CONNECT_YB_DIR && curl -so groovy-jsr223-4.0.9.jar  https://repo1.maven.org/maven2/org/apache/groovy/groovy-jsr223/4.0.9/groovy-jsr223-4.0.9.jar
```

To configure a content-based router you need to add the following lines to your connector configuration.

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
The `<routing-expression>` contains the logic for routing of the events. For example, if you want to re-route the events based on the `country` column in user's table, you may use a expression similar to

```
value.after != null ? (value.after?.country?.value == '\''UK'\'' ? '\''uk_users'\'' : null) : (value.before?.country?.value == '\''UK'\'' ? '\''uk_users'\'' : null)"
```

This expression checks if the value of the row after the operation has the country set to “UK.” If *yes* then the expression returns “uk_users.” If *no*, it returns *null*, and in case the row after the operation is *null* (for example, in a “delete” operation), the expression also checks for the same condition on row values before the operation. The value that is returned determines which new Kafka Topic will receive the re-routed event. If it returns *null*, the event is sent to the default topic.

For more advanced routing configuration, you can refer to [Debezium’s official documentation](https://debezium.io/documentation/reference/stable/transformations/content-based-routing.html) on content-based routing.
