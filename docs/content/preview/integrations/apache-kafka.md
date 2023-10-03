---
title: Apache Kafka
linkTitle: Apache Kafka
description: Apache Kafka
aliases:
  - /preview/integrations
menu:
  preview_integrations:
    identifier: apache-kafka
    parent: data-integration
    weight: 571
type: docs
---

This document describes how to use the [Kafka Connect Sink Connector for YugabyteDB](https://github.com/yugabyte/yb-kafka-connector) to store events from Apache Kafka into YugabyteDB via [YSQL](../../api/ysql/) and [YCQL](../../api/ycql/) APIs.

## Prerequisites

Before you can start using Kafka Connect Sink, ensure that you have the following:

- Confluent Platform with free community features.

  For more information and installation instructions, see [On-Premises Deployments](https://docs.confluent.io/platform/current/installation/installing_cp/overview.html) in the Confluent Platform documentation.

- YugabyteDB cluster.

  For more information and instructions, see [YugabyteDB Quick Start Guide](/preview/quick-start/).

## Configuring Kafka

You configure Kafka Connect Sink as follows:

- Download the TAR file using Confluent Platform, extract this file, and then set the  `CONFLUENT_HOME` and `PATH` environment variables, as follows:

  ```sh
  # assuming a filename of confluent-community-6.1.1.tar...

  tar -xvf confluent-community-6.1.1.tar
  cd confluent-6.1.1
  export CONFLUENT_HOME=/Users/myusername/confluent-6.1.1
  export PATH=$PATH:$CONFLUENT_HOME/bin
  ```

- Use the following command to start ZooKeeper, Kafka, Schema Registry, Kafka Connect REST API, and Kafka Connect:

  ```sh
  confluent local services start
  ```

  Expect output similar to the following:

  ```output
  The local commands are intended for a single-node development environment only,
  NOT for production usage. https://docs.confluent.io/current/cli/index.html

  Using CONFLUENT_CURRENT: /var/folders/_1/ltd94t1x2nsdrwj302jl85vc0000gn/T/confluent.127538
  Starting ZooKeeper
  ZooKeeper is [UP]
  Starting Kafka
  Kafka is [UP]
  Starting Schema Registry
  Schema Registry is [UP]
  Starting Kafka REST
  Kafka REST is [UP]
  Starting Connect
  Connect is [UP]
  Starting ksqlDB Server
  ksqlDB Server is [UP]
  ```

- After changing configuration, use the following command to stop Kafka Connect:

  ```sh
  confluent local services connect stop
  ```

  Expect output similar to the following:

  ```output
  The local commands are intended for a single-node development environment only,
  NOT for production usage. https://docs.confluent.io/current/cli/index.html

  Using CONFLUENT_CURRENT: /var/folders/_1/ltd94t1x2nsdrwj302jl85vc0000gn/T/confluent.127538
  Stopping Connect
  Connect is [DOWN]
  ```

- Restart Kafka Connect to trigger loading of the YugabyteDB Sink JAR file.

## Building YugabyteDB Kafka Connect Sink

Run the following commands to build the JAR files needed by Kafka Connect Sink:

```sh
git clone https://github.com/yugabyte/dsbulk.git
cd dsbulk
mvn clean install -DskipTests
```

The preceding command is used for the local installation.

```sh
git clone https://github.com/yugabyte/messaging-connectors-commons.git
cd messaging-connectors-commons
mvn clean install -DskipTests
```

The preceding command is used for the local installation.

```sh
git clone https://github.com/yugabyte/yb-kafka-sink.git
cd yb-kafka-sink
mvn clean package -DskipTests
```

Expect the following output:

```output
[INFO] Replacing /home/centos/yb-kafka-sink/dist/target/kafka-connect-yugabytedb-sink-1.4.1-SNAPSHOT.jar with /home/centos/yb-kafka-sink/dist/target/kafka-connect-yugabytedb-sink-distribution-1.4.1-SNAPSHOT-shaded.jar
```

You can copy the `kafka-connect-yugabytedb-sink-1.4.1-SNAPSHOT.jar` into the Kafka Connect class path.

The next step is to modify the `etc/schema-registry/connect-avro-distributed.properties` file to add `kafka-connect-yugabytedb-sink-1.4.1-SNAPSHOT.jar` to `plugin.path`. That is, the line containing `plugin.path` should contain the path to `kafka-connect-yugabytedb-sink-1.4.1-SNAPSHOT.jar`, as demonstrated by the following example:

```output
...
plugin.path=/Users/me/confluent-6.1.1/etc/connectors/kafka-connect-yugabytedb-sink-1.4.1-SNAPSHOT.jar,share/java
```

The next step is to start Kafka Connect again by executing the following command:

```sh
confluent local services connect start
//connect-distributed etc/schema-registry/connect-avro-distributed.properties
```

To list the connectors plugin and verify if the connectors are loaded, execute the following command:

```sh
confluent local services connect plugin
```

The following is a sample output produced by the preceding command:

```nocopy.json
{
  "class": "com.datastax.kafkaconnector.DseSinkConnector",
  "type": "sink",
  "version": "1.4.1-SNAPSHOT"
 },
 {
  "class": "com.datastax.oss.kafka.sink.CassandraSinkConnector",
  "type": "sink",
  "version": "1.4.1-SNAPSHOT"
 },
 {
  "class": "com.yugabyte.jdbc.JdbcSinkConnector",
  "type": "sink",
  "version": "1.4.1-SNAPSHOT"
 },
 {
  "class": "com.yugabyte.jdbc.JdbcSourceConnector",
  "type": "source",
  "version": "1.4.1-SNAPSHOT"
 },
```

## Verifying the Integration

You can verify the integration by producing a Record in Kafka.

The following produces a record into the orders topic using the Avro producer:

```sh
./bin/kafka-avro-console-producer \
--broker-list localhost:9092 --topic orders \
--property
value.schema='{"type":"record","name":"myrecord","fields":[{"name":"id","type":"int"},{"name":"product", "type": "string"}, {"name":"quantity", "type": "int"}, {"name":"price", "type": "float"}]}'
```

The console producer waits for input.

The next step is to copy and paste the record, such as the following sample, into the terminal and press Enter:

```nocopy.json
{"id": 999, "product": "foo", "quantity": 100, "price": 50}
```

Use the Avro consumer to verify that the message is published to the topic, as follows:

```sh
./bin/kafka-avro-console-consumer --bootstrap-server localhost:9092 --topic orders --from-beginning
{"id":999,"product":"foo","quantity":100,"price":50.0}
```

## Configuring the JDBC Sink Connector

You can configure the JDBC Sink connector as follows:

- Create a file named `yb-jdbc-sink-connector.properties` with the following content:

  ```json
  {
    "name": "yb-jdbc-sink",
    "config": {
      "connector.class": "com.yugabyte.jdbc.JdbcSinkConnector",
      "tasks.max": "2",
      "topics": "orders",
      "connection.urls":"jdbc:postgresql://localhost:5433/yugabyte",
      "connection.user":"yugabyte",
      "connection.password":"yugabyte",
      "mode":"UPSERT",
      "auto.create":"true"
    }
  }
  ```

- Load the JDBC Sink by executing the following command:

  ```sh
  curl -X POST -H "Content-Type: application/json" -d @/Users/me/confluent-6.1.1/etc/kafka/yb-jdbc-sink-connector.properties "localhost:8083/connectors"
  ```

- Query the YugabyteDB database to select all rows from the `orders` table, as follows:

  ```sql
  yugabyte=# select * from orders;
  ```

  The following output demonstrates that the `orders` table was automatically created and that it contains the record:

  ```output
  id | product | quantity | price
  ----+---------+----------+-------
  999 | foo     | 100      |  50
  (1 row)
  ```

## Configuring the YCQL Sink Connector

You can configure the YCQL Sink connector as follows:

- Create a file named `yb-cql-sink-connector.properties` with the following content:

  ```json
  {
    "name": "example-cql-sink",
    "config": {
      "connector.class": "com.datastax.oss.kafka.sink.CassandraSinkConnector",
      "tasks.max": "2",
      "topics": "orders",
      "contactPoints": "localhost",
      "loadBalancing.localDc": "datacenter1",
      "topic.orders.demo.orders.mapping": "id=value.id, product=value.product, quantity=value.quantity, price=value.price",
      "topic.orders.demo.orders.consistencyLevel": "LOCAL_QUORUM"
    }
  }
  ```

- In YugabyteDB, create a table with following schema:

  ```sql
  ycqlsh> create table demo.orders(id int primary key, product varchar, quantity int, price int);
  ```

- Load the connector by executing the following command:

  ```sh
  curl -X POST -H "Content-Type: application/json" -d @/Users/me/development/play/confluent/confluent-6.0.1/yb-cql-sink-connector.properties "localhost:8083/connectors"
  ```

- Query the YugabyteDB database to select all rows from the `orders` table, as follows:

  ```sql
  ycqlsh> select * from demo.orders;
  ```

  Expect to see the following output:

  ```output
   id | product | quantity | price
  ----+---------+----------+-------
  999 | foo     | 100      |  50
  (1 row)
  ```

## Shutting Down the Cluster

To shut down the YugabyteDB cluster, execute the following commands:

```sh
confluent local services stop
```

```sh
confluent local destroy
```
