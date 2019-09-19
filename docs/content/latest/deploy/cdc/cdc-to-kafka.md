---
title: Change data capture to Kafka
linkTitle: Change data capture (CDC) to Kafka
description: Change data capture (CDC) to Kafka
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    parent: cdc
    identifier: cdc-to-kafka
    weight: 691
type: page
isTocNested: true
showAsideToc: true
---

Follow the steps below to connect a local YugabyteDB cluster to use the Change Data Capture (CDC) API to send data changes to Apache Kafka. To learn about the change data capture (CDC) architecture, see [Change data capture (CDC)](../architecture/cdc-architecture).

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

## Step 5 — Log to Kafka

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

{{< note title="Note" >}}

In the command above, only one YB-Master address is specified, assuming that you are using a 1-node local cluster with RF=1. If you are using a 3-node local cluster, then you need to add a comma-delimited list of the addresses for all of your YB-Master services in the `--master-addrs` option above.

{{< /note >}}


## Step 6 — Write values and observe

In another window, write values to the table and observe the values on your Kafka output stream.
