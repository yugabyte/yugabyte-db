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

## Schema evolution

## Before image

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
