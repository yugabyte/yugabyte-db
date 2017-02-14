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
import org.apache.commons.cli.HelpFormatter;
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
  static {
    LOG.info("Load tester UUID: " + loadTesterUUID);
  }

  // The types of workloads currently registered.
  public static enum WorkLoadType {
    CassandraRetail,
    CassandraSimpleReadWrite,
    CassandraTimeseries,
    RedisSimpleReadWrite,
  }

  // The class type of the workload.
  private Class<? extends Workload> workloadClass;
  public List<Node> nodes = new ArrayList<>();
  Random random = new Random();
  int numReaderThreads;
  int numWriterThreads;

  public Configuration(WorkLoadType workloadType, List<String> hostPortList)
      throws ClassNotFoundException {
    // Get the workload class.
    workloadClass = Class.forName("org.yb.loadtester.workload." + workloadType.toString())
                         .asSubclass(Workload.class);
    LOG.info("Workload: " + workloadClass.getSimpleName());

    for (String hostPort : hostPortList) {
      LOG.info("Adding node: " + hostPort);
      this.nodes.add(Node.fromHostPort(hostPort));
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

  public void initializeThreadCount(CommandLine cmd) {
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

  public void initializeNumKeys(CommandLine cmd) {
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

  public void initializeTableProperties(CommandLine cmd) {
    // Initialize the TTL.
    if (cmd.hasOption("table_ttl_seconds")) {
      Workload.workloadConfig.tableTTLSeconds =
          Long.parseLong(cmd.getOptionValue("table_ttl_seconds"));
    }

    LOG.info("Table TTL (secs): " + Workload.workloadConfig.tableTTLSeconds);
  }

  public static Configuration createFromArgs(String[] args) throws Exception {
    Options options = new Options();
    options.addOption("h", "help", false, "show help.");

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

    Option numThreads = OptionBuilder.create("num_threads");
    numThreads.setDescription("The total number of threads.");
    numThreads.setRequired(false);
    numThreads.setLongOpt("num_threads");
    numThreads.setArgs(1);
    options.addOption(numThreads);

    Option numReadThreads = OptionBuilder.create("num_threads_read");
    numReadThreads.setDescription("The total number of threads that perform reads.");
    numReadThreads.setRequired(false);
    numReadThreads.setLongOpt("num_threads_read");
    numReadThreads.setArgs(1);
    options.addOption(numReadThreads);

    Option numWriteThreads = OptionBuilder.create("num_threads_write");
    numWriteThreads.setDescription("The total number of threads that perform writes.");
    numWriteThreads.setRequired(false);
    numWriteThreads.setLongOpt("num_threads_write");
    numWriteThreads.setArgs(1);
    options.addOption(numWriteThreads);

    Option numWrites = OptionBuilder.create("num_writes");
    numWrites.setDescription("The total number of writes to perform.");
    numWrites.setRequired(false);
    numWrites.setLongOpt("num_writes");
    numWrites.setArgs(1);
    options.addOption(numWrites);

    Option numReads = OptionBuilder.create("num_reads");
    numReads.setDescription("The total number of reads to perform.");
    numReads.setRequired(false);
    numReads.setLongOpt("num_reads");
    numReads.setArgs(1);
    options.addOption(numReads);

    Option numUniqueKeys = OptionBuilder.create("num_unique_keys");
    numUniqueKeys.setDescription("The total number of unique keys to write into the DB.");
    numUniqueKeys.setRequired(false);
    numUniqueKeys.setLongOpt("num_unique_keys");
    numUniqueKeys.setArgs(1);
    options.addOption(numUniqueKeys);

    Option tableLevelTTLSecs = OptionBuilder.create("table_ttl_seconds");
    tableLevelTTLSecs.setDescription("The table level TTL in seconds to create the table with.");
    tableLevelTTLSecs.setRequired(false);
    tableLevelTTLSecs.setLongOpt("table_ttl_seconds");
    tableLevelTTLSecs.setArgs(1);
    options.addOption(tableLevelTTLSecs);

    Option verbose = OptionBuilder.create("verbose");
    verbose.setDescription("Enable debug level logging.");
    verbose.setRequired(false);
    verbose.setLongOpt("verbose");
    verbose.setArgs(0);
    options.addOption(verbose);

    CommandLineParser parser = new BasicParser();
    CommandLine cmd = null;

    try {
      cmd = parser.parse(options, args);
    } catch (ParseException e) {
      printUsage(options, e.getMessage());
      System.exit(0);
    }

    // Set the appropriate log level.
    Logger.getRootLogger().setLevel(cmd.hasOption("verbose") ? Level.DEBUG : Level.INFO);

    if (cmd.hasOption("h")) {
      printUsage(options, "Usage:");
      System.exit(0);
    }

    // Get the workload.
    WorkLoadType workloadType = WorkLoadType.valueOf(cmd.getOptionValue("workload"));
    // Get the proxy nodes.
    List<String> hostPortList = Arrays.asList(cmd.getOptionValue("nodes").split(","));
    // Create the configuration.
    Configuration configuration = new Configuration(workloadType, hostPortList);
    // Set the number of threads.
    configuration.initializeThreadCount(cmd);
    // Initialize num keys.
    configuration.initializeNumKeys(cmd);
    // Initialize table properties.
    configuration.initializeTableProperties(cmd);

    return configuration;
  }

  private static void printUsage(Options options, String header) {
    StringBuilder footer = new StringBuilder();
    footer.append("Valid values for 'workload' are: ");
    for (WorkLoadType workloadType : WorkLoadType.values()) {
      footer.append(workloadType.toString() + " ");
    }
    footer.append("\n");

    HelpFormatter formater = new HelpFormatter();
    formater.printHelp("LoadTester", header, options, footer.toString());
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
