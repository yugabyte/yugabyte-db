// Copyright (c) YugaByte, Inc.

package org.yb.loadtester.common;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Random;
import java.util.UUID;

import org.apache.commons.cli.BasicParser;
import org.apache.commons.cli.CommandLine;
import org.apache.commons.cli.CommandLineParser;
import org.apache.commons.cli.Option;
import org.apache.commons.cli.OptionBuilder;
import org.apache.commons.cli.Options;
import org.apache.commons.cli.ParseException;
import org.apache.log4j.Level;
import org.apache.log4j.Logger;
import org.yb.loadtester.Workload;

public class Configuration {
  private static final Logger LOG = Logger.getLogger(Configuration.class);

  public static final UUID loadTesterUUID = UUID.randomUUID();

  // The types of workloads currently registered.
  public static enum WorkLoadType {
//    CassandraRetail,
    CassandraKeyValue,
    CassandraStockTicker,
    CassandraTimeseries,
    RedisKeyValue,
  }

  // The class type of the workload.
  private Class<? extends Workload> workloadClass;
  public List<Node> nodes = new ArrayList<>();
  Random random = new Random();
  int numReaderThreads;
  int numWriterThreads;
  boolean reuseExistingTable = false;
  CommandLine commandLine;

  public void initialize(CommandLine commandLine) throws ClassNotFoundException {
    this.commandLine = commandLine;
    // Get the workload.
    WorkLoadType workloadType = WorkLoadType.valueOf(commandLine.getOptionValue("workload"));
    // Get the proxy nodes.
    List<String> hostPortList = Arrays.asList(commandLine.getOptionValue("nodes").split(","));
    // Get the workload class.
    workloadClass = getWorkloadClass(workloadType);
    LOG.info("Workload: " + workloadClass.getSimpleName());
    for (String hostPort : hostPortList) {
      LOG.info("Adding node: " + hostPort);
      this.nodes.add(Node.fromHostPort(hostPort));
    }
    // Set the number of threads.
    initializeThreadCount(commandLine);
    // Initialize num keys.
    initializeNumKeys(commandLine);
    // Initialize table properties.
    initializeTableProperties(commandLine);
    // Check if we should drop existing tables.
    if (commandLine.hasOption("reuse_table")) {
      reuseExistingTable = true;
    }
  }

  public Workload getWorkloadInstance() {
    Workload workload = null;
    try {
      // Create a new workload object.
      workload = workloadClass.newInstance();
      // Initialize the workload.
      workload.workloadInit(this);
    } catch (Exception e) {
      LOG.error("Could not create instance of " + workloadClass.getName(), e);
    }
    return workload;
  }

  public CommandLine getCommandLine() {
    return commandLine;
  }

  public List<Node> getNodes() {
    return nodes;
  }

  public Node getRandomNode() {
    int nodeId = random.nextInt(nodes.size());
    LOG.debug("Returning random node id " + nodeId);
    return nodes.get(nodeId);
  }

  public int getNumReaderThreads() {
    return numReaderThreads;
  }

  public int getNumWriterThreads() {
    return numWriterThreads;
  }

  public boolean getReuseExistingTable() {
    return reuseExistingTable;
  }

  private static Class<? extends Workload> getWorkloadClass(WorkLoadType workloadType)
      throws ClassNotFoundException{
    // Get the workload class.
    return Class.forName("org.yb.loadtester.workload." + workloadType.toString())
                         .asSubclass(Workload.class);
  }

