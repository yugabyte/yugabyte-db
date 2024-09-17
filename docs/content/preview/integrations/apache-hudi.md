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

The following tutorial describes steps to integrate YugabyteDB with Apache Hudi, enabling a robust data lakehouse architecture leveraging both SQL capabilities and big data processing power.

## Prerequisites

To use Apache Hudi, ensure that you have the following:

- YugabyteDB up and running. Download and install YugabyteDB on macOS by following the steps in [Quick start](../../quick-start/).
- Java JDK 11 or later is installed.
- Scala is installed.
- Apache Hadoop and Apache Spark installed.

## Setup

To run Apache Hudi with YugabyteDB, do the following:

1. Download and extract Apache Hudi using the following commands:

    ```sh
    wget https://downloads.apache.org/hudi/0.9.0/hudi-0.9.0.tar.gz
    ```

    ```sh
    tar -xvzf hudi-0.9.0.tar.gz
    ```

1. Set the following environment variables:

    ```sh
    export HADOOP_HOME=/usr/local/Cellar/hadoop/your_hadoop_version
    export SPARK_HOME=/usr/local/Cellar/apache-spark/your_spark_version
    export HUDI_HOME=~/path_to_hudi/hudi-0.9.0/
    export PATH=$PATH:$HADOOP_HOME/bin:$SPARK_HOME/bin:$HUDI_HOME/bin
    ```

    Replace `your_hadoop_version` and `your_spark_version` with the installed versions of Hadoop and Spark.

## Configure

To configure Hudi to use YugabyteDB, do the following:

1. Create a configuration file `hudi_config.properties` that specifies how Hudi will connect to YugabyteDB:

    ```properties
    # Hudi configurations
    hoodie.embed.timeline.server=false
    hoodie.datasource.write.table.name=your_hudi_table_name
    hoodie.datasource.write.recordkey.field=name
    hoodie.datasource.write.partitionpath.field=partition_path
    hoodie.datasource.write.table.type=COPY_ON_WRITE
    # YugabyteDB configurations
    spark.yugabyte.driver=org.postgresql.Driver
    spark.yugabyte.url=jdbc:postgresql://127.0.0.1:5433/your_db_name
    spark.yugabyte.user=your_username
    spark.yugabyte.password=your_password
    ```

    Replace `your_hudi_table_name`, `your_db_name`, `your_username`, and `your_password` with the appropriate values for your set up.

1. Create a Spark job to initialize the Hudi table and write some data as follows:

    ```scala
    package example

    import org.apache.hudi.QuickstartUtils._
    import org.apache.hudi.config.HoodieWriteConfig
    import org.apache.spark.sql.SparkSession
    import java.util.Properties

    object HudiYugabyteDBIntegration {
      def main(args: Array[String]): Unit = {
        val spark = SparkSession.builder
          .appName("HudiYugabyteDBIntegration")
          .config("spark.serializer", "org.apache.spark.serializer.KryoSerializer")
          .config("spark.kryo.registrator", "org.apache.spark.HoodieKryoRegistrar")
          .getOrCreate()

        val props = new Properties()
        props.load(new java.io.FileInputStream("hudi_config.properties"))

        val tableName = props.getProperty("hoodie.datasource.write.table.name")

        val dataGen = new DataGenerator()
        val inserts = convertToStringList(dataGen.generateInserts(100))

        import spark.implicits._
        val df = spark.read.json(spark.sparkContext.parallelize(inserts, 2))

        df.write.format("hudi")
          .options(getQuickstartWriteConfigs)
          .option(HoodieWriteConfig.TABLE_NAME, tableName)
          .mode("overwrite")
          .options(Map(
            "hoodie.datasource.write.table.name" -> props.getProperty("hoodie.datasource.write.table.name"),
            "hoodie.datasource.write.recordkey.field" -> props.getProperty("hoodie.datasource.write.recordkey.field"),
            "hoodie.datasource.write.partitionpath.field" -> props.getProperty("hoodie.datasource.write.partitionpath.field"),
            "hoodie.datasource.write.precombine.field" -> "ts",
            "hoodie.datasource.write.table.type" -> props.getProperty("hoodie.datasource.write.table.type")
          ))
          .save(props.getProperty("spark.yugabyte.url"))

        println("Hudi data written to YugabyteDB successfully")
        spark.stop()
      }
    }
    ```

1. Run the Spark job as follows:

    ```sh
    spark-submit --class example.HudiYugabyteDBIntegration --packages org.apache.hudi:hudi-spark-bundle_2.12:0.9.0 path_to_your_scala_file.jar
    ```

## Query the Hudi table

To verify the integration, check that the Hudi table is created successfully as follows:

```sh
azureuser@hudi-test:/tmp/hoodie/your_hudi_table_name$ ls -atlr
```

```output
drwxr-xr-x  7 azureuser azureuser 4096 Dec 16 15:59  .hoodie
```

```sh
azureuser@hudi-test:/tmp/hoodie/your_hudi_table_name$ cd .hoodie/
azureuser@hudi-test:/tmp/hoodie/your_hudi_table_name/.hoodie$ ls -altr
```

