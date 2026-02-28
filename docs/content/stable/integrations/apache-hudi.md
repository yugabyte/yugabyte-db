---
title: Apache Hudi
linkTitle: Apache Hudi
description: Use Apache Hudi with YSQL API
menu:
  stable_integrations:
    identifier: ysql-hudi
    parent: integrations-other
    weight: 571
type: docs
---

[Apache Hudi](https://hudi.apache.org/) is a powerful data management framework that simplifies incremental data processing and storage, making it a valuable component for integrating with YugabyteDB to achieve real-time analytics, and seamless data consistency across distributed environments.

The following tutorials describe how to integrate YugabyteDB with Apache Hudi:

- Using real-time change data capture (CDC) with the [YugabyteDB gRPC connector](/stable/additional-features/change-data-capture/using-yugabytedb-grpc-replication/) and [HoodieDeltaStreamer](https://hudi.apache.org/docs/hoodie_streaming_ingestion/) with Apache Spark.
- Loading incremental data into YugabyteDB using HoodieDeltaStreamer and JDBC driver.

## Prerequisites

To use Apache Hudi, ensure that you have the following:

- Docker.

- YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](/stable/quick-start/docker).

- Install Apache Spark (version 3.4, 3.3, or 3.2) and Scala. Verify installation using `spark-submit` and `spark-shell` commands.

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#HoodieDeltaStreamer" class="nav-link active" id="hoodie-tab" data-bs-toggle="tab"
      role="tab" aria-controls="hoodie" aria-selected="true">
      <img src="/icons/availability.svg" alt="Hudi and CDC Icon">
      YugabyteDB CDC
    </a>
  </li>
  <li >
    <a href="#HoodieStreamer" class="nav-link" id="hudi-tab" data-bs-toggle="tab"
      role="tab" aria-controls="hudi" aria-selected="false">
      <img src="/icons/list-icon.svg" alt="Hudi incremental Icon">
      Incremental data loading
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="HoodieDeltaStreamer" class="tab-pane fade show active" role="tabpanel" aria-labelledby="hoodie-tab">

## YugabyteDB CDC

This integration allows continuous and incremental data ingestion from YugabyteDB into analytical processes, leveraging the power of Apache Hudi.

### Set up Kafka and schema registry

1. Download `docker-compose.yaml` from the [CDC-examples](https://github.com/yugabyte/cdc-examples/blob/main/grpc_connector/cdc-quickstart-kafka-connect/docker-compose.yaml) folder and run all the specified containers.

    This installs the Confluent schema registry, Control Center, ZooKeeper, Kafka, YugabyteDB Debezium Kafka Connector, Grafana, and Prometheus containers. Configure the ports as required.

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
    docker compose up -d
    ```

### Set up and configure gRPC CDC stream ID in YugabyteDB

Create a database stream ID for a specific database (for example, demo):

1. Assign your current database node address to the "{IP}" variable as follows:

    ```sh
    export IP=10.23.16.6
    ```

1. Run the following `yb-admin` command from the YugabyteDB node bin directory or `/home/yugabyte/tserver/bin` directory of any database node of your YugabyteDB cluster:

    ```sh
    ./yb-admin --master_addresses ${IP}:7100 create_change_data_stream ysql.demo implicit all
    ```

    This outputs a stream ID, such as `a4f8291c3737419dbe4feee5a1b19aee`.

### Deploy the Kafka source connector

Run the Kafka connector with the following command:

```sh
curl -i -X POST -H "Accept:application/json" -H "Content-Type:application/json" localhost:8083/connectors/ -d '{
  "name": "cdc-demo",
  "config": {
    "connector.class": "io.debezium.connector.yugabytedb.YugabyteDBgRPCConnector",
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

### Validate the schema and verify Kafka topics

Launch the Confluent Control Center and verify the schema details and messages in the relevant topics.

Access the Control Center at `http://<docker_container_IP_or_VM>:9021`.

Start [populating the data from YugabyteDB](/stable/additional-features/change-data-capture/using-logical-replication/get-started/#use-the-ysql-command-line-client), and ensure you are able to see the messages in **Control Center –> Topics –> Messages** (for example, `http://172.18.0.2:9021/clusters/management/topics/cdc.public.cdctest/message-viewer`).

### Install Apache Hudi

1. [Build Apache Hudi from source](https://github.com/apache/hudi?tab=readme-ov-file#building-apache-hudi-from-source). Modify the source files to account for differences between PostgreSQL and YugabyteDB CDC emissions as follows:

    - In `DebeziumConstants.java`, comment out or remove lines related to `xmin`.

    - In `PostgresDebeziumSource.java`, comment out or remove parameters related to `xmin`.

1. Run Maven to build Hudi as follows:

    ```sh
    mvn clean package -DskipTests
    ```

### Run a Spark job using HoodieDeltaStreamer

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
      # Adjust the target base path for the Hudi table as per your setup
      --target-base-path file:///tmp/hoodie/dbs-cdctest \
      # Adjust the Hudi table name as per your setup
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
      # Adjust the recordkey file as per your setup
      --hoodie-conf hoodie.datasource.write.recordkey.field=sno \
      # Adjust the partition name as per your setup
      --hoodie-conf hoodie.datasource.write.partitionpath.field=name \
      --hoodie-conf hoodie.datasource.write.keygenerator.class=org.apache.hudi.keygen.SimpleKeyGenerator \
      # Adjust the path for the Spark configuration file as per your setup
      --props /your_folder/spark-config.properties
    ```

     Adjust paths and filenames as per your environment setup.

### Query the Hudi table

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

### Verify

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

1. Verify deletion in Hudi Table with the following query in a Spark shell:

    ```spark
    spark.sql("SELECT _hoodie_commit_time, sno, name, _change_operation_type FROM cdcdemo").show()
    ```

    The record with `sno = 826` is deleted from the Hudi table due to the propagated delete event.

  </div>

  <div id="HoodieStreamer" class="tab-pane fade" role="tabpanel" aria-labelledby="hudi-tab">

## Incremental data load

This approach is particularly advantageous for incremental data loading/ETL, and applications requiring real-time processing and handling large amounts of distributed data.

### Install Apache Hudi

1. [Build Apache Hudi from source](https://github.com/apache/hudi?tab=readme-ov-file#building-apache-hudi-from-source) by following the official instructions.

1. Run Maven to build Hudi as follows:

    ```sh
    mvn clean package -DskipTests
    ```

### Configure and run HoodieDeltaStreamer

1. Create a `spark-config.properties` file as follows:

    ```properties
    spark.serializer=org.apache.spark.serializer.KryoSerializer
    spark.sql.catalog.spark_catalog=org.apache.spark.sql.hudi.catalog.HoodieCatalog
    spark.sql.hive.convertMetastoreParquet=false
    ```

1. Create Hudi table properties (`hudi_tbl.props`) as follows:

    ```properties
    hoodie.datasource.write.keygenerator.class=org.apache.hudi.keygen.SimpleKeyGenerator

    # hoodie.datasource.write.recordkey.field - Primary key of hudi_test_table of YugabyteDB
    hoodie.datasource.write.recordkey.field=id

    # hoodie.datasource.write.partitionpath.field - PartitionPath field is created_at
    hoodie.datasource.write.partitionpath.field=created_at

    # hoodie.datasource.write.precombine.field - Precombine field is updated_at
    hoodie.datasource.write.precombine.field=update_at

    # hoodie.streamer.jdbc.url - YugabyteDB DB Node IP address is mentioned with DB Name (yugabyte)
    hoodie.streamer.jdbc.url=jdbc:postgresql://10.12.22.168:5433/yugabyte

    # hoodie.streamer.jdbc.user - DB User is yugabyte
    hoodie.streamer.jdbc.user=yugabyte

    # hoodie.streamer.jdbc.password - Password as per your configuration
    hoodie.streamer.jdbc.password=xxxxx

    # hoodie.streamer.jdbc.driver.class - PostgreSQL driver used to connect YugabyteDB
    hoodie.streamer.jdbc.driver.class=org.postgresql.Driver

    # hoodie.streamer.jdbc.table.name - YugabyteDB table name is hudi_test_table
    hoodie.streamer.jdbc.table.name=hudi_test_table

    # hoodie.streamer.jdbc.incr.column.name - This is incremental column; based on this value, data will be loaded into Hudi using this column.

    # Delta streamer compares Old checkpoint=(Option{val=2023-12-21 00:00:00}). New Checkpoint=(2023-12-21 00:00:00) and loads the fresh data.
    hoodie.streamer.jdbc.table.incr.column.name=update_at

    # hoodie.streamer.jdbc.incr.pull - JDBC connection performs an incremental pull if it is TRUE
    hoodie.streamer.jdbc.incr.pull=true

    # hoodie.streamer.jdbc.incr.fallback.to.full.fetch - When set to true, it will attempt incremental fetch, and if there are errors, it will fallback to a full fetch.
    hoodie.streamer.jdbc.incr.fallback.to.full.fetch=true
    ```

1. Run the Spark job as follows:

    ```sh
    spark-submit \
      --class org.apache.hudi.utilities.streamer.HoodieStreamer \
      --packages org.apache.hudi:hudi-spark3.4-bundle_2.12:0.14.0,org.postgresql:postgresql:42.5.4 \
      --properties-file spark-config.properties \
      --master 'local[*]' \
      --executor-memory 1g "/path/to/hudi-utilities-bundle_2.12-0.14.0.jar" \
      --table-type COPY_ON_WRITE \
      --op UPSERT \
      --source-ordering-field update_at \
      --source-class org.apache.hudi.utilities.sources.JdbcSource \
      # Adjust the target base path for the Hudi table as per your setup
      --target-base-path file:///tmp/path/to/hudidb/ \
       # Adjust the Hudi table name as per your setup
      --target-table hudi_test_table \
      --props hudi_tbl.props
    ```

    Adjust paths and filenames as per your environment setup.

### Query the Hudi table

1. Verify the Hudi table is created in the specified directory as follows:

    ```sh
    cd /tmp/path/to/hudidb/
    ls -latrl
    ```

    You can see output similar to the following:

    ```output
    total 72
    drwxr-xr-x 2 azureuser azureuser 4096 Dec 17 09:04 1701799492525559
    ...
    drwxr-xr-x 7 azureuser azureuser 4096 Dec 17 10:20 .hoodie
    ```

1. To query the table using Spark, follow these steps in Spark Shell (spark-shell):

    ```spark
    import org.apache.spark.sql.SparkSession
    val spark = SparkSession.builder().appName("Hudi Integration").getOrCreate()
    import scala.collection.JavaConversions._
    import org.apache.spark.sql.SaveMode._
    import org.apache.hudi.DataSourceReadOptions._
    import org.apache.hudi.DataSourceWriteOptions._
    import org.apache.hudi.common.table.HoodieTableConfig._
    import org.apache.hudi.config.HoodieWriteConfig._
    import org.apache.hudi.keygen.constant.KeyGeneratorOptions._
    import org.apache.hudi.common.model.HoodieRecord
    import spark.implicits._

    val basePath = "file:///tmp/path/to/hudidb/"
    val ybdbDF = spark.read.format("hudi").load(basePath)
    ybdbDF.createOrReplaceTempView("hudi_test")
    spark.sql("SELECT id, name, qty, created_at, update_at FROM hudi_test").show()
    ```

    You can see output similar to the following:

    ```output
    +---+--------------------+---+----------+--------------------+
    | id|                name|qty|created_at|          update_at|
    +---+--------------------+---+----------+--------------------+
    |101|              John  |255|      null|2023-12-17 09:07:...|
    ...
    +---+--------------------+---+----------+--------------------+
    ```

  </div>
</div>

## Learn more

[Combine Transactional Integrity and Data Lake Operations with YugabyteDB and Apache Hudi](https://www.yugabyte.com/blog/apache-hudi-data-lakehouse-integration/)
