// Copyright (c) YugaByte, Inc.

package com.yugabyte.sample.apps;

import java.util.Arrays;

import com.datastax.driver.core.Metadata;
import com.datastax.driver.core.Session;
import com.yugabyte.sample.common.CmdLineOpts;
import org.apache.commons.cli.CommandLine;
import org.apache.log4j.Logger;
import org.apache.spark.SparkConf;
import org.apache.spark.api.java.JavaPairRDD;
import org.apache.spark.api.java.JavaRDD;
import org.apache.spark.api.java.JavaSparkContext;
import scala.Tuple2;
import com.datastax.spark.connector.cql.CassandraConnector;
import static com.datastax.spark.connector.japi.CassandraJavaUtil.javaFunctions;
import static com.datastax.spark.connector.japi.CassandraJavaUtil.mapTupleToRow;
import static com.datastax.spark.connector.japi.CassandraJavaUtil.someColumns;

public class CassandraSparkWordCount extends AppBase {

  private static final Logger LOG = Logger.getLogger(CassandraSparkWordCount.class);
  // Static initialization of this workload's config.
  static {
    // Set the app type to simple.
    appConfig.appType = AppConfig.Type.Simple;

    appConfig.numWriterThreads = 2;
  }
  private static String wordcount_input_file;

  // The keyspace name.
  // TODO: move to base class.
  static final String keyspace = "ybdemo";
  // The output table name.
  static final String tableName = "wordcounts";

  @Override
  public void initialize(CmdLineOpts configuration) {
    CommandLine commandLine = configuration.getCommandLine();
    if (commandLine.hasOption("wordcount_input_file")) {
      wordcount_input_file = commandLine.getOptionValue("wordcount_input_file");
      LOG.info("wordcount_input_file: " + wordcount_input_file);
    } else {
      LOG.error("wordcount_input_file not provided.");
      System.exit(0);
    }
  }

  @Override
  public void run() {
    // Setup the local spark master, with the desired parallelism.
    SparkConf conf = new SparkConf().setAppName("yb.wordcount")
        .setMaster("local[" + appConfig.numWriterThreads + "]")
        .set("spark.cassandra.connection.host", getRandomNode().getHost());

    // Create the Java Spark context object.
    JavaSparkContext sc = new JavaSparkContext(conf);

    // Create the Cassandra connector to Spark.
    CassandraConnector connector = CassandraConnector.apply(conf);

    // Create a Cassandra session, and initialize the keyspace.
    Session session = connector.openSession();

    // Drop the demo table if it already exists.
    session.execute("DROP TABLE IF EXISTS " + keyspace + "." + tableName);
    // Drop the keyspace if it already exists.
    session.execute("DROP KEYSPACE IF EXISTS " + keyspace);
    // Create a new keyspace.
    session.execute("CREATE KEYSPACE " + keyspace);
    // Create the output table.
    session.execute("CREATE TABLE " + keyspace + "." + tableName +
        " (word VARCHAR PRIMARY KEY, count INT);");

    // Read the input file and convert it to an RDD.
    JavaRDD<String> textFile = sc.textFile(wordcount_input_file);

    // Perform the word count.
    JavaPairRDD<String, Integer> counts =
        textFile.flatMap(line -> Arrays.asList(line.split(" ")).iterator())
            .mapToPair(word -> new Tuple2<String, Integer>(word, 1))
            .reduceByKey((x, y) ->  x +  y);

    // Save the output to the CQL table.
    javaFunctions(counts).writerBuilder(keyspace,
                                        tableName,
                                        mapTupleToRow(String.class, Integer.class))
        .withColumnSelector(someColumns("word", "count"))
        .saveToCassandra();

    session.close();
    sc.close();
  }

  @Override
  public String getWorkloadDescription(String optsPrefix, String optsSuffix) {
    StringBuilder sb = new StringBuilder();
    sb.append(optsPrefix);
    sb.append("Simple Spark word count app that reads a input file to compute word count and ");
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("saves results in the 'wordcounts' table");
    sb.append(optsSuffix);
    return sb.toString();
  }

  @Override
  public String getExampleUsageOptions(String optsPrefix, String optsSuffix) {
    StringBuilder sb = new StringBuilder();
    sb.append(optsPrefix);
    sb.append("--num_threads_write " + appConfig.numWriterThreads);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--spark_input_file <path to input file>");
    sb.append(optsSuffix);
    return sb.toString();
  }
}
