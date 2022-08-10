---
title: Build a Python application using Apache Spark and YugabyteDB
linkTitle: YSQL
description: Build a Python application using Apache Spark and YugabyteDB
menu:
  preview:
    identifier: apache-spark-3-python-ysql
    parent: integrations
    weight: 572
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../java-ysql/" class="nav-link">
      <i class="icon-java-bold" aria-hidden="true"></i>
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
    <a href="../python-ysql/" class="nav-link active">
      <i class="icon-python" aria-hidden="true"></i>
      Python
    </a>
  </li>

</ul>

## Prerequisites

- Download [Apache Spark 3.3.0](https://spark.apache.org/downloads.html) .
- Install and run a [YugabyteDB cluster](/preview/quick-start/).

There are various spark apis of different languages such as  Scala `spark-shell`, Python `pyspark`, and `spark-sql` APIs, out of those this tutorial will explain how to use Scala `spark-shell` API with YugabyteDB.

## Start Python Spark shell with YugabyteDB Driver

Go to the spark installation directory and use the following command to start the pyspark shell and pass the package of YugabyteDB driver with the param --packages. It will fetch the YugabyteDB Driver from local cache if present otherwise it will install the driver from online.

./bin/pyspark --packages com.yugabyte:jdbc-yugabytedb:42.3.0

This prompt will come up:
Welcome to
      ____              __
     / __/__  ___ _____/ /__
    _\ \/ _ \/ _ `/ __/  '_/
   /__ / .__/\_,_/_/ /_/\_\   version 3.3.0
      /_/

Using Python version 3.8.9 (default, Jul 19 2021 09:37:30)
Spark context Web UI available at http://192.168.0.141:4040
Spark context available as 'sc' (master = local[*], app id = local-1658425088632).
SparkSession available as 'spark'.
>>>

## Set up the Database

Go to the YugabyteDB installation and open ysqlsh shell for reading and writing directly into the database using $./bin/ysqlsh
This prompt will come up -

ysqlsh (11.2-YB-2.13.1.0-b0)
Type "help" for help.

yugabyte=#

Create a separate database for pyspark as ysql_pyspark and connect to it:

yugabyte=# CREATE DATABASE ysql_pyspark;
yugabyte=# \c ysql_pyspark;
You are now connected to database "ysql_pyspark" as user "yugabyte".
ysql_pyspark=#

Create a table in the ysql_pyspark database to use it for reading and writing data through jdbc connector from pyspark -
ysql_pyspark=# create table test as select generate_series(1,100000) AS id, random(), ceil(random() * 20);

## Set up the connectivity with YugabyteDB

Following statements will set up the connection url and properties to be used to read from and write data through the jdbc connector into YugabyteDB.

>>> jdbcUrl = "jdbc:yugabytedb://localhost:5433/ysql_pyspark"
>>> connectionProperties = {
  "user" : "yugabyte",
  "password" :  "yugabyte",
  "driver" :  "com.yugabyte.Driver"
}

## Store and Retrieve Data

Create the dataframe for the test table for reading data from the table in the database through jdbc connector using the following code :
>>> test_Df = spark.read.jdbc(url=jdbcUrl, table="test",properties= connectionProperties)

Alternatively, you can also use SQL queries to create a dataframe which pushes down the queries to YugabyteDB  through the  jdbc connector to fetch the rows and create a dataframe on that result.
>>> test_Df = spark.read.jdbc(url=jdbcUrl, table="(select * from test) test_alias", properties=connectionProperties)

Print the schema of the dataframe created above:
>>> test_Df.printSchema()

This will be shown as output-
root
 |-- id: integer (nullable = true)
 |-- random: double (nullable = true)
 |-- ceil: double (nullable = true)

Now, read some data from the table using the Dataframe APIs -
>>> test_Df.select("id","ceil").groupBy("ceil").sum("id").limit(10).show()

This will be shown as output-
+--------+---------+
|ceil    |  sum(id)|
+--------+---------+
|     8.0|248688663|
|     7.0|254438906|
|    18.0|253717793|
|     1.0|253651826|
|     4.0|251144069|
|    11.0|252091080|
|    14.0|244487874|
|    19.0|256220339|
|     3.0|247630466|
|     2.0|249126085|
+--------+---------+

Alternatively, one can use the spark.sql() API to directly execute the SQL queries from pyspark shell using the following code:

>>> test_Df.createOrReplaceTempView("test")
>>> res_df = spark.sql("select ceil, sum(id) from test group by ceil limit 10")
>>> res_df.show()

The output will be similar as above.

The following code will first rename the column of the table test from ceil to round_off in the dataframe and create a new table with the schema of the dataframe and insert the data and name it as test_copy though the jdbc connector.

>>> spark.table("test").withColumnRenamed("ceil", "round_off").write.jdbc(url=jdbcUrl, table="test_copy", properties=connectionProperties)

Verify it in the database -
ysql_pyspark=# \dt
           List of relations
 Schema |   Name    | Type  |  Owner
--------+-----------+-------+----------
 public | test_copy | table | yugabyte
 public | test      | table | yugabyte
(2 rows)

ysql_pyspark=# \d test_copy
                   Table "public.test_copy"
  Column   |       Type       | Collation | Nullable | Default
-----------+------------------+-----------+----------+---------
 id        | integer          |           |          |
 random    | double precision |           |          |
 round_off | double precision |           |          |


ysql_pyspark=# select count(*) from test_copy;
 count
--------
 100000
(1 row)

There are different modes for writing in the database through the jdbc connector such as append, overwrite, errorifexists and ignore. The below code uses the append where it appends the data from test_copy table into the table test.

>>> spark.table("test_copy").write.mode("append").jdbc(url=jdbcUrl, table="test", properties=connectionProperties)

Verify it in the database:

ysql_pyspark=# select count(*) from test;
 count
--------
 200000
(1 row)


## Parallelism

The following code will create the dataframe for the table test with some specific options for maintaining the parallelism while fetching the table content,
numPartitions - divides the whole task into numPartitions parallel tasks.
lowerBound - min value of the partitionColumn in table
upperBound - max value of the partitionColumn in table
partitionColumn - the column on the basis of which partition happen

These options help in breaking down the whole task into numPartitions parallel tasks on the basis of the partitionColumn with the help of min and max value of the column.

>>> new_test_df = spark.read.jdbc(url=jdbcUrl, table="test", properties=connectionProperties,numPartitions=5, column="ceil", lowerBound=0, upperBound=20)

>>> new_test_df.createOrReplaceTempView("test")
>>> spark.sql("select sum(ceil) from test where id > 50000").show

This will be shown as output:
+---------+
|sum(ceil)|
+---------+
|1049414.0|
+---------+

Verify that the spark job is created with parallelism -

Go to the Spark UI using  https://localhost:4040 . if your port 4040 is occupied, then change the port accordingly with the port on which spark-sql’s UI is started which can be seen in the output when spark-sql shell is started.
Now go to the SQL tab in the UI and click on the last executed SQL statements and see if numPartitions is 5 as shown below:

