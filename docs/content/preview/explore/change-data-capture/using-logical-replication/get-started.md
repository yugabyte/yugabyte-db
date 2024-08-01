---
title: Get started with CDC logical replication in YugabyteDB
headerTitle: Get started
linkTitle: Get started
description: Get started with Change Data Capture in YugabyteDB.
headcontent: Start using CDC with logical replication
menu:
  preview:
    parent: explore-change-data-capture-logical-replication
    identifier: get-started
    weight: 20
type: docs
---

To get started streaming data change events from a YugabyteDB database using a replication slot, you can use either of the following client options:

- [pg_recvlogical](#get-started-with-pg_recvlogical)
- [YugabyteDB connector](#get-started-with-yugabytedb-connector)

{{< note title="Note" >}}

CDC via logical replication is supported in YugabyteDB starting from version 2024.1.1.

{{< /note >}}

## Get started with pg_recvlogical

pg_recvlogical is a command-line tool provided by PostgreSQL for interacting with the logical replication feature. It is specifically used to receive changes from the database using logical replication slots.

YugabyteDB provides the pg_recvlogical binary in the `<yugabyte-db-dir>/postgres/bin/` directory, which is inherited and based on PostgreSQL 11.2. Although PostgreSQL also offers a pg_recvlogical binary, you are strongly advised to use the YugabyteDB version to avoid compatibility issues.

### Set up pg_recvlogical

To set up pg_recvlogical, create and start the local cluster by running the following command from your YugabyteDB home directory:

```sh
./bin/yugabyted start \
  --advertise_address=127.0.0.1 \
  --base_dir="${HOME}/var/node1" \
  --tserver_flags="allowed_preview_flags_csv={cdcsdk_enable_dynamic_table_support},cdcsdk_enable_dynamic_table_support=true,cdcsdk_publication_list_refresh_interval_secs=2"
```

#### Create tables

1. Use ysqlsh to connect to the default `yugabyte` database with the default superuser `yugabyte`, as follows:

    ```sh
    bin/ysqlsh -h 127.0.0.1 -U yugabyte -d yugabyte
    ```

1. In the `yugabyte` database, create a table `employees`.

    ```sql
    CREATE TABLE employees (
        employee_id SERIAL PRIMARY KEY,
        name VARCHAR(255),
        email VARCHAR(255),
        department_id INTEGER
    );
    ```

#### Create a Replication slot

Create a logical replication slot named `test_logical_replication_slot` using the `test_decoding` output plugin via the following function:

```sql
SELECT *
FROM pg_create_logical_replication_slot('test_logical_replication_slot', 'test_decoding');
```

Expected output after running the command that indicates successful creation of the slot:

```output
           slot_name           | lsn
-------------------------------+-----
 test_logical_replication_slot | 0/2
```

#### Configure and start pg_recvlogical

The pg_recvlogical binary can be found under `<yugabyte-db-dir>/postgres/bin/`.

Open a new shell and start pg_recvlogical to connect to the `yugabyte` database with the superuser `yugabyte` and replicate changes using the replication slot you created as follows:

```sh
./pg_recvlogical -d yugabyte \
  -U yugabyte \
  -h 127.0.0.1 \
  --slot test_logical_replication_slot \
  --start \
  -f -
```

Any changes that get replicated are printed to stdout.

For more pg_recvlogical configurations, refer to the PostgreSQL [pg_recvlogical](https://www.postgresql.org/docs/11/app-pgrecvlogical.html) documentation.

#### Verify Replication

Return to the shell where ysqlsh is running. Perform DMLs on the `employees` table.

```sql
BEGIN;

INSERT INTO employees (name, email, department_id)
VALUES ('Alice Johnson', 'alice@example.com', 1);

INSERT INTO employees (name, email, department_id)
VALUES ('Bob Smith', 'bob@example.com', 2);

COMMIT;
```

Expected output observed on stdout where pg_recvlogical is running:

```output
BEGIN 2
table public.employees: INSERT: employee_id[integer]:1 name[character varying]:'Alice Johnson' email[character varying]:'alice@example.com' department_id[integer]:1
table public.employees: INSERT: employee_id[integer]:2 name[character varying]:'Bob Smith' email[character varying]:'bob@example.com' department_id[integer]:2
COMMIT 2
```

#### Add tables (Dynamic table addition)

You can add a new table to the `yugabyte` database and any DMLs performed on the new table would also be replicated to pg_recvlogical.

1. In the `yugabyte` database, create a new table `projects`:

    ```sql
    CREATE TABLE projects (
      project_id SERIAL PRIMARY KEY,
      name VARCHAR(255),
      description TEXT
    );
    ```

2. Perform DMLs on the `projects` table:

    ```sql
    INSERT INTO projects (name, description)
    VALUES ('Project A', 'Description of Project A');
    ```

Expected output observed on stdout where pg_recvlogical is running:

```output
BEGIN 3
table public.projects: INSERT: project_id[integer]:1 name[character varying]:'Project A' description[text]:'Description of Project A'
COMMIT 3
```

{{% explore-cleanup-local %}}

## Get started with YugabyteDB connector

This tutorial demonstrates how to use Debezium to monitor a YugabyteDB database. As the data in the database changes, you will see the resulting event streams.

In this tutorial you will start the Debezium services, run a YugabyteDB instance with a basic example database, and use Debezium to monitor the database for changes.

**Prerequisites**

- Docker is installed and running.

    This tutorial uses Docker and the Debezium container images to run the required services. You should use the latest version of Docker. For more information, see the [Docker Engine installation](https://docs.docker.com/engine/installation/) documentation.

### Start the services

Using Debezium requires three separate services: [Zookeeper](http://zookeeper.apache.org/), [Kafka](https://kafka.apache.org), and the Debezium connector service.

In this tutorial, you will set up a single instance of each service using Docker and the Debezium container images.

To start the services needed for this tutorial, you must:

- [Start Zookeeper](#start-zookeeper)
- [Start Kafka](#start-kafka)
- [Start a YugabyteDB database](#start-a-yugabytedb-database)
- [Start Kafka Connect](#start-kafka-connect)

#### Start Zookeeper

Zookeeper is the first service you must start.

1. Open a terminal and use it to start Zookeeper in a container. This command runs a new container using version `2.5.2.Final` of the `debezium/zookeeper` image:

```sh
docker run -d --rm --name zookeeper -p 2181:2181 -p 2888:2888 -p 3888:3888 debezium/zookeeper:2.5.2.Final
```

#### Start Kafka

After starting Zookeeper, you can start Kafka in a new container.

1. Open a new terminal and use it to start Kafka in a container. This command runs a new container using version `2.5.2.Final` of the `debezium/kafka` image:

```sh
docker run -d --rm --name kafka -p 9092:9092 --link zookeeper:zookeeper debezium/kafka:2.5.2.Final
```

{{< note title="Note" >}}

In this tutorial, you will always connect to Kafka from in a Docker container. Any of these containers can communicate with the `kafka` container by linking to it. If you need to connect to Kafka from outside of a Docker container, you have to set the `-e` option to advertise the Kafka address through the Docker host (`-e ADVERTISED_HOST_NAME=` followed by either the IP address or resolvable host name of the Docker host).

{{< /note >}}

#### Start a YugabyteDB database

At this point, you have started Zookeeper and Kafka, but you still need a database server from which Debezium can capture changes. In this procedure, you start a YugabyteDB instance with an example database. Follow the [Quick Start](../../../../quick-start) to start an instance using yugabyted.

{{< note title="Note" >}}

You need to start the database on an IP that is resolvable by the docker containers. If you use the localhost address (that is, `127.0.0.1`) then if you deploy the connectors in the docker containers, they won't be able to talk to the database and will keep trying to connect to `127.0.0.1` inside the container. Use the [--advertise_address option for yugabyted](../../../../reference/configuration/yugabyted#flags-8) to specify the IP you want to start your database instance.

For example, Linux users can use the following:

```sh
./bin/yugabyted start --advertise_address $(hostname -i)
```

{{< /note >}}

##### Use the YSQL command line client

After starting YugabyteDB, use ysqlsh to create your database:

1. Connect the client to the database process running on the IP you specified when you started up the database instance.

    ```sh
    ./bin/ysqlsh -h <ip-of-your-machine>
    ```

    You should see output similar to the following:

    ```output
    ysqlsh (11.2-YB-2.21.1.0-b0)
    Type "help" for help.

    yugabyte=#
    ```

1. Load the schema of the sample tables:

    ```sql
    yugabyte=# \i share/schema.sql
    CREATE TABLE
    CREATE TABLE
    CREATE TABLE
    CREATE TABLE
    ```

1. List the tables

    ```sql
    yugabyte=# \d
    ```

    ```output
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

1. Load data in one of the tables and verify the count.

    ```sql
    yugabyte=# \i share/products.sql
    ```

    ```output
    yugabyte=# select count(*) from products;
    count
    -------
      200
    (1 row)
    ```

#### Start Kafka Connect

After starting YugabyteDB, you start the Kafka Connect service. This service exposes a REST API to manage the YugabyteDB connector.

1. Open a new terminal, and use it to start the Kafka Connect service in a container.

    The following command runs a new container using the `dz.2.5.2.yb.2024.1.SNAPSHOT.1` version of the `quay.io/yugabyte/ybdb-debezium` image:

    ```sh
    docker run -it --rm --name connect -p 8083:8083 -p 1976:1976 -e GROUP_ID=1 -e CONFIG_STORAGE_TOPIC=my_connect_configs -e OFFSET_STORAGE_TOPIC=my_connect_offsets -e STATUS_STORAGE_TOPIC=my_connect_statuses -e CLASSPATH=/kafka/connect/ --link zookeeper:zookeeper --link kafka:kafka quay.io/yugabyte/ybdb-debezium:dz.2.5.2.yb.2024.1.SNAPSHOT.1
    ```

1. Verify that Kafka Connect started and is ready to accept connections. You should see output similar to the following:

    ```output
    ...
    2024-07-19 12:04:33,044 INFO   ||  Kafka version: 3.6.1   [org.apache.kafka.common.utils.AppInfoParser]
    ...
    2024-07-19 12:04:33,661 INFO   ||  [Worker clientId=connect-1, groupId=1] Starting connectors and tasks using config offset -1   [org.apache.kafka.connect.runtime.distributed.DistributedHerder]
    2024-07-19 12:04:33,661 INFO   ||  [Worker clientId=connect-1, groupId=1] Finished starting connectors and tasks   [org.apache.kafka.connect.runtime.distributed.DistributedHerder]
    ```

1. Use the Kafka Connect REST API to check the status of the Kafka Connect service.

    Kafka Connect exposes a REST API to manage Debezium connectors. To communicate with the Kafka Connect service, you can use the `curl` command to send API requests to port 8083 of the Docker host (which you mapped to port 8083 in the `connect` container when you started Kafka Connect).

    Open a new terminal and check the status of the Kafka Connect service:

    ```sh
    $ curl -H "Accept:application/json" localhost:8083/

    {"version":"3.6.1","commit":"5e3c2b738d253ff5","kafka_cluster_id":"kafka-cluster-id"}
    ```

{{< note title="Note" >}}

These commands use `localhost`. If you are using a non-native Docker platform (such as Docker Toolbox), replace `localhost` with the IP address of your Docker host.

{{< /note >}}

### Deploy the YugabyteDB connector

After starting the Debezium and YugabyteDB service, you are ready to deploy the YugabyteDB connector. To deploy the connector, you must:

- [Register the YugabyteDB connector to monitor the `yugabyte` database](#register-a-connector-to-monitor-yugabyte-database)
- Watch the connector start

#### Register a connector to monitor `yugabyte` database

By registering the YugabyteDB connector, the connector will start monitoring the YugabyteDB database's table `products`. When a row in the table changes, Debezium generates a change event.

{{< note title="Note" >}}

In a production environment, you would typically either use the Kafka tools to manually create the necessary topics, including specifying the number of replicas, or you would use the Kafka Connect mechanism for customizing the settings of [auto-created](https://debezium.io/documentation/reference/2.5/configuration/topic-auto-create-config.html) topics. However, for this tutorial, Kafka is configured to automatically create the topics with just one replica.

{{< /note >}}

1. Review the configuration of the YugabyteDB connector that you will register. Before registering the connector, you should be familiar with its configuration. In the next step, you will register the following connector:

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

    - `name` - The name of the connector.
    - `config` - The connector's configuration.
    - `database.hostname` - The database host, which is the IP of the machine running YugabyteDB. If YugabyteDB were running on a normal network, you would specify the IP address or resolvable host name for this value.
    - `topic.prefix` - A unique topic prefix. This name will be used as the prefix for all Kafka topics.
    - `table.include.list` - Only changes in the table `products` of the schema `public` will be detected.
    - `plugin.name` - [Plugin](../key-concepts/#output-plugin) to be used for replication.
    - `slot.name` - Name of the [replication slot](../key-concepts/#replication-slot).

    For more information, see [YugabyteDB connector configuration properties](../yugabytedb-connector/#connector-properties).

1. Open a new terminal and use the `curl` command to register the YugabyteDB connector.

    This command uses the Kafka Connect service API to submit a `POST` request against the `/connectors` resource with a `JSON` document that describes the new connector (called `ybconnector`).

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

1. Verify that `ybconnector` is included in the list of connectors:

    ```sh
    $ curl -H "Accept:application/json" localhost:8083/connectors/

    ["ybconnector"]
    ```

#### Watch the connector start

When you register a connector, it generates a large amount of log output in the Kafka Connect container. By reviewing this output, you can better understand the process that the connector goes through from the time it is created until it begins reading the change events.

After registering the `ybconnector` connector, you can review the log output in the Kafka Connect container (`connect`) to track the connector's status.

Kafka Connect reports some "errors". However, you can safely ignore these warnings: these messages just mean that new Kafka topics were created and that Kafka had to assign a new leader for each one:

```output
2021-11-30 01:38:45,555 WARN   ||  [Producer clientId=connector-producer-inventory-connector-0] Error while fetching metadata with correlation id 3 : {dbserver1=LEADER_NOT_AVAILABLE}   [org.apache.kafka.clients.NetworkClient]
2021-11-30 01:38:45,691 WARN   ||  [Producer clientId=connector-producer-inventory-connector-0] Error while fetching metadata with correlation id 9 : {dbserver1.public.orders=LEADER_NOT_AVAILABLE}   [org.apache.kafka.clients.NetworkClient]
2021-11-30 01:38:45,813 WARN   ||  [Producer clientId=connector-producer-inventory-connector-0] Error while fetching metadata with correlation id 13 : {dbserver1.public.users=LEADER_NOT_AVAILABLE}   [org.apache.kafka.clients.NetworkClient]
2021-11-30 01:38:45,927 WARN   ||  [Producer clientId=connector-producer-inventory-connector-0] Error while fetching metadata with correlation id 18 : {dbserver1.public.products=LEADER_NOT_AVAILABLE}   [org.apache.kafka.clients.NetworkClient]
2021-11-30 01:38:46,043 WARN   ||  [Producer clientId=connector-producer-inventory-connector-0] Error while fetching metadata with correlation id 22 : {dbserver1.public.reviews=LEADER_NOT_AVAILABLE}   [org.apache.kafka.clients.NetworkClient]
```

### View change events

After deploying the YugabyteDB connector, it starts monitoring the `yugabyte` database for data change events.

For this tutorial, you will explore the `dbserver1.public.products` topic.

#### View a change event

Open a new terminal, and use it to start the watch-topic utility to watch the `dbserver1.public.products` topic from the beginning of the topic.

The following command runs the `watch-topic` utility in a new container using the `2.5.2.Final` version of the `debezium/kafka` image:

```sh
docker run -it --rm --name consumer --link zookeeper:zookeeper --link kafka:kafka debezium/kafka:2.5.2.Final watch-topic -a dbserver1.public.products
```

The `watch-topic` utility returns the event records from the `products` table. There will be 200 events, one for each row in the table which was snapshotted. Each event is formatted in JSON, because that is how you configured the Kafka Connect service. There are two JSON documents for each event: one for the key, and one for the value.

You should see output similar to the following:

```output.json
Using ZOOKEEPER_CONNECT=172.17.0.2:2181
Using KAFKA_ADVERTISED_LISTENERS=PLAINTEXT://172.17.0.7:9092
Using KAFKA_BROKER=172.17.0.3:9092
Contents of topic dbserver1.public.products:
...
{"schema":{"type":"struct","fields":[{"type":"struct","fields":[{"type":"struct","fields":[{"type":"int64","optional":false,"default":0,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":false,"name":"id","field":"id"},{"type":"struct","fields":[{"type":"int64","optional":true,"name":"io.debezium.time.MicroTimestamp","version":1,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":true,"name":"created_at","field":"created_at"},{"type":"struct","fields":[{"type":"string","optional":true,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":true,"name":"category","field":"category"},{"type":"struct","fields":[{"type":"string","optional":true,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":true,"name":"ean","field":"ean"},{"type":"struct","fields":[{"type":"double","optional":true,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":true,"name":"price","field":"price"},{"type":"struct","fields":[{"type":"int32","optional":true,"default":5000,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":true,"name":"quantity","field":"quantity"},{"type":"struct","fields":[{"type":"double","optional":true,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":true,"name":"rating","field":"rating"},{"type":"struct","fields":[{"type":"string","optional":true,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":true,"name":"title","field":"title"},{"type":"struct","fields":[{"type":"string","optional":true,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":true,"name":"vendor","field":"vendor"}],"optional":true,"name":"dbserver1.public.products.Value","field":"before"},{"type":"struct","fields":[{"type":"struct","fields":[{"type":"int64","optional":false,"default":0,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":false,"name":"id","field":"id"},{"type":"struct","fields":[{"type":"int64","optional":true,"name":"io.debezium.time.MicroTimestamp","version":1,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":true,"name":"created_at","field":"created_at"},{"type":"struct","fields":[{"type":"string","optional":true,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":true,"name":"category","field":"category"},{"type":"struct","fields":[{"type":"string","optional":true,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":true,"name":"ean","field":"ean"},{"type":"struct","fields":[{"type":"double","optional":true,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":true,"name":"price","field":"price"},{"type":"struct","fields":[{"type":"int32","optional":true,"default":5000,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":true,"name":"quantity","field":"quantity"},{"type":"struct","fields":[{"type":"double","optional":true,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":true,"name":"rating","field":"rating"},{"type":"struct","fields":[{"type":"string","optional":true,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":true,"name":"title","field":"title"},{"type":"struct","fields":[{"type":"string","optional":true,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":true,"name":"vendor","field":"vendor"}],"optional":true,"name":"dbserver1.public.products.Value","field":"after"},{"type":"struct","fields":[{"type":"string","optional":false,"field":"version"},{"type":"string","optional":false,"field":"connector"},{"type":"string","optional":false,"field":"name"},{"type":"int64","optional":false,"field":"ts_ms"},{"type":"string","optional":true,"name":"io.debezium.data.Enum","version":1,"parameters":{"allowed":"true,last,false,incremental"},"default":"false","field":"snapshot"},{"type":"string","optional":false,"field":"db"},{"type":"string","optional":true,"field":"sequence"},{"type":"string","optional":false,"field":"schema"},{"type":"string","optional":false,"field":"table"},{"type":"int64","optional":true,"field":"txId"},{"type":"int64","optional":true,"field":"lsn"},{"type":"int64","optional":true,"field":"xmin"}],"optional":false,"name":"io.debezium.connector.postgresql.Source","field":"source"},{"type":"string","optional":false,"field":"op"},{"type":"int64","optional":true,"field":"ts_ms"},{"type":"struct","fields":[{"type":"string","optional":false,"field":"id"},{"type":"int64","optional":false,"field":"total_order"},{"type":"int64","optional":false,"field":"data_collection_order"}],"optional":true,"name":"event.block","version":1,"field":"transaction"}],"optional":false,"name":"dbserver1.public.products.Envelope","version":1},"payload":{"before":null,"after":{"id":{"value":147,"set":true},"created_at":{"value":1500306107286000,"set":true},"category":{"value":"Doohickey","set":true},"ean":{"value":"6590063715","set":true},"price":{"value":44.4315141414441,"set":true},"quantity":{"value":5000,"set":true},"rating":{"value":4.6,"set":true},"title":{"value":"Mediocre Wool Toucan","set":true},"vendor":{"value":"Bradtke, Wilkinson and Reilly","set":true}},"source":{"version":"dz.2.5.2.yb.2024.1-SNAPSHOT","connector":"postgresql","name":"dbserver1","ts_ms":1721400304248,"snapshot":"true","db":"yugabyte","sequence":"[null,\"2\"]","schema":"public","table":"products","txId":2,"lsn":2,"xmin":null},"op":"r","ts_ms":1721400309609,"transaction":null}}
...
```

{{< note title="Note" >}}

This utility keeps watching the topic, so any new events will automatically appear as long as the utility is running.

{{< /note >}}

#### Update the database and view the update event

Now that you have seen how the YugabyteDB connector captured the create events in the `yugabyte` database, change one of the records and see how the connector captures it.

By completing this procedure, you will learn how to find details about what changed in a database commit, and how you can compare change events to determine when the change occurred in relation to other changes.

1. In the terminal that is running ysqlsh, run the following statement:

    ```sql
    update products set title = 'Enormous Granite Shiny Shoes' where id = 22;
    ```

1. View the updated `products` table:

    ```sql
    yugabyte=# select * from products where id = 22;
    ```

    ```output
    id |       created_at        | category |      ean      |      price       | quantity | rating |            title             |          vendor
    ----+-------------------------+----------+---------------+------------------+----------+--------+------------------------------+---------------------------
    22 | 2017-11-24 20:14:28.415 | Gizmo    | 7595223735110 | 21.4245199604423 |     5000 |    4.2 | Enormous Granite Shiny Shoes | Mayer, Kiehn and Turcotte
    (1 row)
    ```

1. Switch to the terminal running `watch-topic` to see a new event.

    By changing a record in the `products` table, the YugabyteDB connector generated a new event.

    The details for the payload of the *update* event will look similar to the following (formatted for readability):

    ```json
    {
      "before": null,
      "after": {
        "id": {
          "value": 22,
          "set": true
        },
        "created_at": null,
        "category": null,
        "ean": null,
        "price": null,
        "quantity": null,
        "rating": null,
        "title": {
          "value": "Enormous Granite Shiny Shoes",
          "set": true
        },
        "vendor": null
      }
    }
    ```

Note that the fields which were not updated are coming out as `null`. This is because the [REPLICA IDENTITY](../key-concepts/#replica-identity) of the table is `CHANGE` by default, where you only send the values of the updated columns in the change event.

#### Delete a row and view the delete event

1. In the terminal that is running ysqlsh, run the following statement:

    ```sql
    delete from products where id = 22;
    ```

1. Switch to the terminal running `watch-topic` to see two new events. By deleting a row in the `products` table, the YugabyteDB connector generated 2 new events.

    The details for the payload of the first event will look similar to the following (formatted for readability):

    ```json
    {
      "before": {
        "id": {
          "value": 22,
          "set": true
        },
        "created_at": {
          "value": null,
          "set": true
        },
        "category": {
          "value": null,
          "set": true
        },
        "ean": {
          "value": null,
          "set": true
        },
        "price": {
          "value": null,
          "set": true
        },
        "quantity": {
          "value": 5000,
          "set": true
        },
        "rating": {
          "value": null,
          "set": true
        },
        "title": {
          "value": null,
          "set": true
        },
        "vendor": {
          "value": null,
          "set": true
        }
      },
      "after": null
    }
    ```

The second event will have a *key* but the *value* will be `null`; that is a [tombstone event](../yugabytedb-connector/#tombstone-events) generated by the YugabyteDB connector.

### Clean up

After you are finished with the tutorial, you can use Docker to stop all of the running containers.

Run the following command:

```sh
docker stop zookeeper kafka connect consumer
```

Docker stops each container. Because you used the `--rm` option when you started them, Docker also removes them.
