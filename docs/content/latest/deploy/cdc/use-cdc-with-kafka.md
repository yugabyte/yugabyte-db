---
title: Use CDC with Kafka [beta]
linkTitle: Use CDC with Kafka [beta]
description: Use CDC with Kafka [beta]
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    parent: cdc
    identifier: use-cdc-with-kafka
    weight: 691
type: page
isTocNested: true
showAsideToc: true
---


You can use the Change Data Capture (CDC) API, you can use YugabyteDB as a data source to an Apache Kafka or console sink.
Follow the steps outlined below to explore the new CDC functionality using a YugabyteDB local cluster.

## Step 1 — Set up YugabyteDB

Create a YugabyteDB local cluster and add a table.

If you are new to YugabyteDB, you can create a local YugaByte cluster in under five minutes by following the steps in the [Quick start](/quick-start/install/

## Step 2 — Set up Apache Kafka (skip if your are logging to console)

YugabyteDB supports the use of [Apache Avro schemas](http://avro.apache.org/docs/current/#schemas) to serialize and deserialize tables.

Create two Avro schemas, one for the table and one for the primary key of the table. After this step, you should have two files: `table_schema_path.avsc` and `primary_key_schema_path.avsc`.

## Step 3 — Start the Apache Kafka services

1. Download and install Confluent.

The following series of commands will download Confluent, open the `tar` file, and move you to the Confluent installation directory.

```bash
curl -O http://packages.confluent.io/archive/5.3/confluent-5.3.1-2.12.tar.gz
tar -xvzf confluent-5.3.1-2.12.tar.gz
cd confluent-5.3.1/
```

2. Download the `bin` directory and add it to the PATH variable.

```
curl -L https://cnfl.io/cli | sh -s -- -b /<path-to-directory>/bin
export PATH=<path-to-confluent>/bin:$PATH
export CONFLUENT_HOME=~/code/confluent-5.3.1
```

3. Start the Zookeeper, Kafka, and the Avro schema registry services.

```bash
./bin/confluent local start
```

4. Create a Kafka topic.

```bash
./bin/kafka-topics --create --partitions 1 --topic <topic_name> --bootstrap-server localhost:9092 --replication-factor 1
```

5. Start the Kafka consumer.

```bash
bin/kafka-avro-console-consumer --bootstrap-server localhost:9092 --topic <topic_name> --key-deserializer=io.confluent.kafka.serializers.KafkaAvroDeserializer --value-deserializer=io.confluent.kafka.serializers.KafkaAvroDeserializer
```

## Step 4 — Set up Kafka Connect to YugabyteDB

1. In a new shell window, fork YugaByte's GitHub repository for [Kafka Connect to YugabyteDB](https://github.com/yugabyte/yb-kafka-connector) and change to the `yb-cdc` directory.

```
git clone https://github.com/yugabyte/yb-kafka-connector.git
cd yb-kafka-connector/yb-cdc
```

2. Start the Kafka connector application.

## Step 5 — Logging 

You can now follow the steps below to log to a console or Kafka sink.

**Logging to console**

```bash
java -jar target/yb_cdc_connector.jar
--table_name <namespace/database>.<table>
--master_addrs <yb master addresses> [default 127.0.0.1:7100]
--[stream_id] <optional existing stream id>
--log_only // Flag to log to console.
```

**Logging to Kafka**

```bash
java -jar target/yb_cdc_connector.jar
--table_name <namespace/database>.<table>
--master_addrs <yb master addresses> [default 127.0.0.1:7100]
--[stream_id] <optional existing stream id>
--kafka_addrs <kafka cluster addresses> [default 127.0.0.1:9092]
--shema_registry_addrs [default 127.0.0.1:8081]
--topic_name <topic name to write to>
--table_schema_path <avro table schema>
--primary_key_schema_path <avro primary key schema>
```

## Step 6 — Write values and observe

In another window, write values to the table and observe the values on your chosen output stream.
