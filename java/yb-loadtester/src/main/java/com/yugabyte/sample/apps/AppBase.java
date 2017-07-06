// Copyright (c) YugaByte, Inc.

package com.yugabyte.sample.apps;

import java.net.InetSocketAddress;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

import org.apache.log4j.Logger;

import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.Session;
import com.yugabyte.sample.common.CmdLineOpts;
import com.yugabyte.sample.common.CmdLineOpts.Node;
import com.yugabyte.sample.common.SimpleLoadGenerator;
import com.yugabyte.sample.common.metrics.MetricsTracker;
import com.yugabyte.sample.common.metrics.MetricsTracker.MetricName;

import redis.clients.jedis.Jedis;
import redis.clients.jedis.Pipeline;

/**
 * Abstract base class for all apps. This class does the following:
 *   - Provides various helper methods including methods for creating Redis and Cassandra clients.
 *   - Has a metrics tracker object, and internally tracks reads and writes.
 *   - Has the abstract methods that are implemented by the various apps.
 */
public abstract class AppBase implements MetricsTracker.StatusMessageAppender {
  private static final Logger LOG = Logger.getLogger(AppBase.class);
  // Variable to track start time of the workload.
  private long workloadStartTime = -1;
  // Instance of the workload configuration.
  public static AppConfig appConfig = new AppConfig();
  // The configuration of the load tester.
  protected CmdLineOpts configuration;
  // The number of keys written so far.
  protected static AtomicLong numKeysWritten = new AtomicLong(0);
  // The number of keys that have been read so far.
  protected static AtomicLong numKeysRead = new AtomicLong(0);
  // Object to track read and write metrics.
  private static MetricsTracker metricsTracker;
  // State variable to track if this workload has finished.
  protected AtomicBoolean hasFinished = new AtomicBoolean(false);
  // The Cassandra client variables.
  protected Cluster cassandra_cluster = null;
  protected Session cassandra_session = null;
  // The Java redis client.
  private Jedis jedisClient;
  private Pipeline jedisPipeline;
  // Instance of the load generator.
  private static volatile SimpleLoadGenerator loadGenerator = null;
  // Keyspace name.
  private static String keyspace = "ybdemo_keyspace";

  //////////// Helper methods to return the client objects (Redis, Cassandra, etc). ////////////////

  /**
   * We create one Cassandra client per app instance. This is a non-synchronized
   * method, so multiple threads can call it without any performance penalty. If there is no client,
   * a synchronized thread is created so that exactly only one thread will create a client. If there
   * is a pre-existing client, we just return it.
   * @return a Cassandra Session object.
   */
  protected Session getCassandraClient() {
    if (cassandra_cluster == null) {
      createCassandraClient();
    }
    return cassandra_session;
  }

  protected void createKeyspace(Session session, String ks) {
    String create_keyspace_stmt = "CREATE KEYSPACE IF NOT EXISTS " + ks +
      " WITH REPLICATION = {'class': 'SimpleStrategy', 'replication_factor' : 1};";
    cassandra_session.execute(create_keyspace_stmt);
    LOG.debug("Created a keyspace " + ks + " using query: [" + create_keyspace_stmt + "]");
    String use_keyspace_stmt = "USE " + ks + ";";
    cassandra_session.execute(use_keyspace_stmt);
    LOG.debug("Used the new keyspace " + ks + " using query: [" + use_keyspace_stmt + "]");
  }

  /**
   * Private method that is thread-safe and creates the Cassandra client. Exactly one calling thread
   * will succeed in creating the client. This method does nothing for the other threads.
   */
  private synchronized void createCassandraClient() {
    if (cassandra_cluster == null) {
      cassandra_cluster = Cluster.builder()
                       .addContactPointsWithPorts(getNodesAsInet())
                       .build();
      LOG.debug("Connected to cluster: " + cassandra_cluster.getClusterName());
    }
    if (cassandra_session == null) {
      LOG.debug("Creating a session...");
      cassandra_session = cassandra_cluster.connect();
      createKeyspace(cassandra_session, keyspace);
    }
  }

  /**
   * Helper method to create a Jedis client.
   * @return a Jedis (Redis client) object.
   */
  protected synchronized Jedis getRedisClient() {
    if (jedisClient == null) {
      Node node = getRandomNode();
      // Set the timeout to something more than the timeout in the proxy layer.
      jedisClient = new Jedis(node.getHost(), node.getPort(), /* timeout ms */ 61000);
    }
    return jedisClient;
  }

  protected synchronized Pipeline getRedisPipeline() {
    if (jedisPipeline == null) {
      jedisPipeline = getRedisClient().pipelined();
    }
    return jedisPipeline;
  }

  ///////////////////// The following methods are overridden by the apps ///////////////////////////

  /**
   * This method is called to allow the app to initialize itself with the various command line
   * options.
   */
  public void initialize(CmdLineOpts configuration) {}

  /**
   * The apps extending this base should drop all the tables they create when this method is called.
   */
  public void dropTable() {}

  public void dropCassandraTable(String tableName) {
    try {
      String drop_stmt = String.format("DROP TABLE %s;", tableName);
      getCassandraClient().execute(drop_stmt);
      LOG.info("Dropped Cassandra table " + tableName + " using query: [" + drop_stmt + "]");
    } catch (Exception e) {
      LOG.info("Ignoring exception dropping table: " + e.getMessage());
    }
  }

  /**
   * The apps extending this base should create all the necessary tables in this method.
   */
  public void createTablesIfNeeded() {
    for (String create_stmt : getCreateTableStatements()) {
      getCassandraClient().execute(create_stmt);
      LOG.info("Created a Cassandra table using query: [" + create_stmt + "]");
    }
  }

