---
title: Get started with CDC in YugabyteDB
headerTitle: Get started
linkTitle: Get started
description: Get started with Change Data Capture in YugabyteDB.
headcontent: Get set up for using CDC in YugabyteDB
menu:
  preview:
    parent: explore-change-data-capture-logical-replication
    identifier: get-started
    weight: 20
type: docs
---

This tutorial demonstrates how to use Debezium to monitor a YugabyteDB database. As the data in the database changes, you will see the resulting event streams.

In this tutorial you will start the Debezium services, run a YugabyteDB instance with a simple example database, and use Debezium to monitor the database for changes.

**Prerequisites**
* Docker is installed and running.

This tutorial uses Docker and the Debezium container images to run the required services. You should use the latest version of Docker. For more information, see [the Docker Engine installation documentation](https://docs.docker.com/engine/installation/).

## Starting the services

Using Debezium requires three separate services: [Zookeeper](http://zookeeper.apache.org/), [Kafka](), and the Debezium connector service. In this tutorial, you will set up a single instance of each service using Docker and the Debezium container images.

To start the services needed for this tutorial, you must:
* [Start Zookeeper](#starting-zookeeper)
* [Start Kafka](#starting-kafka)
* [Start a YugabyteDB database](#starting-a-yugabytedb-database)
* Start Kafka Connect

### Starting Zookeeper

Zookeeper is the first service you must start.

**Procedure**

1. Open a terminal and use it to start Zookeeper in a container. This command runs a new container using version `2.5.2.Final` of the `debezium/zookeeper` image:

```sh
docker run -d --rm --name zookeeper -p 2181:2181 -p 2888:2888 -p 3888:3888 debezium/zookeeper:2.5.2.Final
```

### Starting Kafka

After starting Zookeeper, you can start Kafka in a new container.

**Procedure**

1. Open a new terminal and use it to start Kafka in a container. This command runs a new container using version `2.5.2.Final` of the `debezium/kafka` image:

```sh
docker run -d --rm --name kafka -p 9092:9092 --link zookeeper:zookeeper debezium/kafka:2.5.2.Final
```

{{< note title="Note" >}}

In this tutorial, you will always connect to Kafka from within a Docker container. Any of these containers can communicate with the `kafka` container by linking to it. If you needed to connect to Kafka from outside of a Docker container, you would have to set the `-e` option to advertise the Kafka address through the Docker host (`-e ADVERTISED_HOST_NAME=` followed by either the IP address or resolvable host name of the Docker host).

{{< /note >}}

### Starting a YugabyteDB database

At this point, you have started Zookeeper and Kafka, but you still need a database server from which Debezium can capture changes. In this procedure, you will start a YugabyteDB instance with an example database. Follow the [Quick Start](../../../quick-start) to start an instance with yugabyted.

{{< note title="Note" >}}

You need to start the database on an IP that is resolvable by the docker containers. If you use the localhost address i.e. `127.0.0.1` then if we deploy the connectors in the docker containers, they will not be able to talk to the database and will keep trying to connect to `127.0.0.1` inside the container. You can use the [--advertise_address option for yugabyted](../../../reference/configuration/yugabyted#flags-8) to specify the IP you want to start your database instance.

For example, linux users can use:

```sh
./bin/yugabyted start --advertise_address $(hostname -i)
```

{{< /note >}}

#### Starting a YSQL command line client


After starting YugabyteDB, you start a YugabyteDB command line client `ysqlsh` so that you can create your database.

**Procedure**

1. Connect the client to the database process running on the IP you specified while starting up the database instance.

```sh
./bin/ysqlsh -h <ip-of-your-machine>
```

2. Verify that `ysqlsh` started. You should see output similar to the following:

```sh
ysqlsh (11.2-YB-2.21.1.0-b0)
Type "help" for help.

yugabyte=#
```

3. Load the schema of the sample tables:

```sql
yugabyte=# \i share/schema.sql
CREATE TABLE
CREATE TABLE
CREATE TABLE
CREATE TABLE
```

4. List the tables

```sql
yugabyte=# \d
               List of relations
 Schema |      Name       |   Type   |  Owner
--------+-----------------+----------+----------
 public | orders          | table    | yugabyte
 public | orders_id_seq   | sequence | yugabyte
 public | products        | table    | yugabyte
 public | products_id_seq | sequence | yugabyte
 public | reviews         | table    | yugabyte
 public | reviews_id_seq  | sequence | yugabyte
 public | users           | table    | yugabyte
 public | users_id_seq    | sequence | yugabyte
(8 rows)
```

5. Load data in one of the tables and verify the count

```sql
yugabyte=# \i share/products.sql

yugabyte=# select count(*) from products;
 count
-------
   200
(1 row)
```

### Starting Kafka Connect

After starting YugabyteDB, you start the Kafka Connect service. This service exposes a REST API to manage the Debezium YugabyteDB connector.

**Procedure**

1. Open a new terminal, and use it to start the Kafka Connect service in a container. This command runs a new container using the `dz.2.5.2.yb.2024.1.SNAPSHOT.1` version of the `quay.io/yugabyte/ybdb-debezium` image:

```sh
docker run -it --rm --name connect -p 8083:8083 -p 1976:1976 -e GROUP_ID=1 -e CONFIG_STORAGE_TOPIC=my_connect_configs -e OFFSET_STORAGE_TOPIC=my_connect_offsets -e STATUS_STORAGE_TOPIC=my_connect_statuses -e CLASSPATH=/kafka/connect/ --link zookeeper:zookeeper --link kafka:kafka quay.io/yugabyte/ybdb-debezium:dz.2.5.2.yb.2024.1.SNAPSHOT.1
```

2. Verify that Kafka Connect started and is ready to accept connections. You should see output similar to the following:

```sh
...
2024-07-19 12:04:33,044 INFO   ||  Kafka version: 3.6.1   [org.apache.kafka.common.utils.AppInfoParser]
...
2024-07-19 12:04:33,661 INFO   ||  [Worker clientId=connect-1, groupId=1] Starting connectors and tasks using config offset -1   [org.apache.kafka.connect.runtime.distributed.DistributedHerder]
2024-07-19 12:04:33,661 INFO   ||  [Worker clientId=connect-1, groupId=1] Finished starting connectors and tasks   [org.apache.kafka.connect.runtime.distributed.DistributedHerder]
```

3. Use the Kafka Connect REST API to check the status of the Kafka Connect service. Kafka Connect exposes a REST API to manage Debezium connectors. To communicate with the Kafka Connect service, you can use the `curl` command to send API requests to port 8083 of the Docker host (which you mapped to port 8083 in the `connect` container when you started Kafka Connect). Open a new terminal and check the status of the Kafka Connect service:

```sh
$ curl -H "Accept:application/json" localhost:8083/

{"version":"3.6.1","commit":"5e3c2b738d253ff5","kafka_cluster_id":"kafka-cluster-id"}
```

{{<note title="Note">}}

These commands use `localhost`. If you are using a non-native Docker platform (such as Docker Toolbox), replace `localhost` with the IP address of your Docker host.

{{< /note >}}

## Deploying the YugabyteDB connector

After starting the Debezium and YugabyteDB service, you are ready to deploy the YugabyteDB connector. To deploy the connector, you must:

* [Register the YugabyteDB connector to monitor the `yugabyte` database](#registering-a-connector-to-monitor-yugabyte-database)
* Watch the connector start

### Registering a connector to monitor `yugabyte` database

By registering the Debezium YugabyteDB connector, the connector will start monitoring the YugabyteDB database's table `products`. When a row in the table changes, Debezium generates a change event.

{{< note title="Note" >}}

In a production environment, you would typically either use the Kafka tools to manually create the necessary topics, including specifying the number of replicas, or you’d use the Kafka Connect mechanism for customizing the settings of [auto-created](https://debezium.io/documentation/reference/2.5/configuration/topic-auto-create-config.html) topics. However, for this tutorial, Kafka is configured to automatically create the topics with just one replica.

{{< /note >}}

**Procedure**

1. Review the configuration of the Debezium YugabyteDB connector that you will register. Before registering the connector, you should be familiar with its configuration. In the next step, you will register the following connector:

```json
{
  "name": "ybconnector",
  "config": {
    "tasks.max":"1",
    "connector.class": "io.debezium.connector.postgresql.YugabyteDBConnector",
    "database.hostname":"'$(hostname -i)'",
    "database.port":"5433",
    "database.user": "yugabyte",
    "database.password":"yugabyte",
    "database.dbname":"yugabyte",
    "topic.prefix":"dbserver1",
    "snapshot.mode":"initial",
    "table.include.list":"public.products",
    "plugin.name":"yboutput",
    "slot.name":"yb_replication_slot"
  }
}
```

* `name` - The name of the connector.
* `config` - The connector's configuration.
* `database.hostname` - The database host, which is the IP of the machine running YugabyteDB. If YugabyteDB were running on a normal network, you would specify the IP address or resolvable host name for this value.
* `topic.prefix` - A unique topic prefix. This name will be used as the prefix for all Kafka topics.
* `table.include.list` - Only changes in the table `products` of the schema `public` will be detected.
* `plugin.name` - [Plugin](./overview#output-plugin) to be used for replication.
* `slot.name` - Name of the [replication slot](./overview#replication-slot).

For more information, see [YugabyteDB connector configuration properties](./yugabtyedb-connector#connector-properties).

2. Open a new terminal, and use the `curl` command to register the Debezium YugabyteDB connector. This command uses the Kafka Connect service’s API to submit a `POST` request against the `/connectors` resource with a `JSON` document that describes the new connector (called `ybconnector`).

```sh
curl -i -X POST -H "Accept:application/json" -H "Content-Type:application/json" localhost:8083/connectors/ -d '{
  "name": "ybconnector",
  "config": {
    "tasks.max":"1",
    "connector.class": "io.debezium.connector.postgresql.YugabyteDBConnector",
    "database.hostname":"'$(hostname -i)'",
    "database.port":"5433",
    "database.user": "yugabyte",
    "database.password":"yugabyte",
    "database.dbname":"yugabyte",
    "topic.prefix":"dbserver1",
    "snapshot.mode":"initial",
    "table.include.list":"public.products",
    "plugin.name":"yboutput",
    "slot.name":"yb_replication_slot"
  }
}'
```

{{< note title="Note" >}}

Windows users may need to escape the double-quotes.

{{< /note >}}

3. Verify that `ybconnector` is included in the list of connectors:

```sh
$ curl -H "Accept:application/json" localhost:8083/connectors/

["ybconnector"]
```

### Watching the connector start

When you register a connector, it generates a large amount of log output in the Kafka Connect container. By reviewing this output, you can better understand the process that the connector goes through from the time it is created until it begins reading the change events.

After registering the `ybconnector` connector, you can review the log output in the Kafka Connect container (`connect`) to track the connector’s status.

Kafka Connect reports some "errors". However, you can safely ignore these warnings: these messages just mean that new Kafka topics were created and that Kafka had to assign a new leader for each one:

```sh
2021-11-30 01:38:45,555 WARN   ||  [Producer clientId=connector-producer-inventory-connector-0] Error while fetching metadata with correlation id 3 : {dbserver1=LEADER_NOT_AVAILABLE}   [org.apache.kafka.clients.NetworkClient]
2021-11-30 01:38:45,691 WARN   ||  [Producer clientId=connector-producer-inventory-connector-0] Error while fetching metadata with correlation id 9 : {dbserver1.public.orders=LEADER_NOT_AVAILABLE}   [org.apache.kafka.clients.NetworkClient]
2021-11-30 01:38:45,813 WARN   ||  [Producer clientId=connector-producer-inventory-connector-0] Error while fetching metadata with correlation id 13 : {dbserver1.public.users=LEADER_NOT_AVAILABLE}   [org.apache.kafka.clients.NetworkClient]
2021-11-30 01:38:45,927 WARN   ||  [Producer clientId=connector-producer-inventory-connector-0] Error while fetching metadata with correlation id 18 : {dbserver1.public.products=LEADER_NOT_AVAILABLE}   [org.apache.kafka.clients.NetworkClient]
2021-11-30 01:38:46,043 WARN   ||  [Producer clientId=connector-producer-inventory-connector-0] Error while fetching metadata with correlation id 22 : {dbserver1.public.reviews=LEADER_NOT_AVAILABLE}   [org.apache.kafka.clients.NetworkClient]
```

