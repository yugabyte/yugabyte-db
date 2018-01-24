// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package com.yugabyte.sample.common;

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
import org.apache.log4j.Logger;

import com.yugabyte.sample.apps.AppBase;

/**
 * This is a helper class to parse the user specified command-line options if they were specified,
 * print help messages when running the app, etc.
 */
public class CmdLineOpts {
  private static final Logger LOG = Logger.getLogger(CmdLineOpts.class);

  // This is a unique UUID that is created by each instance of the application. This UUID is used in
  // various apps to make the keys unique. This allows us to run multiple instances of the app
  // safely.
  public static UUID loadTesterUUID;

  // The various apps present in this sample.
  public static enum AppName {
    CassandraHelloWorld,
    CassandraKeyValue,
    CassandraBatchKeyValue,
    CassandraBatchTimeseries,
    CassandraTransactionalKeyValue,
    CassandraStockTicker,
    CassandraTimeseries,
    CassandraUserId,
    CassandraPersonalization,
    CassandraSparkWordCount,
    CassandraSparkKeyValueCopy,
    RedisKeyValue,
    RedisPipelinedKeyValue,
    RedisHashPipelined
  }

  // The class type of the app needed to spawn new objects.
  private Class<? extends AppBase> appClass;
  // List of database contact points. One contact point could be resolved to multiple nodes IPs.
  public List<ContactPoint> contactPoints = new ArrayList<>();
  // The number of reader threads to spawn for OLTP apps.
  int numReaderThreads;
  // The number of writer threads to spawn for OLTP apps.
  int numWriterThreads;
  boolean readOnly = false;
  boolean localReads = false;
  // Random number generator.
  Random random = new Random();
  // Command line opts parser.
  CommandLine commandLine;