  private void initializeThreadCount(CommandLine cmd) {
    // Check if there are a fixed number of threads or variable.
    String numThreadsStr = cmd.getOptionValue("num_threads");
    if (Workload.workloadConfig.readIOPSPercentage == -1) {
      numReaderThreads = Workload.workloadConfig.numReaderThreads;
      numWriterThreads = Workload.workloadConfig.numWriterThreads;
    } else {
      int numThreads = 0;
      if (numThreadsStr != null) {
          numThreads = Integer.parseInt(numThreadsStr);
      } else {
        // Default to 8 * num-cores
        numThreads = 8 * Runtime.getRuntime().availableProcessors();
      }
      numReaderThreads =
          (int) Math.round(1.0 * numThreads * Workload.workloadConfig.readIOPSPercentage / 100);
      numWriterThreads = numThreads - numReaderThreads;
    }

    // If number of read and write threads are specified on the command line, that overrides all
    // the other values.
    if (cmd.hasOption("num_threads_read")) {
      numReaderThreads = Integer.parseInt(cmd.getOptionValue("num_threads_read"));
    }
    if (cmd.hasOption("num_threads_write")) {
      numWriterThreads = Integer.parseInt(cmd.getOptionValue("num_threads_write"));
    }
    LOG.info("Num reader threads: " + numReaderThreads +
             ", num writer threads: " + numWriterThreads);
  }

  private void initializeNumKeys(CommandLine cmd) {
    if (cmd.hasOption("num_writes")) {
      Workload.workloadConfig.numKeysToWrite = Long.parseLong(cmd.getOptionValue("num_writes"));
    }
    if (cmd.hasOption("num_reads")) {
      Workload.workloadConfig.numKeysToRead = Long.parseLong(cmd.getOptionValue("num_reads"));
    }
    if (cmd.hasOption("num_unique_keys")) {
      Workload.workloadConfig.numUniqueKeysToWrite =
          Long.parseLong(cmd.getOptionValue("num_unique_keys"));
    }
    LOG.info("Num keys to insert: " + Workload.workloadConfig.numUniqueKeysToWrite);
    LOG.info("Num keys to update: " +
        (Workload.workloadConfig.numKeysToWrite - Workload.workloadConfig.numUniqueKeysToWrite));
    LOG.info("Num keys to read: " + Workload.workloadConfig.numKeysToRead);
  }

  private void initializeTableProperties(CommandLine cmd) {
    // Initialize the TTL.
    if (cmd.hasOption("table_ttl_seconds")) {
      Workload.workloadConfig.tableTTLSeconds =
          Long.parseLong(cmd.getOptionValue("table_ttl_seconds"));
    }

    LOG.info("Table TTL (secs): " + Workload.workloadConfig.tableTTLSeconds);
  }

  public static Configuration createFromArgs(String[] args) throws Exception {
    Options options = new Options();

    Option appType = OptionBuilder.create("workload");
    appType.setDescription("The workload to run.");
    appType.setRequired(true);
    appType.setLongOpt("workload");
    appType.setArgs(1);
    options.addOption(appType);

    Option proxyAddrs = OptionBuilder.create("nodes");
    proxyAddrs.setDescription("Comma separated proxies, host1:port1,....,hostN:portN");
    proxyAddrs.setRequired(true);
    proxyAddrs.setLongOpt("nodes");
    proxyAddrs.setArgs(1);
    options.addOption(proxyAddrs);

    options.addOption("help", false, "Show help message.");
    options.addOption("verbose", false, "Enable debug level logging.");

    options.addOption("reuse_table", false, "Reuse table if it already exists.");
    options.addOption("num_threads", true, "The total number of threads.");
    options.addOption("num_threads_read", true, "The number of threads that perform reads.");
    options.addOption("num_threads_write", true, "The number of threads that perform writes.");
    options.addOption("num_writes", true, "The total number of writes to perform.");
    options.addOption("num_reads", true, "The total number of reads to perform.");
    options.addOption("table_ttl_seconds", true, "The TTL in seconds to create the table with.");

    // Options for CassandraTimeseries workload.
    options.addOption("num_users", true, "[CassandraTimeseries] The total number of users.");
    options.addOption("min_nodes_per_user", true,
                      "[CassandraTimeseries] The min number of nodes per user.");
    options.addOption("min_nodes_per_user", true,
                      "[CassandraTimeseries] The min number of nodes per user.");
    options.addOption("max_nodes_per_user", true,
                      "[CassandraTimeseries] The max number of nodes per user.");
    options.addOption("min_metrics_count", true,
                      "[CassandraTimeseries] The min number of metrics for all nodes of user.");
    options.addOption("max_metrics_count", true,
                      "[CassandraTimeseries] The max number of metrics for all nodes of user.");
    options.addOption("data_emit_rate_millis", true,
                      "[CassandraTimeseries] The rate at which each node emits all metrics.");

    // Options for CassandraStockTicker workload.
    options.addOption("num_ticker_symbols", true,
                      "[CassandraStockTicker] The total number of stock ticker symbols.");

    // Options for the key-value workloads.
    options.addOption("num_unique_keys", true,
                      "[KV workloads only] Number of unique keys to write into the DB.");

    CommandLineParser parser = new BasicParser();
    CommandLine commandLine = null;

    try {
      commandLine = parser.parse(options, args);
    } catch (ParseException e) {
      try {
        printUsage(options, e.getMessage());
      } catch (Exception e1) {
        LOG.error("Hit error parsing command line", e1);
      }
      System.exit(0);
    }

    // Set the appropriate log level.
    Logger.getRootLogger().setLevel(commandLine.hasOption("verbose") ? Level.DEBUG : Level.INFO);

    if (commandLine.hasOption("help")) {
      try {
        printUsage(options, "Usage:");
      } catch (Exception e) {
        LOG.error("Hit error parsing command line", e);
      }
      System.exit(0);
    }

    Configuration configuration = new Configuration();
    configuration.initialize(commandLine);
    return configuration;
  }

