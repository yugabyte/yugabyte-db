---
title: Apache Hudi
linkTitle: Apache Hudi
description: Use Apache Hudi with YSQL API
menu:
  preview_integrations:
    identifier: ysql-hudi
    parent: integrations-other
    weight: 571
type: docs
---

[Apache Hudi](https://hudi.apache.org/) is a powerful data management framework that simplifies incremental data processing and storage, making it a valuable component for integrating with YugabyteDB to achieve real-time analytics, and seamless data consistency across distributed environments.

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#HoodieDeltaStreamer" class="nav-link active" id="hoodie-tab" data-bs-toggle="tab"
      role="tab" aria-controls="hoodie" aria-selected="true">
      <img src="/icons/database.svg" alt="Server Icon">
      Hoodie DeltaStreamer with CDC
    </a>
  </li>
  <li >
    <a href="#HoodieStreamer" class="nav-link" id="hudi-tab" data-bs-toggle="tab"
      role="tab" aria-controls="hudi" aria-selected="false">
      <img src="/icons/cloud.svg" alt="Cloud Icon">
      Hoodie Streamer
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="HoodieDeltaStreamer" class="tab-pane fade" role="tabpanel" aria-labelledby="hoodie-tab">

The following tutorial describes steps to integrate YugabyteDB with Apache Hudi for real-time Change Data Capture (CDC) using YugabyteDB's CDC connector and Hoodie DeltaStreamer with Apache Spark.

This integration allows continuous and incremental data ingestion from YugabyteDB into analytical processes, leveraging the power of Apache Hudi.

## Prerequisites

To use Apache Hudi, ensure that you have the following:

- Docker installed.

- YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](../../quick-start/docker).

- Install Apache Spark (version 3.4, 3.3, or 3.2) and Scala. Verify installation using `spark-submit` and `spark-shell` commands.

## Setup Kafka and Schema Registry

