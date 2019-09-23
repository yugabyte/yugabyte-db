---
title: Using the Yugabyte CDC connector
linkTitle: Using the Yugabyte CDC connector
description: Using the Yugabyte CDC connector
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

Use change data capture (CDC) in your YugabyteDB deployments to asynchronously replicate data changes. In the sections below, learn how you can use the Yugabyte CDC connector tp send data changes to Apache Kafka or to `stdout`.

## Prerequisites

A Java runtime (or JDK) for Java 8 or later. JDK and JRE installers for Linux, macOS, and Windows can be downloaded from [OpenJDK](https://openjdk.java.net/install/), [AdoptOpenJDK](https://adoptopenjdk.net/releases.html), or [Azul Systems](https://www.azul.com/downloads/zulu-community/). When installed, the default location of the JRE or JDK is:

- Linux: `jre\lib\`
- macOS: `\Library\Java\`
- Windows: `C:\Program Files\Java\`

The JRE directory location can be found by looking at the `JAVA_HOME` environment variable.

## Install the Yugabyte CDC connector

1. Download the [Yugabyte CDC connector (`yb-cdc-connector.jar`)](https://github.com/yugabyte/yb-kafka-connector/blob/master/yb-cdc/yb-cdc-connector.jar).

2. Install the JAR file in the following recommended location:

- Linux: `jre\lib\ext\yb-cdc-connector.jar`
- macOS: `\Library\Java\Extensions\yb-cdc-connector.jar`
- Windows: `%SystemRoot%\Sun\Java\lib\ext\yb-cdc-connector.jar`

## Use the Yugabyte CDC connector

To use the Yugabyte CDC connector, run the `yb_cdc_connector` JAR file.

### Apache Kafka

```bash
java -jar target/yb_cdc_connector.jar
--table_name <namespace/database>.<table>
--master_addrs <yb master addresses>
[ --[stream_id] <optional existing stream id> ]
--kafka_addrs <kafka cluster addresses> [default 127.0.0.1:9092]
--schema_registry_addrs [default 127.0.0.1:8081]
--topic_name <topic name to write to>
--table_schema_path <avro table schema>
--primary_key_schema_path <avro primary key schema>
```

### `stdout`

```bash
java -jar yb_cdc_connector.jar
--table_name <database>.<table>
--master_addrs <yb master addresses> [default 127.0.0.1:7100]
[ --stream_id <stream-id> ]
[ --log_only ]
```

For details on the options, see [Use ]

## Parameters

### Required parameters

#### `--table_name`

Specify the name of the YSQL database or YCQL namespace.

#### `--master_addrs`

Specify the IP address of the YB-Master services. Default value is `127.0.0.1:7100`.

If you are using a 3-node local cluster, then you need to specify a comma-delimited list of the addresses for all of your YB-Master services.

### Optional parameters

#### `--stream_id`

Specify the existing stream ID. If you do not specify the stream ID, on restart the log output stream starts from the first available record.

If specified (recommended), on restart, the log output stream resumes after the last output logged.

To get the stream ID, the first time you can get the stream ID from the console output.

#### `--log_only`

Flag to restrict logging only to the console.

## Examples

### Sending a CDC output stream to `stdout`

The following command will start the Yugabyte CDC connector and send an output stream from a 3-node YugabyteDB cluster to `stdout`.

```bash
java -jar yb_cdc_connector.jar
--master_addrs 127.0.0.1,127.0.0.2,127.0.0.3
--table_name yugabyte.cdc
--log_only
```

### Sending a CDC output stream to a Kafka topic

```bash
java -jar target/yb_cdc_connector.jar
--table_name <namespace/database>.<table>
--master_addrs <yb master addresses> [default 127.0.0.1:7100]
--[stream_id] <optional existing stream id>
--kafka_addrs <kafka cluster addresses> [default 127.0.0.1:9092]
--schema_registry_addrs [default 127.0.0.1:8081]
--topic_name <topic name to write to>
--table_schema_path <avro table schema>
--primary_key_schema_path <avro primary key schema>
```