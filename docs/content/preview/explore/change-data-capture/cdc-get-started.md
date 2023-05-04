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

![CDC records for update and delete statements without enabling before image](/images/explore/cdc-records-without-before-image.png)

With before image enabled, the update and delete records look like

![CDC records for update and delete statements with before image](/images/explore/cdc-records-with-before-image.png)

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

![Update CDC record depicting schema evolution](/images/explore/update-cdc-record-schema-evolution.png)

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