1. Download the `docker-compose.yaml` from the [CDC-examples](https://github.com/yugabyte/cdc-examples/blob/main/cdc-quickstart-kafka-connect/docker-compose.yaml) folder and run all the containers specified within it. It will install the Confluent schema registry, Control Center, ZooKeeper, Kafka, YugabyteDB Debezium Kafka Connector, Grafana, and Prometheus containers and configure the ports as required.

    This example uses port 8091 for the schema registry as follows:

    ```properties
    schema-registry:
      image: confluentinc/cp-schema-registry:7.2.1
      hostname: schema-registry
      container_name: schema-registry
      depends_on:
        - broker
      ports:
        - "8091:8091"
      environment:
        SCHEMA_REGISTRY_HOST_NAME: schema-registry
        SCHEMA_REGISTRY_KAFKASTORE_BOOTSTRAP_SERVERS: 'broker:29092'
        SCHEMA_REGISTRY_LISTENERS: http://0.0.0.0:8091
    ```

1. Run the docker containers:

    ```sh
    docker-compose up -d
    ```

## Setup and configure CDC Stream ID in YugabyteDB

Create a database stream ID for a specific database (for example, demo).

1. Assign your current database node address to the "{IP}" variable as follows:

    ```sh
    export IP=10.23.16.6
    ```

1. Run the following `yb-admin` command from the YugabyteDB node bin directory or `/home/yugabyte/tserver/bin` directory of any database node of your YugabyteDB cluster:

    ```sh
    ./yb-admin --master_addresses ${IP}:7100 create_change_data_stream ysql.demo implicit all
    ```

    This will output a stream ID such as, `a4f8291c3737419dbe4feee5a1b19aee`.

## Deploy the Kafka Source Connector

Run the Kafka connector with the following command:

```sh
curl -i -X POST -H "Accept:application/json" -H "Content-Type:application/json" localhost:8083/connectors/ -d '{
  "name": "cdc-demo",
  "config": {
    "connector.class": "io.debezium.connector.yugabytedb.YugabyteDBConnector",
    "database.hostname": "'$IP'",
    "database.port": "5433",
    "tasks.max": "3",
    "database.master.addresses": "'$IP':7100",
    "database.user": "yugabyte",
    "database.password": "xxxxxxx",
    "database.dbname": "demo",
    "database.server.name": "dbs",
    "table.include.list": "public.cdctest",
    "database.streamid": "a4f8291c3737419dbe4feee5a1b19aee",
    "transforms":"pgcompatible",
    "transforms.pgcompatible.type":"io.debezium.connector.yugabytedb.transforms.PGCompatible",
    "key.converter.schemas.enable": "true",
    "value.converter.schemas.enable": "true",
    "tombstones.on.delete":"false",
    "key.converter":"io.confluent.connect.avro.AvroConverter",
    "key.converter.schema.registry.url":"http://schema-registry:8091",
    "value.converter":"io.confluent.connect.avro.AvroConverter",
    "value.converter.schema.registry.url":"http://schema-registry:8091"
  }
}'
```

## Validate the schema and verify Kafka Topics

Launch the Confluent Control Center and verify the schema details and messages in the relevant topics. Access the Control Center at <http://{your_docker_container_IP_or_VM}:9021>. Start populating the data from YugabyteDB, and ensure you are able to see the messages in **Control Center –> Topics –> Messages** (for example, <http://{your_docker_container_IP_or_VM}:9021/clusters/management/topics/cdc.public.cdctest/message-viewer>).

## Install Apache Hudi

1. [Build Apache Hudi from source](https://github.com/apache/hudi?tab=readme-ov-file#building-apache-hudi-from-source). Modify the source files to account for differences between PostgreSQL and YugabyteDB CDC emissions as follows:

    1. In `DebeziumConstants.java`, comment out or remove lines related to "xmin".

    1. In `PostgresDebeziumSource.java`, comment out or remove parameters related to "xmin".

1. Run Maven to build Hudi as follows:

    ```sh
    mvn clean package -DskipTests
    ```

## Run a Spark Job using Hoodie DeltaStreamer

1. Create a `spark-config.properties` file as follows:

    ```properties
    spark.serializer=org.apache.spark.serializer.KryoSerializer
    spark.sql.catalog.spark_catalog=org.apache.spark.sql.hudi.catalog.HoodieCatalog
    spark.sql.hive.convertMetastoreParquet=false
    ```

1. Run the Spark job as follows:

    ```sh
    spark-submit --class org.apache.hudi.utilities.deltastreamer.HoodieDeltaStreamer \
      --packages org.apache.hudi:hudi-spark3.4-bundle_2.12:0.14.0 \
      --master local[*] \
      "/your_folder/hudi-release-0.14.0/packaging/hudi-utilities-bundle/target/hudi-utilities-bundle_2.12-0.14.0.jar" \
      --table-type MERGE_ON_READ \
      --source-class org.apache.hudi.utilities.sources.debezium.PostgresDebeziumSource \
      --payload-class org.apache.hudi.common.model.debezium.PostgresDebeziumAvroPayload \
      --target-base-path file:///tmp/hoodie/dbs-cdctest \
      --target-table dbs_cdctest \
      --source-ordering-field _event_origin_ts_ms \
      --continuous \
      --source-limit 4000000 \
      --min-sync-interval-seconds 20 \
      --hoodie-conf bootstrap.servers=localhost:9092 \
      --hoodie-conf schema.registry.url=http://localhost:8091 \
      --hoodie-conf hoodie.deltastreamer.schemaprovider.registry.url=http://localhost:8091/subjects/dbs.public.cdctest-value/versions/latest \
      --hoodie-conf hoodie.deltastreamer.source.kafka.value.deserializer.class=io.confluent.kafka.serializers.    KafkaAvroDeserializer \
      --hoodie-conf hoodie.deltastreamer.source.kafka.topic=dbs.public.cdctest \
      --hoodie-conf auto.offset.reset=earliest \
      --hoodie-conf hoodie.datasource.write.recordkey.field=sno \
      --hoodie-conf hoodie.datasource.write.partitionpath.field=name \
      --hoodie-conf hoodie.datasource.write.keygenerator.class=org.apache.hudi.keygen.SimpleKeyGenerator \
      --props /your_folder/spark-config.properties
    ```

## Query the Hudi table

1. Verify the Hudi table is created in `/tmp/hoodie/dbs-cdctest` as follows:

    ```sh
    cd /tmp/hoodie/dbs-cdctest
    ls -atlr
    ```

1. Start a Spark shell to query the Hudi table as follows:

    ```sh
    spark-shell
    ```

1. Import necessary libraries and read data from the Hudi table:

    ```sh
    import org.apache.hudi.DataSourceReadOptions._
    import org.apache.hudi.config.HoodieWriteConfig._
    import org.apache.spark.sql.SaveMode._
    import spark.implicits._

    val basePath = "file:///tmp/hoodie/dbs-cdctest"
    val cdcDF = spark.read.format("hudi").load(basePath)
    cdcDF.createOrReplaceTempView("cdcdemo")

    spark.sql("SELECT _hoodie_commit_time, sno, name, _change_operation_type FROM cdcdemo").show()
    ```

## Verify

Perform database operations on the `cdctest` table in YugabyteDB and verify that the changes are reflected in the Hudi table by running queries through the Spark shell.

1. Insert and update records in the `cdctest` table in your YugabyteDB database as follows:

    ```sql
    -- Example of insert statement
    INSERT INTO public.cdctest (sno, name) VALUES (826, 'Test User 1');

    -- Example of update statement
    UPDATE public.cdctest SET name = 'Updated Test User 1' WHERE sno = 826;
    ```

1. Start a new Spark shell with `spark-shell` and import the necessary classes and read the data from the Hudi table:

    ```spark
    import org.apache.hudi.DataSourceReadOptions._
    import org.apache.hudi.config.HoodieWriteConfig._
    import org.apache.spark.sql.SaveMode._
    import spark.implicits._

    val basePath = "file:///tmp/hoodie/dbs-cdctest"
    val cdcDF = spark.read.format("hudi").load(basePath)
    cdcDF.createOrReplaceTempView("cdcdemo")

    spark.sql("SELECT _hoodie_commit_time, sno, name, _change_operation_type FROM cdcdemo").show()
    ```

    You can see the changes reflected from the `cdctest` table in YugabyteDB through the CDC connector into the Hudi table. The `_change_operation_type` field indicates whether the operation was an update (u), insert (c), or delete (d).

1. Delete a record from the `cdctest` table in YugabyteDB:

    ```sql
    DELETE FROM public.cdctest WHERE sno = 826;
    ```

1. Verify deletion in Hudi Table with the follwing query in a Spark shell:

    ```spark
    spark.sql("SELECT _hoodie_commit_time, sno, name, _change_operation_type FROM cdcdemo").show()
    ```

    Verify that the record with `sno = 826` is deleted from the Hudi table due to the propagated delete event.

</div>

  <div id="HoodieStreamer" class="tab-pane fade show active" role="tabpanel" aria-labelledby="local-tab">

The following tutorial describes steps to how to load incremental data YugabyteDB and with Apache Hudi using Hudi's Streamer and JDBC Driver.

This approach is suitable for incremental data loading/ETL.

## Prerequisites

To use Apache Hudi, ensure that you have the following:

- Docker installed.

- YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](../../quick-start/docker).

- Install Apache Spark (version 3.4, 3.3, or 3.2) and Scala. Verify installation using `spark-submit` and `spark-shell` commands.

  </div>