  public void initialize(CommandLine commandLine) throws ClassNotFoundException {
    this.commandLine = commandLine;
    if (commandLine.hasOption("uuid")) {
      loadTesterUUID = UUID.fromString(commandLine.getOptionValue("uuid"));
      LOG.info("Using given UUID : " + loadTesterUUID);
    } else if (commandLine.hasOption("nouuid")) {
      loadTesterUUID = null;
      LOG.info("Using NO UUID");
    } else {
      loadTesterUUID = UUID.randomUUID();
      LOG.info("Using a randomly generated UUID : " + loadTesterUUID);
    }

    // Get the workload.
    AppName appName = AppName.valueOf(commandLine.getOptionValue("workload"));
    appClass = getAppClass(appName);
    LOG.info("App: " + appClass.getSimpleName());

    if (commandLine.hasOption("skip_workload")) {
      AppBase.appConfig.skipWorkload = true;
      // For now, drop table is the only op that is permitted without workload.
      if (!commandLine.hasOption("drop_table_name")) {
        LOG.error("Table name to be dropped is expected when skipping workload run.");
        System.exit(1);
      }
    }

    if (commandLine.hasOption("run_time")) {
      AppBase.appConfig.runTimeSeconds =
          Integer.parseInt(commandLine.getOptionValue("run_time"));
    }
    LOG.info("Run time (seconds): " + AppBase.appConfig.runTimeSeconds);

    // Get the proxy contact points.
    List<String> hostPortList = Arrays.asList(commandLine.getOptionValue("nodes").split(","));
    for (String hostPort : hostPortList) {
      LOG.info("Adding node: " + hostPort);
      this.contactPoints.add(ContactPoint.fromHostPort(hostPort));
    }

    // This check needs to be done before initializeThreadCount is called.
    if (commandLine.hasOption("read_only")) {
      AppBase.appConfig.readOnly = true;
      readOnly = true;
      if (!commandLine.hasOption("uuid") && !commandLine.hasOption("nouuid")) {
        LOG.error("uuid (or nouuid) needs to be provided when using --read-only");
        System.exit(1);
      }
    }

    // Set the number of threads.
    initializeThreadCount(commandLine);
    // Initialize num keys.
    initializeNumKeys(commandLine);
    // Initialize table properties.
    initializeTableProperties(commandLine);
    if (commandLine.hasOption("local_reads")) {
      AppBase.appConfig.localReads = true;
      localReads = true;
    }
    LOG.info("Local reads: " + localReads);
    LOG.info("Read only load: " + readOnly);
    if (appName == AppName.CassandraBatchTimeseries) {
      if (commandLine.hasOption("read_batch_size")) {
        AppBase.appConfig.cassandraReadBatchSize =
            Integer.parseInt(commandLine.getOptionValue("read_batch_size"));
        LOG.info("CassandraBatchTimeseries batch size: " +
                 AppBase.appConfig.cassandraReadBatchSize);
      }
      if (commandLine.hasOption("read_back_delta_from_now")) {
        AppBase.appConfig.readBackDeltaTimeFromNow =
            Integer.parseInt(commandLine.getOptionValue("read_back_delta_from_now"));
        LOG.info("CassandraBatchTimeseries delta read: " +
                 AppBase.appConfig.readBackDeltaTimeFromNow);
      }
    }
    if (appName == AppName.CassandraBatchKeyValue) {
      if (commandLine.hasOption("batch_size")) {
        AppBase.appConfig.cassandraBatchSize =
            Integer.parseInt(commandLine.getOptionValue("batch_size"));
        if (AppBase.appConfig.cassandraBatchSize > AppBase.appConfig.numUniqueKeysToWrite) {
          LOG.fatal("The batch size cannot be more than the number of unique keys");
          System.exit(-1);
        }
      }
      LOG.info("CassandraBatchKeyValue batch size : " + AppBase.appConfig.cassandraBatchSize);
    }
    if (appName == AppName.CassandraPersonalization) {
      if (commandLine.hasOption("num_stores")) {
        AppBase.appConfig.numStores = Integer.parseInt(commandLine.getOptionValue("num_stores"));
      }
      LOG.info("CassandraPersonalization number of stores : " + AppBase.appConfig.numStores);
      if (commandLine.hasOption("num_new_coupons_per_customer")) {
        AppBase.appConfig.numNewCouponsPerCustomer =
          Integer.parseInt(commandLine.getOptionValue("num_new_coupons_per_customer"));
      }
      LOG.info("CassandraPersonalization number of new coupons per costomer : " +
               AppBase.appConfig.numNewCouponsPerCustomer);
      if (commandLine.hasOption("max_coupons_per_customer")) {
        AppBase.appConfig.maxCouponsPerCustomer =
          Integer.parseInt(commandLine.getOptionValue("max_coupons_per_customer"));
      }
      if (AppBase.appConfig.numNewCouponsPerCustomer >
          AppBase.appConfig.maxCouponsPerCustomer) {
        LOG.fatal(
            "The number of new coupons cannot exceed the maximum number of coupons per customer");
        System.exit(-1);
      }
      LOG.info("CassandraPersonalization maximum number of coupons per costomer : " +
               AppBase.appConfig.maxCouponsPerCustomer);
    }
    if (appName == AppName.RedisPipelinedKeyValue ||
        appName == AppName.RedisHashPipelined) {
      if (commandLine.hasOption("pipeline_length")) {
        AppBase.appConfig.redisPipelineLength =
            Integer.parseInt(commandLine.getOptionValue("pipeline_length"));
        if (AppBase.appConfig.redisPipelineLength > AppBase.appConfig.numUniqueKeysToWrite) {
          LOG.fatal("The pipeline length cannot be more than the number of unique keys");
          System.exit(-1);
        }
      }
      LOG.info("RedisPipelinedKeyValue pipeline length : " + AppBase.appConfig.redisPipelineLength);
    }
    if (appName == AppName.RedisHashPipelined) {
      if (commandLine.hasOption("num_subkeys_per_key")) {
        AppBase.appConfig.numSubkeysPerKey =
            Integer.parseInt(commandLine.getOptionValue("num_subkeys_per_key"));
        if (AppBase.appConfig.redisPipelineLength >
            AppBase.appConfig.numUniqueKeysToWrite) {
          LOG.fatal("The pipeline length cannot be more than the number of unique keys");
          System.exit(-1);
        }
      }
      if (commandLine.hasOption("key_freq_zipf_exponent")) {
        AppBase.appConfig.keyUpdateFreqZipfExponent = Double.parseDouble(
            commandLine.getOptionValue("key_freq_zipf_exponent"));
      }
      if (commandLine.hasOption("subkey_freq_zipf_exponent")) {
        AppBase.appConfig.subkeyUpdateFreqZipfExponent = Double.parseDouble(
            commandLine.getOptionValue("subkey_freq_zipf_exponent"));
      }
      if (commandLine.hasOption("subkey_value_size_zipf_exponent")) {
        AppBase.appConfig.valueSizeZipfExponent = Double.parseDouble(
            commandLine.getOptionValue("subkey_value_size_zipf_exponent"));
      }
      if (commandLine.hasOption("subkey_value_max_size")) {
        AppBase.appConfig.maxValueSize = Integer.parseInt(
            commandLine.getOptionValue("subkey_value_max_size"));
      }
      if (commandLine.hasOption("num_subkeys_per_write")) {
        AppBase.appConfig.numSubkeysPerWrite = Integer.parseInt(
            commandLine.getOptionValue("num_subkeys_per_write"));
        if (AppBase.appConfig.numSubkeysPerWrite >
            AppBase.appConfig.numSubkeysPerKey) {
          LOG.fatal("Writing more subkeys than the number of subkeys per key.");
          System.exit(-1);
        }
      }
      if (commandLine.hasOption("num_subkeys_per_read")) {
        AppBase.appConfig.numSubkeysPerRead = Integer.parseInt(
            commandLine.getOptionValue("num_subkeys_per_read"));
        if (AppBase.appConfig.numSubkeysPerRead >
            AppBase.appConfig.numSubkeysPerKey) {
          LOG.fatal("Writing more subkeys than the number of subkeys per key.");
          System.exit(-1);
        }
      }
    }
    if (commandLine.hasOption("with_local_dc")) {
      if (AppBase.appConfig.disableYBLoadBalancingPolicy == true) {
        LOG.error("--disable_yb_load_balancing_policy cannot be used with --with_local_dc");
        System.exit(1);
      }
      AppBase.appConfig.localDc = commandLine.getOptionValue("with_local_dc");
    }
  }

