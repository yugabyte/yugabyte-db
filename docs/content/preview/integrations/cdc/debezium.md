---
title: Debezium and CDC in YugabyteDB
linkTitle: Debezium
description: Using Debezium for CDC in YugabyteDB.
badges: ea
aliases:
  - /preview/integrations/cdc/
menu:
  preview_integrations:
    identifier: cdc-debezium
    parent: data-integration
    weight: 571
type: docs
---

Debezium is an open-source distributed platform used to capture the changes in a database. The [YugabyteDB gRPC Connector](../../../explore/change-data-capture/using-yugabytedb-grpc-replication/) is based on the Debezium Connector, and captures row-level changes in a YugabyteDB database's schemas.

## Get the YugabyteDB gRPC Connector

Using Docker, you can get the connector from Quay:

```sh
docker pull quay.io/yugabyte/debezium-connector:latest
```

If you want to build the connector image yourself, follow the steps in the [README for debezium-connector-yugabytedb](https://github.com/yugabyte/debezium-connector-yugabytedb/blob/main/README.md).

## Run Debezium locally

Use the following steps to run change data capture (CDC) with Debezium on a local YugabyteDB cluster:

1. Start Zookeeper.

    ```sh
    docker run -it --rm --name zookeeper -p 2181:2181 -p 2888:2888 -p 3888:3888 debezium/zookeeper:1.9
    ```

1. Start Kafka.

    ```sh
    docker run -it --rm --name kafka -p 9092:9092 --link zookeeper:zookeeper debezium/kafka:1.9
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

[`yb-admin`](../../../admin/yb-admin#change-data-capture-cdc-commands) is equipped with commands to manage stream IDs for CDC. Use it to create a stream ID:

```sh
./bin/yb-admin --master_addresses ${IP}:7100 create_change_data_stream ysql.yugabyte
```

You should see output similar to the following:

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
  quay.io/yugabyte/debezium-connector:latest
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

### Use SSL

If you have an SSL-enabled cluster, you need to provide the path to the root certificate in the `database.sslrootcert` configuration property.

Do the following:

1. Copy the certificate file to your Docker container (assuming that the file exists on the root directory of your machine):

    ```sh
    docker cp ~/root.crt connect:/kafka/
    ```

1. Deploy the connector configuration:

    ```sh
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
        "snapshot.mode":"never",
        "database.sslrootcert":"/kafka/root.crt"
      }
    }'
    ```

For a list of all the configuration options provided with the YugabyteDB gRPC Connector, see [Connector configuration properties](../../../explore/change-data-capture/using-yugabytedb-grpc-replication/debezium-connector-yugabytedb/#connector-configuration-properties).

{{< tip title="TRUNCATE tables when CDC is enabled" >}}

By default, the YugabyteDB CDC implementation does not allow you to TRUNCATE a table while an active CDC stream is present on the namespace. To allow truncating tables while CDC is active, set the [enable_truncate_cdcsdk_table](../../../reference/configuration/yb-tserver/#enable-truncate-cdcsdk-table) flag to true.

{{< /tip >}}

### Start a Kafka Topic console consumer (optional)

```sh
docker run -it --rm --name consumer --link zookeeper:zookeeper --link kafka:kafka debezium/kafka:1.9 \
watch-topic -a dbserver1.public.test
```

## Other examples

To explore other examples on the usage of CDC, refer to [CDC examples](https://github.com/yugabyte/cdc-examples).
