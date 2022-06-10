---
title: Debezium and CDC in YugabyteDB
linkTitle: Debezium
description: Debezium is an open source distributed platform used to capture the changes in a database.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
aliases:
  - /preview/integrations/cdc/
section: INTEGRATIONS
menu:
  preview:
    identifier: cdc-debezium
    weight: 571
isTocNested: true
showAsideToc: true
---

Debezium is an open-source distributed platform used to capture the changes in a database.

## Get the Debezium connector for YugabyteDB

### Pull the connector from Docker

Using Docker, get the connector from Quay:

```sh
docker pull quay.io/yugabyte/debezium-connector:1.3.4-BETA
```

### Build the Debezium connector on your own

If you want to build the connector image yourself, follow the steps listed in the [README for debezium-connector-yugabytedb](https://github.com/yugabyte/debezium/blob/final-connector-ybdb/debezium-connector-yugabytedb2/README.md).

## Run Debezium locally

Follow the steps in this section to run CDC with Debezium on a local YugabyteDB cluster.

1. Start Zookeeper.

    ```sh
    docker run -it --rm --name zookeeper -p 2181:2181 -p 2888:2888 -p 3888:3888 debezium/zookeeper:1.6
    ```

1. Start Kafka.

    ```sh
    docker run -it --rm --name kafka -p 9092:9092 --link zookeeper:zookeeper debezium/kafka:1.6
    ```

1. Assign your computer's IP address to an environment variable.

    ```sh
    # macOS:
    export IP=$(ipconfig getifaddr en0)

    # Linux:
    export IP=$(hostname -i)
    ```

1. Start a cluster using yugabyted. Note that you need to run yugabyted with the IP of your machine; otherwise, it would consider localhost (which would be mapped to the docker host instead of your machine).

    ```sh
    ./bin/yugabyted start --listen $IP
    ```

1. Connect using ysqlsh and create a table:

    ```sh
    ./bin/ysqlsh -h $IP
    ```

    ```sql
    create table test (id int primary key, name text, days_worked bigint);
    ```

### Create a database stream ID

[`yb-admin`](../../../admin/yb-admin#change-data-capture-cdc-commands) is equipped with the commands to manage stream IDs for Change data capture. Use it to create a stream ID:

```sh
./bin/yb-admin --master_addresses ${IP}:7100 create_change_data_stream ysql.yugabyte
```

The above command will result an output similar to this:

```output
CDC Stream ID: d540f5e4890c4d3b812933cbfd703ed3
```

### Start Debezium

Launch the connector:

```sh
docker run -it --rm \
  --name connect -p 8083:8083 -e GROUP_ID=1 \
  -e CONFIG_STORAGE_TOPIC=my_connect_configs \
  -e OFFSET_STORAGE_TOPIC=my_connect_offsets \
  -e STATUS_STORAGE_TOPIC=my_connect_statuses \
  --link zookeeper:zookeeper --link kafka:kafka \
  quay.io/yugabyte/debezium-connector:1.3.4-BETA
```

Deploy the configuration for the connector:

```bash
curl -i -X POST -H "Accept:application/json" -H "Content-Type:application/json" \
  localhost:8083/connectors/ \
  -d '{
  "name": "ybconnector",
  "config": {
    "connector.class": "io.debezium.connector.yugabytedb.YugabyteDBConnector",
    "database.hostname":"'$IP'",
    "database.port":"5433",
    "database.master.addresses": "'$IP':7100",
    "database.user": "yugabyte",
    "database.password": "yugabyte",
    "database.dbname" : "yugabyte",
    "database.server.name": "dbserver1",
    "table.include.list":"public.test",
    "database.streamid":"d540f5e4890c4d3b812933cbfd703ed3",
    "snapshot.mode":"never"
  }
}'
```

For a list of all the configuration options we provide with the Debezium YugabyteDB connector, see the [configuration options](../../../explore/change-data-capture/debezium-connector-yugabytedb/).

{{< warning title="Warning" >}}

YugabyteDB's CDC implementation doesn't support DROP TABLE and TRUNCATE TABLE commands yet, and the behavior of these commands while streaming data from CDC is undefined. If you need to drop or truncate a table, delete the stream ID using [yb-admin](../../../admin/yb-admin/#change-data-capture-cdc-commands).

See [limitations](../../../explore/change-data-capture/#limitations) for more details on upcoming feature support.

{{< /warning >}}

### Start a Kafka Topic console consumer (optional)

```sh
docker run -it --rm --name consumer --link zookeeper:zookeeper --link kafka:kafka debezium/kafka:1.6 \
watch-topic -a dbserver1.public.test
```