  /**
   * Creates new instance of the app.
   */
  public AppBase createAppInstance() {
    return createAppInstance(true /* enableMetrics */);
  }

  /**
   * Creates new instance of the app.
   * @param enableMetrics Should metrics tracker be enabled.
   */
  public AppBase createAppInstance(boolean enableMetrics) {
    AppBase workload = null;
    try {
      // Create a new workload object.
      workload = appClass.newInstance();
      // Initialize the workload.
      workload.workloadInit(this, enableMetrics);
    } catch (Exception e) {
      LOG.error("Could not create instance of " + appClass.getName(), e);
    }
    return workload;
  }

  public CommandLine getCommandLine() {
    return commandLine;
  }

  public List<ContactPoint> getContactPoints() {
    return contactPoints;
  }

  public ContactPoint getRandomContactPoint() {
    int contactPointId = random.nextInt(contactPoints.size());
    LOG.debug("Returning random contact point id " + contactPointId);
    return contactPoints.get(contactPointId);
  }

  public int getNumReaderThreads() {
    return numReaderThreads;
  }

  public int getNumWriterThreads() {
    return numWriterThreads;
  }

  public boolean getReadOnly() {
    return readOnly;
  }

  public boolean doErrorChecking() {
    return AppBase.appConfig.sanityCheckAtEnd;
  }

  public boolean shouldDropTable() {
    return AppBase.appConfig.shouldDropTable;
  }

  public boolean skipWorkload() {
    return AppBase.appConfig.skipWorkload;
  }

  private static Class<? extends AppBase> getAppClass(AppName workloadType)
      throws ClassNotFoundException{
    // Get the workload class.
    return Class.forName("com.yugabyte.sample.apps." + workloadType.toString())
                         .asSubclass(AppBase.class);
  }

  private void initializeThreadCount(CommandLine cmd) {
    // Check if there are a fixed number of threads or variable.
    String numThreadsStr = cmd.getOptionValue("num_threads");
    if (readOnly) {
      numReaderThreads = AppBase.appConfig.numReaderThreads;
      numWriterThreads = 0;
    } else if (AppBase.appConfig.readIOPSPercentage == -1) {
      numReaderThreads = AppBase.appConfig.numReaderThreads;
      numWriterThreads = AppBase.appConfig.numWriterThreads;
    } else {
      int numThreads = 0;
      if (numThreadsStr != null) {
          numThreads = Integer.parseInt(numThreadsStr);
      } else {
        // Default to 8 * num-cores
        numThreads = 8 * Runtime.getRuntime().availableProcessors();
      }
      numReaderThreads =
          (int) Math.round(1.0 * numThreads * AppBase.appConfig.readIOPSPercentage / 100);
      numWriterThreads = numThreads - numReaderThreads;
    }

    // If number of read and write threads are specified on the command line, that overrides all
    // the other values.
    if (cmd.hasOption("num_threads_read")) {
      numReaderThreads = Integer.parseInt(cmd.getOptionValue("num_threads_read"));
    }
    if (cmd.hasOption("num_threads_write")) {
      if (readOnly) {
        LOG.warn("Ignoring num_threads_write option. It shouldn't be used with read_only.");
      } else {
        numWriterThreads = Integer.parseInt(cmd.getOptionValue("num_threads_write"));
      }
    }
    LOG.info("Num reader threads: " + numReaderThreads +
             ", num writer threads: " + numWriterThreads);
  }

