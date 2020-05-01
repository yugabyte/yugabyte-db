---
title: YugabyteDB CDC connector
linkTitle: YugabyteDB CDC connector
description: YugabyteDB CDC connector
beta: /latest/faq/general/#what-is-the-definition-of-the-beta-feature-tag
block_indexing: true
menu:
  v2.0:
    parent: cdc
    identifier: use-cdc
    weight: 641
type: page
isTocNested: true
showAsideToc: true
---

Use change data capture (CDC) in your YugabyteDB deployments to asynchronously stream data changes to external systems. In the sections below, learn how you can use the YugabyteDB CDC connector to send data changes to Apache Kafka or to `stdout`.

## Prerequisites

### Java

A Java runtime (or JDK) for Java 8 or later. JDK and JRE installers for Linux, macOS, and Windows can be downloaded from [OpenJDK](https://openjdk.java.net/install/), [AdoptOpenJDK](https://adoptopenjdk.net/releases.html), or [Azul Systems](https://www.azul.com/downloads/zulu-community/). When installed, the default location of the JRE or JDK is:

- Linux: `jre\lib\`
- macOS: `\Library\Java\`
- Windows: `C:\Program Files\Java\`

The JRE directory location can be found by looking at the `JAVA_HOME` environment variable.

## Install the connector

1. Download the [YugabyteDB CDC connector (`yb-cdc-connector.jar`)](https://github.com/yugabyte/yb-kafka-connector/blob/master/yb-cdc/yb-cdc-connector.jar).

2. Install the JAR file in the following recommended location:

- Linux: `jre\lib\ext\yb-cdc-connector.jar`
- macOS: `\Library\Java\Extensions\yb-cdc-connector.jar`
- Windows: `%SystemRoot%\Sun\Java\lib\ext\yb-cdc-connector.jar`

## Use the connector

To use the YugabyteDB CDC connector, run the `yb_cdc_connector` JAR file.

### Syntax for Apache Kafka

```sh
java -jar target/yb_cdc_connector.jar
--table_name <namespace>.<table>
--master_addrs <yb-master-addresses>
[ --stream_id <existing-stream-id> ]
--kafka_addrs <kafka-cluster-addresses>
--schema_registry_addrs
--topic_name <topic-name>
--table_schema_path <avro-table-schema>
--primary_key_schema_path <avro-primary-key-schema>
```

### Syntax for stdout

```sh
java -jar yb_cdc_connector.jar
--table_name <namespace>.<table>
--master_addrs <yb-master-addresses>
--stream_id <stream-id>
--log_only
```

## Parameters

### Required parameters 

#### `--table_name`

Specify the namespace and table, where namespace is the database (YSQL) or keyspace (YCQL).

#### `--master_addrs`

Specify the IP addresses for all of the YB-Master servers that are producing or consuming. Default value is `127.0.0.1:7100`.

If you are using a 3-node local cluster, then you need to specify a comma-delimited list of the addresses for all of your YB-Master servers.

#### `--log_only` (stdout only)

Flag to restrict logging only to the console.

#### `--topic_name` (Apache Kafka only)

Specify the Apache Kafka topic name.

#### `--schema_registry_addrs` (Apache Kafka only)

#### `--table_schema_path` (Apache Kafka only)

Specify the location of the Avro file (`.avsc`) for the table schema.

#### `--primary_key_schema_path` (Apache Kafka only)

Specify the location of the Avro file (`.avsc`) for the primary key schema.

### Optional parameters

#### `--stream_id`

Specify the existing stream ID. If you do not specify the stream ID, on restart the log output stream starts from the first available record.

If specified (recommended), on restart, the log output stream resumes after the last output logged.

To get the stream ID, run the YugabyteDB CDC connector and the first time you can get the stream ID from the console output.

## Examples

### Sending a CDC output stream to "stdout"

The following command will start the YugabyteDB CDC connector and send an output stream from a 3-node YugabyteDB cluster to `stdout`.

```sh
java -jar yb_cdc_connector.jar
--master_addrs 127.0.0.1,127.0.0.2,127.0.0.3
--table_name yugabyte.users
--log_only
```

### Sending a CDC output stream to a Kafka topic

The following command will start the YugabyteDB CDC connector and send an output stream from a 3-node YugabyteDB cluster to a Kafka topic.

```sh
java -jar target/yb_cdc_connector.jar
--table_name yugabyte.users
--master_addrs 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100
--kafka_addrs 127.0.0.1:9092
--schema_registry_addrs 127.0.0.1:8081
--topic_name users_topic
--table_schema_path table_schema_path.avsc
--primary_key_schema_path primary_key_schema_path.avsc
```
