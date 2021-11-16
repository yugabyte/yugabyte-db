---
title: Build a Java application using Apache Spark and YugabyteDB
headerTitle: Apache Spark
linkTitle: Apache Spark
description: Build a Java-based application using Apache Spark and YugabyteDB.
menu:
  v2.6:
    identifier: apache-spark-2-java
    parent: integrations
    weight: 572
showAsideToc: true
isTocNested: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="{{< relref "./scala.md" >}}" class="nav-link">
      <i class="icon-scala" aria-hidden="true"></i>
      Scala
    </a>
  </li>

  <li >
    <a href="{{< relref "./java.md" >}}" class="nav-link active">
      <i class="icon-java-bold" aria-hidden="true"></i>
      Java
    </a>
  </li>

  <li >
    <a href="{{< relref "./python.md" >}}" class="nav-link">
      <i class="icon-python" aria-hidden="true"></i>
      Python
    </a>
  </li>

</ul>

## Setting Up a Project with Maven

To build a Java application using the YugabyteDB Spark Connector for YCQL, add the following to your `pom.xml` file for Scala 2.12:

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

```
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

### How to Use JSONB

You can use `jsonb` data type for your columns. `jsonb` values are processed using the [Spark JSON functions](https://spark.apache.org/docs/3.0.0/sql-ref-functions-builtin.html#json-functions).

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

Note that the preceding operators are currently evaluated by Spark and not propagated to YCQL queries (as " -> "  `jsonb` operators). This is tracked by https://github.com/yugabyte/yugabyte-db/issues/6738

When the `get_json_object` function is used in the projection clause, only the target sub-object is requested and returned by YugabyteDB, as demonstrated in the following example where only the sub-object at `key[1].m[2].b` is returned:

```java
String query = "SELECT id, address, 
                get_json_object(phone, '$.key[1].m[2].b') 
                as key FROM mycatalog.test.person";
Dataset<Row> rows = spark.sql(query);
```

To confirm the pruning, you can use logging at the database-level (such as audit logging) or inspect the execution plan of Spark using the `EXPLAIN` statement. The following example creates the execution plan as a String, then logs it and checks that it contains the expected sub-object in YCQL syntax (such as `phone->'key'->1->'m'->2->'b'`):

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

For additional examples, see the following:

- [Spark 3 tests](https://github.com/yugabyte/yugabyte-db/tree/master/java/yb-cql-4x/src/test/java/org/yb/loadtest) 
- [Spark 3 sample apps](https://github.com/yugabyte/yugabyte-db/tree/master/java/yb-cql-4x/src/main/java/com/yugabyte/sample/apps)
- [TestSpark3Jsonb.java](https://github.com/yugabyte/yugabyte-db/blob/master/java/yb-cql-4x/src/test/java/org/yb/loadtest/TestSpark3Jsonb.java)