  private void initializeNumKeys(CommandLine cmd) {
    if (cmd.hasOption("num_writes")) {
      AppBase.appConfig.numKeysToWrite = Long.parseLong(cmd.getOptionValue("num_writes"));
    }
    if (cmd.hasOption("num_reads")) {
      AppBase.appConfig.numKeysToRead = Long.parseLong(cmd.getOptionValue("num_reads"));
    }
    if (cmd.hasOption("num_unique_keys")) {
      AppBase.appConfig.numUniqueKeysToWrite =
          Long.parseLong(cmd.getOptionValue("num_unique_keys"));
    }
    AppBase.appConfig.maxWrittenKey = Long.parseLong(cmd.getOptionValue("max_written_key",
        String.valueOf(AppBase.appConfig.maxWrittenKey)));
    if (cmd.hasOption("value_size")) {
      AppBase.appConfig.valueSize = Integer.parseInt(cmd.getOptionValue("value_size"));
    }
    if (cmd.hasOption("sleep_time")) {
      AppBase.appConfig.sleepTime =
          Integer.parseInt(cmd.getOptionValue("sleep_time"));
    }
    if (cmd.hasOption("socket_timeout")) {
      AppBase.appConfig.jedisSocketTimeout =
          Integer.parseInt(cmd.getOptionValue("socket_timeout"));
    }
    if (cmd.hasOption("use_ascii_values")) {
      AppBase.appConfig.restrictValuesToAscii = true;
    }
    if (cmd.hasOption("sanity_check_at_end")) {
      AppBase.appConfig.sanityCheckAtEnd = true;
    }
    if (cmd.hasOption("disable_yb_load_balancing_policy")) {
      AppBase.appConfig.disableYBLoadBalancingPolicy = true;
    }
    if (cmd.hasOption("print_all_exceptions")) {
      AppBase.appConfig.printAllExceptions = true;
    }
    if (cmd.hasOption("create_table_name") && cmd.hasOption("drop_table_name")) {
      LOG.error("Both create and drop table options cannot be provided together.");
      System.exit(1);
    }
    if (cmd.hasOption("create_table_name")) {
      AppBase.appConfig.tableName = cmd.getOptionValue("create_table_name");
      LOG.info("Create table name: " + AppBase.appConfig.tableName);
    }
    if (cmd.hasOption("drop_table_name")) {
      AppBase.appConfig.tableName = cmd.getOptionValue("drop_table_name");
      LOG.info("Drop table name: " + AppBase.appConfig.tableName);
      AppBase.appConfig.shouldDropTable = true;
    }
    LOG.info("Num unique keys to insert: " + AppBase.appConfig.numUniqueKeysToWrite);
    LOG.info("Num keys to update: " +
        (AppBase.appConfig.numKeysToWrite - AppBase.appConfig.numUniqueKeysToWrite));
    LOG.info("Num keys to read: " + AppBase.appConfig.numKeysToRead);
    LOG.info("Value size: " + AppBase.appConfig.valueSize);
    LOG.info("Restrict values to ASCII strings: " + AppBase.appConfig.restrictValuesToAscii);
    LOG.info("Perform sanity check at end of app run: " + AppBase.appConfig.sanityCheckAtEnd);
  }

  private void initializeTableProperties(CommandLine cmd) {
    // Initialize the TTL.
    if (cmd.hasOption("table_ttl_seconds")) {
      AppBase.appConfig.tableTTLSeconds =
          Long.parseLong(cmd.getOptionValue("table_ttl_seconds"));
    }

    LOG.info("Table TTL (secs): " + AppBase.appConfig.tableTTLSeconds);
  }

