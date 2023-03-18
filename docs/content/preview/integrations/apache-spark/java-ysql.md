---
title: Build Java applications using Apache Spark and YugabyteDB YSQL
headerTitle: Build a Java application using Apache Spark and YugabyteDB
linkTitle: YSQL
description: Learn how to build a Java application using Apache Spark and YugabyteDB YSQL.
menu:
  preview_integrations:
    identifier: apache-spark-2-java-ysql
    parent: apache-spark
    weight: 573
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../java-ysql/" class="nav-link active">
      <i class="fa-brands fa-java" aria-hidden="true"></i>
      Java
    </a>
  </li>

  <li >
    <a href="../scala-ysql/" class="nav-link">
      <i class="icon-scala" aria-hidden="true"></i>
      Scala
    </a>
  </li>

  <li >
    <a href="../python-ysql/" class="nav-link">
      <i class="icon-python" aria-hidden="true"></i>
      Python
    </a>
  </li>

  <li >
    <a href="../spark-sql/" class="nav-link">
      Spark SQL
    </a>
  </li>

</ul>

The following tutorial describes how to use [Apache Spark](https://spark.apache.org/) in a Java program with YugabyteDB to read and write data using the YugabyteDB JDBC driver.

## Prerequisites

This tutorial assumes that you have:

- YugabyteDB running. If you are new to YugabyteDB, follow the steps in [Quick start](../../../quick-start/).
- Java Development Kit (JDK) 1.8. JDK installers for Linux and macOS can be downloaded from [OpenJDK](http://jdk.java.net/), [AdoptOpenJDK](https://adoptopenjdk.net/), or [Azul Systems](https://www.azul.com/downloads/zulu-community/). Homebrew users on macOS can install using `brew install AdoptOpenJDK/openjdk/adoptopenjdk8`.
- [Apache Spark 3.3.0](https://spark.apache.org/downloads.html).
- [Apache Maven 3.3](https://maven.apache.org/index.html) or later.

## Set up the database

Create the database and table you will read and write to as follows:

1. From your YugabyteDB installation directory, use [ysqlsh](../../../admin/ysqlsh/) shell to read and write directly to the database as follows:

    ```sh
    ./bin/ysqlsh
    ```

1. Create a database `ysql_spark` and connect to it using the following:

    ```sql
    yugabyte=# CREATE DATABASE ysql_spark;
    yugabyte=# \c ysql_spark;
    ```

    ```output
    You are now connected to database "ysql_spark" as user "yugabyte".
    ysql_spark=#
    ```

1. Create a table in the `ysql_spark` database to read and write data through the JDBC connector as follows:

    ```sql
    ysql_spark=# CREATE TABLE test AS SELECT generate_series(1,100000) AS id, random(), ceil(random() * 20);
    ```

## Create and configure the Java project

1. Create a project using the following maven command:

    ```sh
    $ mvn archetype:generate -DgroupId=com.yugabyte -DartifactId=sparkSample -DarchetypeArtifactId=maven-archetype-quickstart -DinteractiveMode=false
    $ cd sparkSample
    ```

1. Open the `pom.xml` file in a text editor and add the following dependencies for Apache Spark and the YugabyteDB JDBC driver.

    ```xml
    <dependency>
        <groupId>org.apache.spark</groupId>
        <artifactId>spark-sql_2.11</artifactId>
        <version>2.4.2</version>
    </dependency>
    <dependency>
        <groupId> com.yugabyte</groupId>
        <artifactId>jdbc-yugabytedb</artifactId>
        <version>42.3.0</version>
    </dependency>
    ```

1. Install the added dependencies.

    ```sh
    $ mvn install
    ```

1. Create a java file `sparkSQLJavaExample.java` under `src/main/java` directory of your `sparkSample` project and add the following code to the file:

    ```java
    import org.apache.spark.sql.SparkSession;
    import org.apache.spark.sql.Dataset;
    import org.apache.spark.sql.Row;
    import java.util.Properties;

    public class sparkSQLJavaExample {

       public static void main(String[] args) {
           //Create the spark session to work with spark
           SparkSession spark = SparkSession
                   .builder()
                   .appName("Java Spark SQL basic example")
                   .config("spark.master", "local")
                   .getOrCreate();

           //Connection URL
           String jdbcUrl = "jdbc:yugabytedb://localhost:5433/ysql_spark";
           Properties connectionProperties = new Properties();
           connectionProperties.put("user", "yugabyte");
           connectionProperties.put("password", "yugabyte");

           //Create the DataFrame to read the data from the database table test
           Dataset<Row> df_test = spark.read()
                   .jdbc(jdbcUrl, "public.test", connectionProperties);

           //Print the schema of the DataFrame
           df_test.printSchema();

           /*
           The output will be similar to the following:
           root
            |-- id: integer (nullable = true)
            |-- random: double (nullable = true)
            |-- ceil: double (nullable = true)
            */

           //Read some data through the DataFrame APIs
           df_test.select("id","ceil").groupBy("ceil").sum("id").limit(5).show();

           /*
            The output will be similar to the following:
           +----+---------+
           |ceil|  sum(id)|
           +----+---------+
           | 8.0|249126014|
           | 7.0|252286019|
           |18.0|240967395|
           | 1.0|249119602|
           | 4.0|247163696|
           +----+---------+
            */

           /*
           Renaming the column of the table "test", from "ceil" to "round_off"
           in the dataframe and create a new table with the schema of the
           changed dataframe and insert all its data in the new table, name
           it as test_copy though the jdbc connector.
           */

           df_test.createOrReplaceTempView("test");
           spark.table("test")
                   .withColumnRenamed("ceil", "round_off")
                   .write()
                   .jdbc(jdbcUrl, "test_copy", connectionProperties);
           //Create the DataFrame to read data from the database table test_copy
           Dataset<Row> df_test_copy = spark.read()
                   .jdbc(jdbcUrl, "public.test_copy", connectionProperties);

           //Print the schema of the DataFrame
           df_test_copy.printSchema();

           /*
           The output will be similar to the following:
           root
            |-- id: integer (nullable = true)
            |-- random: double (nullable = true)
            |-- round_off: double (nullable = true)
           */

           /*
           The following code will create the DataFrame for the table "test" with some
           specific options for maintaining the parallelism while fetching the table content,

           1. numPartitions - divides the whole task into numPartitions parallel tasks.
           2. lowerBound - min value of the partitionColumn in table
           3. upperBound - max value of the partitionColumn in table
           4. partitionColumn - the column on the basis of which partition happen

           These options help in breaking down the whole task into `numPartitions` parallel
           tasks on the basis of the `partitionColumn`, with the help of minimum and maximum
           value of the column.

           5. pushDownPredicate - optimizes the query by pushing down the filters to
           YugabyteDB using the JDBC connector.
           6. pushDownAggregate - optimizes the query by pushing down the aggregated
           to YugabyteDB using the JDBC connector.

           These two options help in optimizing the SQL queries executing on this DataFrame
           if those SQL queries consist of some filters or aggregate functions by pushing
           down those filters and aggregates to the YugabyteDB using the JDBC connector.
            */
           Dataset<Row> new_df_test = spark.read()
                   .format("jdbc")
                   .option("url", jdbcUrl)
                   .option("dbtable", "test")
                   .option("user", "yugabyte")
                   .option("password", "yugabyte")
                   .option("driver", "com.yugabyte.Driver")
                   .option("load-balance", "true")
                   .option("numPartitions", 5)
                   .option("partitionColumn", "ceil")
                   .option("lowerBound", 0)
                   .option("upperBound", 20)
                   .option("pushDownPredicate", true)
                   .option("pushDownAggregate", true)
                   .load();

           new_df_test.createOrReplaceTempView("test");
           spark.sql("select sum(ceil) from test where id > 50000").show();
           /*
           The output will be similar to the following:
           +---------+
           |sum(ceil)|
           +---------+
           | 525124.0|
           +---------+
            */

           spark.stop();
       }
    }
    ```

## Compile and run the application

1. Compile the project:

    ```sh
    $ mvn compile
    ```

1. Run the application using the following command and verify your output as mentioned in the comments of the `sparkSQLJavaExample` file:

    ```sh
    $  mvn exec:java -Dexec.mainClass="sparkSQLJavaExample"
    ```
