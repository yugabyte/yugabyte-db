---
title: Using the YugabyteDB CDC connector
linkTitle: Using the YugabyteDB CDC connector
description: Using the YugabyteDB CDC connector
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    parent: cdc
    identifier: use-cdc
    weight: 641
type: page
isTocNested: true
showAsideToc: true
---

Use change data capture (CDC) in your YugabyteDB deployments to asynchronously replicate data changes. In the sections below, learn how you can use the YugabyteDB CDC connector to send data changes to Apache Kafka or to `stdout`.

{{< note title="Note" >}}

The information on this page is for testing and learning about using CDC with the YugabyteDB CDC connector on a local YugabyteDB cluster. Details about requirements for production deployments will be added shortly.

{{< /note >}}

## Prerequisites

### YugabyteDB

A 1-node YugabyteDB cluster with an RF of 1 is up and running locally (the `yb-ctl create` command creates this by default). If you are new to YugabyteDB, you can create a local YugabyteDB cluster in under five minutes by following the steps in the [Quick start](/quick-start/install/).

### Java

A Java runtime (or JDK) for Java 8 or later. JDK and JRE installers for Linux, macOS, and Windows can be downloaded from [OpenJDK](https://openjdk.java.net/install/), [AdoptOpenJDK](https://adoptopenjdk.net/releases.html), or [Azul Systems](https://www.azul.com/downloads/zulu-community/). When installed, the default location of the JRE or JDK is:

- Linux: `jre\lib\`
- macOS: `\Library\Java\`
- Windows: `C:\Program Files\Java\`

The JRE directory location can be found by looking at the `JAVA_HOME` environment variable.

### [Optional] Apache Kafka

A local install of the Confluent Platform should be up and running. The [Confluent Platform](https://docs.confluent.io/current/platform.html) includes [Apache Kafka](https://docs.confluent.io/current/kafka/introduction.html) and additional tools and services (including Zookeeper and Avro), making it easy for you to quickly get started using the Kafka event streaming platform.

To quickly get a local Confluent Platform (with Apache Kafka) up and running, follow the steps in the [Confluent Platform Quick Start (Local)](https://docs.confluent.io/current/quickstart/ce-quickstart.html#ce-quickstart).

{{< note title="Note" >}}

The Confluent Platform currently only supports Java 8 and 11. If you do not use one of these, an error message is generated and it will not start.

{{< /note >}}

## Install the YugabyteDB CDC connector

1. Download the [YugabyteDB CDC connector (`yb-cdc-connector.jar`)](https://github.com/yugabyte/yb-kafka-connector/blob/master/yb-cdc/yb-cdc-connector.jar).

2. Install the JAR file in the following recommended location:

- Linux: `jre\lib\ext\yb-cdc-connector.jar`
- macOS: `\Library\Java\Extensions\yb-cdc-connector.jar`
- Windows: `%SystemRoot%\Sun\Java\lib\ext\yb-cdc-connector.jar`

## Use the YugabyteDB CDC connector

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

#### `topic_name` (Apache Kafka only)

Specify the Apache Kafka topic name.

#### `schema_registry_addrs` (Apache Kafka only)

#### `table_schema_path` (Apache Kafka only)

Specify the location of the Avro file (`.avsc`) for the table schema.

#### `primary_key_schema_path` (Apache Kafka only)

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