  /**
   * Creates a command line opts object from the arguments specified on the command line.
   * @param
   * @return a CmdLineOpts object
   * @throws Exception
   */
  public static CmdLineOpts createFromArgs(String[] args) throws Exception {
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

    options.addOption("uuid", true, "The UUID to use for this loadtester.");
    options.addOption("nouuid", false,
                      "Do not use a UUID. Keys will be key:1, key:2, key:3, "
                          + "instead of <uuid>:1, <uuid>:2, <uuid>:3 etc.");
    options.addOption("create_table_name", true, "The name of the CQL table to create.");
    options.addOption("drop_table_name", true, "The name of the CQL table to drop.");
    options.addOption("read_only", false, "Read-only workload. " +
        "Values must have been written previously and uuid must be provided. " +
        "num_threads_write will be ignored.");
    options.addOption("local_reads", false, "Use consistency ONE for reads.");
    options.addOption("num_threads", true, "The total number of threads.");
    options.addOption("num_threads_read", true, "The number of threads that perform reads.");
    options.addOption("num_threads_write", true, "The number of threads that perform writes.");
    options.addOption("num_writes", true, "The total number of writes to perform.");
    options.addOption("num_reads", true, "The total number of reads to perform.");
    options.addOption(
        "sleep_time", true,
        "How long (in ms) to sleep between multiple pipeline batches.");
    options.addOption("socket_timeout", true,
                      "How long (in ms) to wait for a response from jedis.");
    options.addOption("value_size", true, "Size in bytes of the value. " +
        "The bytes are random. Value size should be more than 5 (9) bytes for binary (ascii) " +
        "values in order to have checksum for read verification. First byte is used as a " +
        "binary/ascii marker. If value size is more than 16 bytes, random content is prepended " +
        "with \"val: $key\" string, so we can check if value matches the key during read.");
    options.addOption("use_ascii_values", false, "[RedisKeyValue] If " +
        "specified, values are restricted to ASCII strings.");
    options.addOption("table_ttl_seconds", true, "The TTL in seconds to create the table with.");
    options.addOption("sanity_check_at_end", false,
        "Add FATAL logs to ensure no failures before terminating the app.");
    options.addOption("disable_yb_load_balancing_policy", false,
        "Disable Yugabyte load-balancing policy.");
    options.addOption("print_all_exceptions", false,
        "Print all exceptions encountered on the client, instead of sampling.");
    options.addOption("skip_workload", false, "Skip running workload.");
    options.addOption("run_time", true,
        "Run time for workload. Negative value means forever (default).");

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
    options.addOption("max_written_key", true,
        "[KV workloads only, reusing existing table] Max written key number.");

    // Options for CassandraBatchKeyValue app.
    options.addOption("batch_size", true,
                      "[CassandraBatchKeyValue] Number of keys to write in a batch.");

    // Options for CassandraBatchTimeseries app.
    options.addOption("read_batch_size", true,
                      "[CassandraBatchTimeseries] Number of keys to read in a batch.");
    options.addOption("read_back_delta_from_now", true,
                      "[CassandraBatchTimeseries] Time unit delta back from current time unit.");

    options.addOption("with_local_dc", true, "Local DC name.");

    // Options for CassandraSparkWordCount app.
    options.addOption("wordcount_input_file", true,
                      "[CassandraSparkWordCount] Input file with words to count.");

    options.addOption("wordcount_input_table", true,
                      "[CassandraSparkWordCount] Input table with words to count.");

    options.addOption("wordcount_output_table", true,
                      "[CassandraSparkWordCount] Output table to write wordcounts to.");

    // Options for CassandraPersonalization app.
    options.addOption("num_stores", true,
                      "[CassandraPersonalization] Number of stores.");
    options.addOption("num_new_coupons_per_customer", true,
                      "[CassandraPersonalization] Number of new coupons per customer.");
    options.addOption("max_coupons_per_customer", true,
                      "[CassandraPersonalization] Maximum number of coupons per customer.");

    // Options for CassandraSparkKeyValueCopy app.
    options.addOption("keyvaluecopy_input_table", true,
        "[CassandraSparkKeyValueCopy] Input table with text keys and blob values.");

    options.addOption("keyvaluecopy_output_table", true,
        "[CassandraSparkKeyValueCopy] Output table to copy the key-value pairs to.");

    // Options for Redis Pipelined Key Value
    options.addOption(
        "pipeline_length", true,
        "[RedisPipelinedKeyValue/RedisHashPipelined] Number of commands to be sent out"
            + " in a redis pipelined sync.");

    options.addOption(
        "num_subkeys_per_key", true,
        "[RedisHashPipelined] Number of subkeys in each key for the RedisHashPipelined workload.");
    options.addOption(
        "num_subkeys_per_write", true,
        "[RedisHashPipelined] Number of subkeys updated in each HMSet "
            + "for the RedisHashPipelined workload.");
    options.addOption(
        "num_subkeys_per_read", true,
        "[RedisHashPipelined] Number of subkeys read in each HMGet "
            + "for the RedisHashPipelined workload.");

    options.addOption(
        "key_freq_zipf_exponent", true,
        "[RedisHashPipelined] The zipf distribution exponent, if keys " +
        "should be picked using a Zipf distribution. If <= 0, we use " +
        "a uniform distribution");
    options.addOption(
        "subkey_freq_zipf_exponent", true,
        "[RedisHashPipelined] The zipf distribution exponent, if subkeys " +
        "should be picked using a Zipf distribution. If <= 0, we use " +
        "a uniform distribution");
    options.addOption(
        "subkey_value_size_zipf_exponent", true,
        "[RedisHashPipelined] The zipf distribution exponent, if the value " +
        "sizes should be picked using a Zipf distribution. Value sizes are " +
        "chosen such that the expected mean is the value specified by --value_size. " +
        "If <= 0, all subkeys will have the value specified by --value_size");
    options.addOption(
        "subkey_value_max_size", true,
        "[RedisHashPipelined] If using zipf distribution to choose value sizes, " +
        "specifies an upper bound on the value sizes.");

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
    LogUtil.configureLogLevel(commandLine.hasOption("verbose"));

    if (commandLine.hasOption("help")) {
      try {
        printUsage(options, "Usage:");
      } catch (Exception e) {
        LOG.error("Hit error parsing command line", e);
      }
      System.exit(0);
    }

    CmdLineOpts configuration = new CmdLineOpts();
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
    for (AppName workloadType : AppName.values()) {
      int port = 0;
      if (workloadType.toString().startsWith("Cassandra")) port = 9042;
      else if (workloadType.toString().startsWith("Redis")) port = 6379;
      AppBase workload = getAppClass(workloadType).newInstance();

      footer.append("\n - " + workloadType.toString() + " :\n");
      footer.append("   ");
      for (int idx = 0; idx < workloadType.toString().length(); idx++) {
        footer.append("-");
      }
      footer.append("\n");

      List<String> description = workload.getWorkloadDescription();
      if (!description.isEmpty()) {
        for (String line : description) {
          footer.append("\t\t");
          footer.append(line);
          footer.append("\n");
        }
        footer.append("\n");
      }
      footer.append("\t\tUsage:\n");
      footer.append(optsPrefix);
      footer.append("java -jar yb-sample-apps.jar");
      footer.append(optsSuffix);
      footer.append(optsPrefix + "--workload " + workloadType.toString() + optsSuffix);
      footer.append(optsPrefix + "--nodes 127.0.0.1:" + port);

      footer.append("\n\n\t\tOther options (with default values):\n");
      for (String line : workload.getExampleUsageOptions()) {
        footer.append(optsPrefix + "[ ");
        footer.append(line);
        footer.append(" ]\n");
      }
    }
    footer.append("\n");
    System.out.println(footer.toString());
    System.exit(0);
  }

  /**
   * One contact point could be resolved to multiple nodes IPs. For example for Kubernetes
   * yb-tservers.default.svc.cluster.local contact point is resolved to all tservers IPs.
   */
  public static class ContactPoint {
    String host;
    int port;

    public ContactPoint(String host, int port) {
      this.host = host;
      this.port = port;
    }

    public String getHost() {
      return host;
    }

    public int getPort() {
      return port;
    }

    public static ContactPoint fromHostPort(String hostPort) {
      String[] parts = hostPort.split(":");
      String host = parts[0];
      int port = Integer.parseInt(parts[1]);
      return new ContactPoint(host, port);
    }

    public String ToString() { return host + ":" + port; }
  }
}
