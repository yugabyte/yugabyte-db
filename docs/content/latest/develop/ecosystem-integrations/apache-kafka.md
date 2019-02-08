---
title: Apache Kafka
linkTitle: Apache Kafka
description: Apache Kafka
aliases:
  - /develop/ecosystem-integrations/apache-kafka/
menu:
  latest:
    identifier: apache-kafka
    parent: ecosystem-integrations
    weight: 571
isTocNested: true
showAsideToc: true
---

In this tutorial, we are going to use the [Kafka Connect-based Sink Connector for YugaByte DB](https://github.com/YugaByte/yb-kafka-connector) to store events from Apache Kafka into YugaByte DB using YugaByte DB's Cassandra-compatible [YCQL](../../../api/ycql) API.

## 1. Start Local Cluster

Start a YugaByte DB cluster on your [local machine](../../../quick-start/install/). Check that you are able to connect to YugaByte DB using `cqlsh` by doing the following.
<div class='copy separator-dollar'>
```sh
$ ./bin/cqlsh localhost
```
</div>
```sh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh> 
```

Create the table that would store the Kafka events.

```{.sh .copy .separator-gt}
cqlsh> CREATE KEYSPACE IF NOT EXISTS demo;
CREATE TABLE demo.test_table (key text, value bigint, ts timestamp, PRIMARY KEY (key));
```

## 2. Download Apache Kafka 

Download from the [Apache Kafka downloads page](https://kafka.apache.org/downloads). This tutorial uses the `2.11` version of Apache Kafka.
<div class='copy separator-dollar'>
```sh
$ mkdir -p ~/yb-kafka && cd ~/yb-kafka
wget http://apache.cs.utah.edu/kafka/2.0.0/kafka_2.11-2.0.0.tgz
tar xvfz kafka_2.11-2.0.0.tgz && cd kafka_2.11-2.0.0
```
</div>

## 3. Install the Kafka Sink Connector for YugaByte DB

Clone the git `yb-kafka-connector` git repo.
<div class='copy separator-dollar'>
```sh
$ cd ~/yb-kafka
git clone https://github.com/YugaByte/yb-kafka-connector.git
cd yb-kafka-connector/
```
</div>

Build the repo to get the connector jar.
<div class='copy separator-dollar'>
```sh
$ mvn clean install -DskipTests
```
</div>

The connector jar `yb-kafka-connnector-1.0.0.jar` is now placed in the `./target` directory. Copy this jar to the libs directory in Kafka Home.
<div class='copy separator-dollar'>
```sh
$ cp ./target/yb-kafka-connnector-1.0.0.jar ~/yb-kafka/kafka_2.11-2.0.0/libs/
```
</div>

Go to the Kafka libs directory and get the additional jars that the connector depends on (including the driver for the Cassandra-compatible YCQL API)
<div class='copy separator-dollar'>
```sh
$ cd ~/yb-kafka/kafka_2.11-2.0.0/libs/
wget http://central.maven.org/maven2/io/netty/netty-all/4.1.25.Final/netty-all-4.1.25.Final.jar
wget http://central.maven.org/maven2/com/yugabyte/cassandra-driver-core/3.2.0-yb-18/cassandra-driver-core-3.2.0-yb-18.jar
wget http://central.maven.org/maven2/com/codahale/metrics/metrics-core/3.0.1/metrics-core-3.0.1.jar
```
</div>

## 4. Start ZooKeeper and Kafka

Now you can start ZooKeeper and Kafka as shown below.
<div class='copy separator-dollar'>
```sh
$ cd ~/yb-kafka/kafka_2.11-2.0.0
```
</div>
<div class='copy separator-dollar'>
```sh
$ ./bin/zookeeper-server-start.sh config/zookeeper.properties &
```
</div>
<div class='copy separator-dollar'>
```sh
$ ./bin/kafka-server-start.sh config/server.properties &
```
</div>

Now create the Kafka topic that will be used to persist messages in the YugaByte DB table we created earlier.
<div class='copy separator-dollar'>
```sh
$ ./bin/kafka-topics.sh --create \
	--zookeeper localhost:2181 \
	--replication-factor 1 \
	--partitions 1 \
	--topic test
```
</div>

## 5. Start Kafka Sink Connector for YugaByte DB

At this point, we have YugaByte DB's YCQL APU running at 9042 port with the `test_table` table created in the `demo` keyspace. We also have Kafka running at the 9092 port with the `test_topic` topic created. We are ready to start the connector.
<div class='copy separator-dollar'>
```sh
$ ./bin/connect-standalone.sh \
	~/yb-kafka/yb-kafka-connector/resources/examples/kafka.connect.properties \
	~/yb-kafka/yb-kafka-connector/resources/examples/yugabyte.sink.properties 
```
</div>

The `yugabyte.sink.properties` file used above (and shown below) has the configuration necessary for this sink to work correctly. You will have to change this file to the Kafka topic and YugaByte DB table necessary for your application.
```sh
# Sample yugabyte sink properties.

name=yugabyte-sink
connector.class=com.yb.connect.sink.YBSinkConnector

topics=test_topic

yugabyte.cql.keyspace=demo
yugabyte.cql.tablename=test_table
```

## 6. Produce Events for Kafka

We can now produce some events into Kafka using the `kafka-console-producer.sh` utility that ships with Kafka.
<div class='copy separator-dollar'>
```sh
$ ~/yb-kafka/kafka_2.11-2.0.0/bin/kafka-console-producer.sh 
	--broker-list localhost:9092 \
	--topic test_topic
```
</div>

Enter the following.

```{.sh .copy}
{"key" : "A", "value" : 1, "ts" : 1541559411000}
{"key" : "B", "value" : 2, "ts" : 1541559412000}
{"key" : "C", "value" : 3, "ts" : 1541559413000}
```

## 7. Verify Events in YugaByte DB

The events above should now show up as rows in the YugaByte DB table.

```{.sh .copy .separator-gt}
cqlsh> select * from demo.test_table;
```
```sh
key | value | ts
----+-------+---------------------------------
  A |     1 | 2018-11-07 02:56:51.000000+0000
  C |     3 | 2018-11-07 02:56:53.000000+0000
  B |     2 | 2018-11-07 02:56:52.000000+0000
```