  private static void printUsage(Options options, String header) throws Exception {
    StringBuilder footer = new StringBuilder();

    footer.append("****************************************************************************\n");
    footer.append("*                                                                          *\n");
    footer.append("*                     YugaByte Platform Demo App                           *\n");
    footer.append("*                                                                          *\n");
    footer.append("****************************************************************************\n");
    footer.append("\n");
    footer.append("Use this demo app to try out a variety of workloads against the YugaByte data " +
                  "platform.\n");
    String optsPrefix = "\t\t\t";
    String optsSuffix = " \\\n";
    for (WorkLoadType workloadType : WorkLoadType.values()) {
      int port = 0;
      if (workloadType.toString().startsWith("Cassandra")) port = 9042;
      else if (workloadType.toString().startsWith("Redis")) port = 6379;
      Workload workload = getWorkloadClass(workloadType).newInstance();

      footer.append("\n - " + workloadType.toString() + " :\n");
      footer.append("   ");
      for (int idx = 0; idx < workloadType.toString().length(); idx++) {
        footer.append("-");
      }
      footer.append("\n");

      String description = workload.getWorkloadDescription("\t\t", "\n");
      if (!description.isEmpty()) {
        footer.append(description + "\n");
      }
      footer.append("\t\tUsage:\n");
      footer.append(optsPrefix);
      footer.append("java -jar yb-loadtester-0.8.0-SNAPSHOT-jar-with-dependencies.jar");
      footer.append(optsSuffix);
      footer.append(optsPrefix + "--workload " + workloadType.toString() + optsSuffix);
      footer.append(optsPrefix + "--nodes 127.0.0.1:" + port);

      footer.append("\n\n\t\tOther options (with default values):\n");
      footer.append(workload.getExampleUsageOptions(optsPrefix + "[ ", " ]\n"));
    }
    footer.append("\n");
    System.out.println(footer.toString());
    System.exit(0);
  }

  public static class Node {
    String host;
    int port;

    public Node(String host, int port) {
      this.host = host;
      this.port = port;
    }

    public String getHost() {
      return host;
    }

    public int getPort() {
      return port;
    }

    public static Node fromHostPort(String hostPort) {
      String[] parts = hostPort.split(":");
      String host = parts[0];
      int port = Integer.parseInt(parts[1]);
      return new Node(host, port);
    }
  }
}
