---
title: Build Java applications using Apache Spark and YugabyteDB YCQL
headerTitle: Build a Java application using Apache Spark and YugabyteDB
linkTitle: YCQL
description: Learn how to build a Java-based application using Apache Spark and YugabyteDB YCQL.
aliases:
  - /preview/integrations/apache-spark/java/
menu:
  preview_integrations:
    identifier: apache-spark-2-java-ycql
    parent: apache-spark
    weight: 574
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../java-ycql/" class="nav-link active">
      <i class="fa-brands fa-java" aria-hidden="true"></i>
      Java
    </a>
  </li>

  <li >
    <a href="../scala-ycql/" class="nav-link">
      <i class="icon-scala" aria-hidden="true"></i>
      Scala
    </a>
  </li>

  <li >
    <a href="../python-ycql/" class="nav-link">
      <i class="icon-python" aria-hidden="true"></i>
      Python
    </a>
  </li>

</ul>

The following tutorial describes how to build a Java application using the YugabyteDB Spark Connector for YCQL, and perform YCQL queries.

## Prerequisites

This tutorial assumes that you have:

- YugabyteDB running. If you are new to YugabyteDB, follow the steps in [Quick start](../../../quick-start/).
- Java Development Kit (JDK) 1.8. JDK installers for Linux and macOS can be downloaded from [OpenJDK](http://jdk.java.net/), [AdoptOpenJDK](https://adoptopenjdk.net/), or [Azul Systems](https://www.azul.com/downloads/zulu-community/). Homebrew users on macOS can install using `brew install AdoptOpenJDK/openjdk/adoptopenjdk8`.
- [Apache Spark 3.3.0](https://spark.apache.org/downloads.html).
- [Apache Maven 3.3](https://maven.apache.org/index.html) or later.

## Setting Up a Project with Maven

Add the following to your `pom.xml` file for Scala 2.12:

```xml
<dependency>
  <groupId>com.yugabyte.spark</groupId>
  <artifactId>spark-cassandra-connector_2.12</artifactId>
  <version>3.0-yb-8</version>
</dependency>
```

For more information, see [Maven artifact](https://search.maven.org/artifact/com.yugabyte.spark/spark-cassandra-connector_2.12).

## Using Apache Spark with YCQL

Suppose you work with a YCQL table created as follows:

```sql
CREATE TABLE test.person (
  id int PRIMARY KEY,
  name text,
  address text,
  phone jsonb
);
```

This table is populated with the following rows:

```output
id  | name  | address                | phone
----+-------+------------------------+-----------------------------
1   | John  | Hammersmith London, UK | {"code":"+44","phone":1000}
2   | Nick  | Acton London, UK       | {"code":"+43","phone":1200}
3   | Smith | 11 Acton London, UK    | {"code":"+44","phone":1400}
```

The following Java code shows how to initialize a Spark session:

```java
// Setup the local spark master
SparkConf conf = new SparkConf().setAppName("yb.spark-jsonb")
  .setMaster("local[1]")
  .set("spark.cassandra.connection.localDC", "datacenter1")
  .set("spark.cassandra.connection.host", addresses.get(0).getHostName())
  .set("spark.sql.catalog.ybcatalog",
       "com.datastax.spark.connector.datasource.CassandraCatalog");
SparkSession spark = SparkSession.builder().config(conf).getOrCreate();
```

The preceding code sets the local data centre, the Cassandra contact point, and an identifier for the YCQL catalog of keyspaces and tables to be used for later reference.

Any table in the YCQL catalog (declared as a CassandraCatalog) is read as a YCQL table. The following Java code shows how to read from such table:

```java
Dataset<Row> rows = spark.sql("SELECT id, address, phone FROM" +
                              "ybcatalog.test.person");
```

Similarly, the appropriately-declared catalog enables the use of the appropriate Spark connector. The following Java code shows how to write to a YCQL table:

```java
rows.writeTo("ybcatalog.test.personcopy");
```

For additional examples, see the following:

- [Spark 3 tests](https://github.com/yugabyte/yugabyte-db/tree/master/java/yb-cql-4x/src/test/java/org/yb/loadtest)
- [Spark 3 sample apps](https://github.com/yugabyte/yugabyte-db/tree/master/java/yb-cql-4x/src/main/java/com/yugabyte/sample/apps)
- [TestSpark3Jsonb.java](https://github.com/yugabyte/yugabyte-db/blob/master/java/yb-cql-4x/src/test/java/org/yb/loadtest/TestSpark3Jsonb.java)

## Using JSONB

You can use the `jsonb` data type for your columns. JSONB values are processed using the [Spark JSON functions](https://spark.apache.org/docs/3.0.0/sql-ref-functions-builtin.html#json-functions).

The following example shows how to select the phone code using the `get_json_object` function to select the sub-object at the specific path:

```java
Dataset<Row> rows =
  spark.sql("SELECT get_json_object(phone, '$.code') as code,
  get_json_object(phone, '$.phone') as phone
    FROM mycatalog.test.person");
```

The following example shows how to apply a filter based on a `jsonb` sub-object:

```java
Dataset<Row> rows =
  spark.sql("SELECT * FROM mycatalog.test.person
            WHERE get_json_object(phone, '$.phone') = 1000");
```

You can also use the `.filter()` function provided by Spark to add filtering conditions.

Note that the preceding operators are currently evaluated by Spark and not propagated to YCQL queries (as `->` JSONB operators). This is tracked by [GitHub issue #6738](https://github.com/yugabyte/yugabyte-db/issues/6738).

When the `get_json_object` function is used in the projection clause, only the target sub-object is requested and returned by YugabyteDB, as demonstrated in the following example, where only the sub-object at `key[1].m[2].b` is returned:

```java
String query = "SELECT id, address,
                get_json_object(phone, '$.key[1].m[2].b')
                as key FROM mycatalog.test.person";
Dataset<Row> rows = spark.sql(query);
```

To confirm the pruning, you can use logging at the database level (such as audit logging) or inspect the Spark execution plan using the `EXPLAIN` statement. The following example creates the execution plan as a String, then logs it and checks that it contains the expected sub-object in YCQL syntax (such as `phone->'key'->1->'m'->2->'b'`):

```java
// Get the execution plan as text rows
Dataset<Row> explain_rows = spark.sql("EXPLAIN " + query);

// Convert the EXPLAIN rows into a String object
StringBuilder explain_sb = new StringBuilder();
Iterator<Row> iterator = explain_rows.toLocalIterator();
  while (iterator.hasNext()) {
    Row row = iterator.next();
    explain_sb.append(row.getString(0));
}
String explain_text = explain_sb.toString();
// Log the execution plan
logger.info("plan is " + explain_text);

// Check that the expected sub-object is requested in the plan
assertTrue(explain_text.contains(
  "id,address,phone->'phone',phone->'key'->1->'m'->2->'b'"));
```

### JSONB Upsert

The JSONB upsert functionality in the YugabyteDB Apache Spark connector supports merging data into existing JSONB columns instead of overwriting the existing data.

Data for fields in JSONB columns may be collected at multiple points in time. For subsequent loading of partial data (apart from the first batch), you may not want to overwrite existing data for the fields.

Using the upsert functionality, subsequent data can be merged with existing data (for the given row key). This provides flexibility to aggregate data after multiple rounds of loading.

The following example uses `Dataset` to load incremental data.

This sequence of operations can be performed multiple times on different CSV inputs (against the same target table).

```java
// Create Spark session
SparkSession spark = SparkSession.builder().config(conf).withExtensions(new CassandraSparkExtensions())
                .getOrCreate();
URL sqlFileRes = getClass().getClassLoader().getResource("person.csv");

// Specify the CSV file containing new data
Dataset<Row> rows = spark.read().option("header", true).csv(sqlFileRes.getFile());
rows.createOrReplaceTempView("temp");

// Specify the fields from the CSV (code,call,Module)
Dataset<Row> updatedRows = spark.sql("select id,name,address,code,call,Module from temp ");

// Specify JSON field to column mapping (see description below)
updatedRows.write().format("org.apache.spark.sql.cassandra")
      .option("keyspace", KEYSPACE)
      .option("table", INPUT_TABLE)
      .option("spark.cassandra.mergeable.json.column.mapping", "code,call,Module:phone")
      .option("spark.cassandra.json.quoteValueString", "true").mode(SaveMode.Append).save();
```

Here is sample content of the dataset:

```output
+---+-----+----------------+--------------------------------------------------------------+
|id |name |address         |phone                                                         |
+---+-----+----------------+--------------------------------------------------------------+
|5  |SAM  |INDIA           |{"call":"75675655","code":"91"}                               |
|1  |ram  |null            |{"Module":"CM","call":"932233342","code":"+42","phone":"200"} |
+---+-----+----------------+--------------------------------------------------------------+
```

#### Configuring JSONB upsert

##### spark.cassandra.json.quoteValueString

Specifies whether the JSONB field values should be quoted as string. Defaults to false (because this is not space efficient).

The option allows you to preserve floating point precision for JSONB fields. For un-quoted non-numeric values, double quotes are added. Otherwise the values would be rejected by YCQL.

Here is an example - note the quotes around 100.1:

```json
{
  "dl":1122,
  "rsrp": [
    "abc",
    { "rsrq": null },
    { "sinr":"100.1" }
  ]
}
```

##### spark.cassandra.mergeable.json.column.mapping

The JSONB field to column mapping of JSONB values should adopt merge semantics when writing.

Each mapping starts with the field names, separated by comma, then a colon, followed by the JSONB column name. The mappings are separated by semicolons.

For example, if `{"dl": 5, "ul":"foo"}` is stored in the JSONB column `usage`, and `{"x": "foo", "y":"bar"}` is stored in the JSONB column `z`, the mapping would be expressed as `dl,ul:usage;x,y:z`, where `dl` and `ul` are fields in the `usage` column, and `x` and `y` are fields in the `z` column.

This mapping is for dataframe `save()` operations where you specify the JSONB column(s) that the given JSON fields map to.

When binding individual rows, missing JSONB field values are set to `null`. This allows data in the csv file to load uninterrupted. To turn off this behaviour, use `spark.cassandra.output.ignoreNulls`.

The following example code uses mapping:

```java
Dataset<Row> updatedRows = spark
    .sql("select date as gdate,imsi,rsrp,sinr from temp ");
updatedRows.write().format("org.apache.spark.sql.cassandra").option("keyspace", KEYSPACE)
    .option("table", CDR_TABLE)
    .option("spark.cassandra.mergeable.json.column.mapping", "dl,rsrp,sinr,ul:usage")
```

##### spark.cassandra.output.ignoreNulls

Set to true to cause all null values to be left as unset rather than bound.

For example, for `{"dl": null, "ul":"foo"}`, if `spark.cassandra.output.ignoreNulls` is specified, only the `ul` field gets written (merged) into the corresponding JSONB column, and the `dl` field is ignored.
