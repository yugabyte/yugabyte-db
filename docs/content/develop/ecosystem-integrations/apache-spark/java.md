## Maven

Add the following snippet to your pom.xml:

```
<dependency>
<groupId>com.datastax.spark</groupId>
 <artifactId>spark-cassandra-connector_2.10</artifactId>
 <version>2.0.5</version>
</dependency>
```

## Sample Application

### Running the Spark word-count sample application

You can run our Spark-based sample app with:

```
java -jar yb-sample-apps.jar --workload CassandraSparkWordCount --nodes 127.0.0.1:9042
```

It reads data from a table with sentences - by default it generates an input table `ybdemo_keyspace.lines`, computes the frequencies of the words and writes the result to the output table `ybdemo_keyspace.wordcounts`.

### Examining the source code

To look at the source code, you can check:

- the source file in our GitHub source repo [here](https://github.com/YugaByte/yugabyte-db/blob/master/java/yb-loadtester/src/main/java/com/yugabyte/sample/apps/CassandraSparkWordCount.java)
- untar the jar `java/yb-sample-apps-sources.jar` in the download bundle

Most of the logic is in the `run()` method of the `CassandraSparkWordCount` class (in the file `src/main/java/com/yugabyte/sample/apps/CassandraSparkWordCount.java`). Some of the key portions of the sample program are explained in the sections below.


## Main sections of an Apache Spark program on YugaByte

### Initializing the Spark context.

The SparkConf object is configured as follows:

```
// Setup the local spark master, with the desired parallelism.
SparkConf conf = new SparkConf().setAppName("yb.wordcount")
                                .setMaster("local[1]")       // num Spark threads
                                .set("spark.cassandra.connection.host", hostname);

// Create the Java Spark context object.
JavaSparkContext sc = new JavaSparkContext(conf);

// Create the Cassandra connector to Spark.
CassandraConnector connector = CassandraConnector.apply(conf);

// Create a Cassandra session, and initialize the keyspace.
Session session = connector.openSession();
```

### Setting the input source

To set the input data for Spark, you can do one of the following.

- Reading from a table with a column `line` as the input:
```
// Read rows from table and convert them to an RDD.
JavaRDD<String> rows = javaFunctions(sc).cassandraTable(keyspace, inputTable)
                                        .select("line")
                                        .map(row -> row.getString("line"));
```

- Reading from a file as the input:
```
// Read the input file and convert it to an RDD.
JavaRDD<String> rows = sc.textFile(inputFile);
```

### Performing the word count processing

The word count is performed using the following code snippet:
```
// Perform the word count.
JavaPairRDD<String, Integer> counts = rows.flatMap(line -> Arrays.asList(line.split(" ")).iterator())
                                          .mapToPair(word -> new Tuple2<String, Integer>(word, 1))
                                          .reduceByKey((x, y) ->  x + y);
```

### Setting the output table

The output is written to the `outTable` table.
```
// Create the output table.
session.execute("CREATE TABLE IF NOT EXISTS " + outTable +
                " (word VARCHAR PRIMARY KEY, count INT);");

// Save the output to the CQL table.
javaFunctions(counts).writerBuilder(keyspace, outputTable, mapTupleToRow(String.class, Integer.class))
                     .withColumnSelector(someColumns("word", "count"))
                     .saveToCassandra();
```
