// Copyright (c) YugaByte, Inc.

package com.yugabyte.sample.apps;

import java.util.Arrays;

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

  private boolean useCassandraInput;

  // input source (file or Cassandra table)
  private String inputFile;
  private String inputKeyspace;
  private String inputTableName;

  // output source (Cassandra table)
  private String outputKeyspace;
  private String outputTableName;

  // default values
  private static final String defaultKeyspace = "ybdemo";
  private static final String defaultInputTableName = "lines";
  private static final String defaultOutputTableName = "wordcounts";

  private void executeAndLog(Session session, String stmt) {
    LOG.info(stmt);
    session.execute(stmt);
  }

  @Override
  public void initialize(CmdLineOpts configuration) {
    CommandLine commandLine = configuration.getCommandLine();

    //--------------------- Setting Input source (Cassandra table or file) -----------------------\\
    if (commandLine.hasOption("wordcount_input_file") &&
        commandLine.hasOption("wordcount_input_table")) {
      LOG.fatal("Input source can be EITHER file or table: found both options set");
      System.exit(1);
    } else if (commandLine.hasOption("wordcount_input_file")) {
      inputFile = commandLine.getOptionValue("wordcount_input_file");
      useCassandraInput = false;
      LOG.info("Using wordcount_input_file: " + inputFile);
    } else if (commandLine.hasOption("wordcount_input_table")) {
      String[] fragments = commandLine.getOptionValue("wordcount_input_table").split("\\.");
      if (fragments.length == 2) {
        inputKeyspace = fragments[0];
        inputTableName = fragments[1];
        useCassandraInput = true;
        LOG.info("Using wordcount_input_table: " + inputKeyspace + "." + inputTableName);
      } else {
        LOG.fatal("Expected <keyspace>.<table name> format for input table: " + fragments.length);
        System.exit(1);
      }
    } else { // defaults
      LOG.info("No input given, will create sample table and use it as input.");
      inputKeyspace = defaultKeyspace;
      inputTableName = defaultInputTableName;
      useCassandraInput = true;

      // Setting up sample table
      Session session = getCassandraClient();

      // Create keyspace and use it.
      createKeyspace(session, inputKeyspace);
      // Drop the sample table if it already exists.
      if (!configuration.getReuseExistingTable()) {
        dropCassandraTable(inputKeyspace + "." + inputTableName);
      }
      // Create the input table.
      executeAndLog(session, "CREATE TABLE IF NOT EXISTS " + inputKeyspace + "." + inputTableName +
              " (id int, line varchar, primary key(id));");

      // Insert some rows.
      String insert_stmt = "INSERT INTO " + inputKeyspace + "." + inputTableName + "(id, line) " +
              "VALUES (%d, '%s');";
      executeAndLog(session, String.format(insert_stmt, 1, "zero one two three four five"));
      executeAndLog(session, String.format(insert_stmt, 2, "nine eight seven six"));
      executeAndLog(session, String.format(insert_stmt, 3, "four two"));
      executeAndLog(session, String.format(insert_stmt, 4, "one seven seven six"));
      executeAndLog(session, String.format(insert_stmt, 5, "three one four one five nine two six"));
      executeAndLog(session, String.format(insert_stmt, 6, "two seven one eight two eight one"));

      LOG.info("Created sample table " + inputKeyspace + "." + inputTableName);
    }

    //----------------------- Setting Output location (Cassandra table) --------------------------\\
    if (commandLine.hasOption("wordcount_output_table")) {
      String[] fragments = commandLine.getOptionValue("wordcount_input_table").split("\\.");
      if (fragments.length == 2) {
        outputKeyspace = fragments[0];
        outputTableName = fragments[1];
        useCassandraInput = true;
        LOG.info("Using wordcount_output_table: " + outputKeyspace + "." + outputTableName);
      } else {
        LOG.fatal("Expected <keyspace>.<table name> format for output table: " + fragments.length);
        System.exit(1);
      }
    } else { // defaults
      outputKeyspace = defaultKeyspace;
      outputTableName = defaultOutputTableName;
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

    //------------------------------------------- Input ------------------------------------------\\
    JavaRDD<String> rows;
    if (useCassandraInput) {
      // Read rows from table and convert them to an RDD.
      rows = javaFunctions(sc).cassandraTable(inputKeyspace, inputTableName)
              .select("line").map(row -> row.getString("line"));
    } else {
      // Read the input file and convert it to an RDD.
      rows = sc.textFile(inputFile);
    }

    // Perform the word count.
    JavaPairRDD<String, Integer> counts =
        rows.flatMap(line -> Arrays.asList(line.split(" ")).iterator())
            .mapToPair(word -> new Tuple2<String, Integer>(word, 1))
            .reduceByKey((x, y) ->  x +  y);

    //------------------------------------------- Output -----------------------------------------\\
    createKeyspace(session, outputKeyspace);
    // Drop the output table if it already exists.
    if (!this.configuration.getReuseExistingTable()) {
      dropCassandraTable(outputKeyspace + "." + outputTableName);
    }
    // Create the output table.
    session.execute("CREATE TABLE IF NOT EXISTS " + outputKeyspace + "." + outputTableName +
            " (word VARCHAR PRIMARY KEY, count INT);");

    // Save the output to the CQL table.
    javaFunctions(counts).writerBuilder(outputKeyspace,
            outputTableName,
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
    sb.append("Simple Spark word count app that reads from an input table or file to compute ");
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("word count and saves results in an output table");
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("Input source is either input_file or input_table. If none is given a sample ");
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("Cassandra table " + defaultKeyspace + "." + defaultInputTableName +
            " is created and used as input");
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
    sb.append("--wordcount_output_table " + defaultKeyspace+ "." + defaultOutputTableName);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--wordcount_input_file <path to input file>");
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--wordcount_input_table <keyspace>.<table name>");
    sb.append(optsSuffix);
    return sb.toString();
  }
}