```output
total 136
drwxr-xr-x 2 azureuser azureuser 4096 Dec 16 15:44 archived
drwxr-xr-x 2 azureuser azureuser 4096 Dec 16 15:44 .schema
drwxr-xr-x 3 azureuser azureuser 4096 Dec 16 15:44 .aux
-rw-r--r-- 1 azureuser azureuser	0 Dec 16 15:44 20231216154446818.deltacommit.requested
-rw-r--r-- 1 azureuser azureuser	8 Dec 16 15:44 .20231216154446818.deltacommit.requested.crc
drwxr-xr-x 4 azureuser azureuser 4096 Dec 16 15:44 metadata
-rw-r--r-- 1 azureuser azureuser 1014 Dec 16 15:44 hoodie.properties
-rw-r--r-- 1 azureuser azureuser   16 Dec 16 15:44 .hoodie.properties.crc
-rw-r--r-- 1 azureuser azureuser 1499 Dec 16 15:44 20231216154446818.deltacommit.inflight
-rw-r--r-- 1 azureuser azureuser   20 Dec 16 15:44 .20231216154446818.deltacommit.inflight.crc
-rw-r--r-- 1 azureuser azureuser 2858 Dec 16 15:44 20231216154446818.deltacommit
-rw-r--r-- 1 azureuser azureuser   32 Dec 16 15:44 .20231216154446818.deltacommit.crc
-rw-r--r-- 1 azureuser azureuser	0 Dec 16 15:45 20231216154526806.deltacommit.requested
-rw-r--r-- 1 azureuser azureuser	8 Dec 16 15:45 .20231216154526806.deltacommit.requested.crc
-rw-r--r-- 1 azureuser azureuser 1499 Dec 16 15:45 20231216154526806.deltacommit.inflight
-rw-r--r-- 1 azureuser azureuser   20 Dec 16 15:45 .20231216154526806.deltacommit.inflight.crc
-rw-r--r-- 1 azureuser azureuser 2882 Dec 16 15:45 20231216154526806.deltacommit
-rw-r--r-- 1 azureuser azureuser   32 Dec 16 15:45 .20231216154526806.deltacommit.crc
-rw-r--r-- 1 azureuser azureuser	0 Dec 16 15:53 20231216155306810.deltacommit.requested
-rw-r--r-- 1 azureuser azureuser	8 Dec 16 15:53 .20231216155306810.deltacommit.requested.crc
-rw-r--r-- 1 azureuser azureuser  814 Dec 16 15:53 20231216155306810.deltacommit.inflight
-rw-r--r-- 1 azureuser azureuser   16 Dec 16 15:53 .20231216155306810.deltacommit.inflight.crc
drwxr-xr-x 6 azureuser azureuser 4096 Dec 16 15:53 ..
-rw-r--r-- 1 azureuser azureuser 1957 Dec 16 15:53 20231216155306810.deltacommit
-rw-r--r-- 1 azureuser azureuser   24 Dec 16 15:53 .20231216155306810.deltacommit.crc
-rw-r--r-- 1 azureuser azureuser	0 Dec 16 15:53 20231216155326810.deltacommit.requested
-rw-r--r-- 1 azureuser azureuser	8 Dec 16 15:53 .20231216155326810.deltacommit.requested.crc
-rw-r--r-- 1 azureuser azureuser 1503 Dec 16 15:53 20231216155326810.deltacommit.inflight
-rw-r--r-- 1 azureuser azureuser   20 Dec 16 15:53 .20231216155326810.deltacommit.inflight.crc
-rw-r--r-- 1 azureuser azureuser 2898 Dec 16 15:53 20231216155326810.deltacommit
-rw-r--r-- 1 azureuser azureuser   32 Dec 16 15:53 .20231216155326810.deltacommit.crc
-rw-r--r-- 1 azureuser azureuser	0 Dec 16 15:59 20231216155926812.deltacommit.requested
-rw-r--r-- 1 azureuser azureuser	8 Dec 16 15:59 .20231216155926812.deltacommit.requested.crc
-rw-r--r-- 1 azureuser azureuser 1525 Dec 16 15:59 20231216155926812.deltacommit.inflight
-rw-r--r-- 1 azureuser azureuser   20 Dec 16 15:59 .20231216155926812.deltacommit.inflight.crc
drwxr-xr-x 7 azureuser azureuser 4096 Dec 16 15:59 .
-rw-r--r-- 1 azureuser azureuser 2232 Dec 16 15:59 20231216155926812.deltacommit
-rw-r--r-- 1 azureuser azureuser   28 Dec 16 15:59 .20231216155926812.deltacommit.crc
drwxr-xr-x 2 azureuser azureuser 4096 Dec 16 15:59 .temp
```

You can execute queries to the table using Spark. Refer to instructions in the [Apache Hudi documentation](https://hudi.apache.org/docs/quick-start-guide/#querying).

## Learn more

- [Combine Transactional Integrity and Data Lake Operations with YugabyteDB and Apache Hudi](https://www.yugabyte.com/blog/apache-hudi-data-lakehouse-integration/)