  protected List<String> getCreateTableStatements() {
    return Arrays.asList();
  }

  /**
   * This call models an OLTP read for the app to perform read operations.
   * @return Number of reads done, a value <= 0 indicates no ops were done.
   */
  public long doRead() { return 0; }

  /**
   * This call models an OLTP write for the app to perform write operations.
   * @return Number of writes done, a value <= 0 indicates no ops were done.
   */
  public long doWrite() { return 0; }

  /**
   * This call should implement the main logic in non-OLTP apps. Not called for OLTP apps.
   */
  public void run() {}

  /**
   * Default implementation of the status message appender. Override/add to this method any stats
   * that need to be printed to the end user.
   */
  @Override
  public void appendMessage(StringBuilder sb) {
    sb.append("Uptime: " + (System.currentTimeMillis() - workloadStartTime) + " ms | ");
  }

  /**
   *Method to print the description for the app.
   * @param linePrefix : a prefix to be added to each line that is being printed.
   * @param lineSuffix : a suffix to be added at the end of each line.
   * @return the formatted description string.
   */
  public String getWorkloadDescription(String linePrefix, String lineSuffix) {
    return "";
  }

  /**
   * Method to pretty print the example usage for the app.
   * @param linePrefix
   * @param lineSuffix
   * @return
   */
  public String getExampleUsageOptions(String linePrefix, String lineSuffix) {
    return "";
  }


  ////////////// The following methods framework/helper methods for subclasses. ////////////////////

  /**
   * The load tester framework call this method of the base class. This in turn calls the
   * 'initialize()' method which the plugins should implement.
   * @param configuration configuration of the load tester framework.
   */
  public void workloadInit(CmdLineOpts configuration) {
    workloadStartTime = System.currentTimeMillis();
    this.configuration = configuration;
    initialize(configuration);
    initMetricsTracker();
  }

   private synchronized void initMetricsTracker() {
    if (metricsTracker == null) {
      metricsTracker = new MetricsTracker();
      if (appConfig.appType == AppConfig.Type.OLTP) {
        metricsTracker.createMetric(MetricName.Read);
        metricsTracker.createMetric(MetricName.Write);
        metricsTracker.registerStatusMessageAppender(this);
        metricsTracker.start();
      }
    }
  }

  /**
   * Helper method to get a random proxy-service node to do io against.
   * @return
   */
  public Node getRandomNode() {
    return configuration.getRandomNode();
  }

  /**
   * Returns a list of Inet address objects in the proxy tier. This is needed by Cassandra clients.
   */
  public List<InetSocketAddress> getNodesAsInet() {
    List<InetSocketAddress> inetAddrs = new ArrayList<InetSocketAddress>();
    for (Node node : configuration.getNodes()) {
      // Convert Node to InetSocketAddress.
      inetAddrs.add(new InetSocketAddress(node.getHost(), node.getPort()));
    }
    return inetAddrs;
  }


  /**
   * Returns true if the workload has finished running, false otherwise.
   */
  public boolean hasFinished() {
    return hasFinished.get();
  }

  /**
   * Stops the workload running for this app. The purpose of this method is to allow a clean stop
   * of the workload from external inputs.
   */
  public void stopApp() {
    hasFinished.set(true);
  }

  /**
   * Called by the framework to perform write operations - internally measures the time taken to
   * perform the write op and keeps track of the number of keys written, so that we are able to
   * report the metrics to the user.
   */
  public void performWrite() {
    // If we have written enough keys we are done.
    if (appConfig.numKeysToWrite > 0 && numKeysWritten.get() >= appConfig.numKeysToWrite) {
      hasFinished.set(true);
      return;
    }
    // Perform the write and track the number of successfully written keys.
    long startTs = System.nanoTime();
    long count = doWrite();
    long endTs = System.nanoTime();
    if (count > 0) {
      numKeysWritten.addAndGet(count);
      metricsTracker.getMetric(MetricName.Write).accumulate(count, endTs - startTs);
    }
  }

  /**
   * Called by the framework to perform read operations - internally measures the time taken to
   * perform the read op and keeps track of the number of keys read, so that we are able to
   * report the metrics to the user.
   */
  public void performRead() {
    // If we have read enough keys we are done.
    if (appConfig.numKeysToRead > 0 && numKeysRead.get() >= appConfig.numKeysToRead) {
      hasFinished.set(true);
      return;
    }
    // Perform the read and track the number of successfully read keys.
    long startTs = System.nanoTime();
    long count = doRead();
    long endTs = System.nanoTime();
    if (count > 0) {
      numKeysRead.addAndGet(count);
      metricsTracker.getMetric(MetricName.Read).accumulate(count, endTs - startTs);
    }
  }

  @Override
  public String appenderName() {
    return this.getClass().getSimpleName();
  }

  /**
   * Terminate the workload (tear down connections if needed, etc).
   */
  public void terminate() {
    destroyClients();
  }

  protected synchronized void destroyClients() {
    if (cassandra_session != null) {
      cassandra_session.close();
      cassandra_session = null;
    }
    if (cassandra_cluster != null) {
      cassandra_cluster.close();
      cassandra_cluster = null;
    }
    if (jedisClient != null) {
      jedisClient.close();
    }
  }

  public SimpleLoadGenerator getSimpleLoadGenerator() {
    if (loadGenerator == null) {
      synchronized (AppBase.class) {
        if (loadGenerator == null) {
          loadGenerator = new SimpleLoadGenerator(0, appConfig.numUniqueKeysToWrite,
              appConfig.maxWrittenKey);
        }
      }
    }
    return loadGenerator;
  }

  public static long numOps() {
    return numKeysRead.get() + numKeysWritten.get();
  }
}
