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

package com.yugabyte.sample.apps;

import java.net.InetSocketAddress;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Random;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.zip.Adler32;
import java.util.zip.Checksum;

import com.datastax.driver.core.ConsistencyLevel;
import org.apache.log4j.Logger;

import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.Session;
import com.datastax.driver.core.SimpleStatement;
import com.datastax.driver.core.policies.DCAwareRoundRobinPolicy;
import com.yugabyte.driver.core.policies.PartitionAwarePolicy;
import com.yugabyte.sample.common.CmdLineOpts;
import com.yugabyte.sample.common.CmdLineOpts.Node;
import com.yugabyte.sample.common.SimpleLoadGenerator;
import com.yugabyte.sample.common.SimpleLoadGenerator.Key;
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

  // Number of uniques keys to insert by default.
  public static final int NUM_UNIQUE_KEYS = 1000000;

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
  private static volatile MetricsTracker metricsTracker;
  // State variable to track if this workload has finished.
  protected AtomicBoolean hasFinished = new AtomicBoolean(false);
  // The Cassandra client variables.
  protected static volatile Cluster cassandra_cluster = null;
  protected static volatile Session cassandra_session = null;
  // The Java redis client.
  private volatile Jedis jedisClient = null;
  private volatile Pipeline jedisPipeline = null;
  private Node redisServerInUse = null;
  // Instance of the load generator.
  private static volatile SimpleLoadGenerator loadGenerator = null;
  // Keyspace name.
  private static String keyspace = "ybdemo_keyspace";

  //////////// Helper methods to return the client objects (Redis, Cassandra, etc). ////////////////

  /**
   * We create one shared Cassandra client. This is a non-synchronized method, so multiple threads
   * can call it without any performance penalty. If there is no client, a synchronized thread is
   * created so that exactly only one thread will create a client. If there is a pre-existing
   * client, we just return it.
   * @return a Cassandra Session object.
   */
  protected Session getCassandraClient() {
    if (cassandra_cluster == null) {
      createCassandraClient(getNodesAsInet());
    }
    return cassandra_session;
  }

  protected static void createKeyspace(Session session, String ks) {
    String create_keyspace_stmt = "CREATE KEYSPACE IF NOT EXISTS " + ks +
      " WITH REPLICATION = {'class': 'SimpleStrategy', 'replication_factor' : 1};";
    // Use consistency level ONE to allow cross DC requests.
    session.execute(
      session.prepare(create_keyspace_stmt)
        .setConsistencyLevel(ConsistencyLevel.ONE)
        .bind());
    LOG.debug("Created a keyspace " + ks + " using query: [" + create_keyspace_stmt + "]");
    String use_keyspace_stmt = "USE " + ks + ";";
    session.execute(
      session.prepare(use_keyspace_stmt)
        .setConsistencyLevel(ConsistencyLevel.ONE)
        .bind());
    LOG.debug("Used the new keyspace " + ks + " using query: [" + use_keyspace_stmt + "]");
  }

  protected String getKeyspace() {
    return keyspace;
  }

  /**
   * Private method that is thread-safe and creates the Cassandra client. Exactly one calling thread
   * will succeed in creating the client. This method does nothing for the other threads.
   */
  private static synchronized void createCassandraClient(List<InetSocketAddress> nodes) {
    if (cassandra_cluster == null) {
      Cluster.Builder builder = Cluster.builder().addContactPointsWithPorts(nodes);
      if (appConfig.localDc != null && !appConfig.localDc.isEmpty()) {
        builder.withLoadBalancingPolicy((new PartitionAwarePolicy(
            DCAwareRoundRobinPolicy.builder()
              .withLocalDc(appConfig.localDc)
              .withUsedHostsPerRemoteDc(Integer.MAX_VALUE)
              .allowRemoteDCsForLocalConsistencyLevel()
              .build(),
            appConfig.partitionMetadataRefreshSeconds)));
      } else if (!appConfig.disableYBLoadBalancingPolicy) {
        builder.withLoadBalancingPolicy(
          new PartitionAwarePolicy(appConfig.partitionMetadataRefreshSeconds));
      }
      cassandra_cluster = builder.build();
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
      redisServerInUse = node;
    }
    return jedisClient;
  }

  protected synchronized Pipeline getRedisPipeline() {
    if (jedisPipeline == null) {
      jedisPipeline = getRedisClient().pipelined();
    }
    return jedisPipeline;
  }

  public synchronized void resetClients() {
    jedisClient = null;
    jedisPipeline = null;
    redisServerInUse = null;
    cassandra_cluster = null;
    cassandra_session = null;
  }

  Random random = new Random();
  byte[] buffer;
  Checksum checksum = new Adler32();
  // For binary values we store checksum in bytes.
  static final int CHECKSUM_SIZE = 4;
  // For ASCII values we store checksum in hex string.
  static final int CHECKSUM_ASCII_SIZE = CHECKSUM_SIZE * 2;
  // If value size is more than VALUE_SIZE_TO_USE_PREFIX we add prefix in "val: $key" format
  // in order to check if value matches the key during read.
  static final int VALUE_SIZE_TO_USE_PREFIX = 16;
  static final byte ASCII_MARKER = (byte) 'A';
  static final byte BINARY_MARKER = (byte) 'B';

  /////////////////  Helper functions to create or verify a value of given size. ////////////////
  private static boolean isUseChecksum(int valueSize, int checksumSize) {
    return valueSize > checksumSize + 1;
  }

  private static boolean isUsePrefix(int valueSize) {
    return valueSize > VALUE_SIZE_TO_USE_PREFIX;
  }

  protected byte[] getRandomValue(Key key) {
    buffer[0] = appConfig.restrictValuesToAscii ? ASCII_MARKER : BINARY_MARKER;
    final int checksumSize = appConfig.restrictValuesToAscii ? CHECKSUM_ASCII_SIZE : CHECKSUM_SIZE;
    final boolean isUseChecksum = isUseChecksum(appConfig.valueSize, checksumSize);
    final int contentSize = appConfig.valueSize - (isUseChecksum ? checksumSize : 0);
    int i = 1;
    if (isUsePrefix(appConfig.valueSize)) {
      final byte[] keyValueBytes = key.getValueStr().getBytes();

      // Beginning of value is not random, but has format "<MARKER><PREFIX>", where prefix is
      // "val: $key" (or part of it in case small value size). This is needed to verify expected
      // value during read.
      final int prefixSize = Math.min(contentSize - 1 /* marker */, keyValueBytes.length);
      System.arraycopy(keyValueBytes, 0, buffer, 1, prefixSize);
      i += prefixSize;
    }

    // Generate randomly the rest of payload leaving space for checksum.
    if (appConfig.restrictValuesToAscii) {
      final int ASCII_START = 32;
      final int ASCII_RANGE_SIZE = 95;
      while (i < contentSize) {
        long r = (long) (random.nextInt() & 0xffffffff);
        // Hack to minimize number of calls to random.nextInt() in order to reduce CPU load.
        // This makes distribution non-uniform, but should be OK for load tests.
        for (int n = Math.min(Integer.BYTES, contentSize - i); n > 0;
          r /= ASCII_RANGE_SIZE, n--) {
          buffer[i++] = (byte) (ASCII_START + r % ASCII_RANGE_SIZE);
        }
      }
    } else {
      while (i < contentSize) {
        for (int r = random.nextInt(), n = Math.min(Integer.BYTES, contentSize - i); n > 0;
             r >>= Byte.SIZE, n--)
          buffer[i++] = (byte) r;
      }
    }

    if (isUseChecksum) {
      checksum.reset();
      checksum.update(buffer, 0, contentSize);
      long cs = checksum.getValue();
      if (appConfig.restrictValuesToAscii) {
        String csHexStr = Long.toHexString(cs);
        // Prepend zeros
        while (i < appConfig.valueSize - csHexStr.length()) {
          buffer[i++] = (byte) '0';
        }
        System.arraycopy(csHexStr.getBytes(), 0, buffer, i, csHexStr.length());
      } else {
        while (i < appConfig.valueSize) {
          buffer[i++] = (byte) cs;
          cs >>= Byte.SIZE;
        }
      }
    }

    return buffer;
  }

  protected void verifyRandomValue(Key key, byte[] value) {
    final boolean isAscii = value[0] == ASCII_MARKER;
    final int checksumSize = isAscii ? CHECKSUM_ASCII_SIZE : CHECKSUM_SIZE;
    final boolean hasChecksum = isUseChecksum(value.length, checksumSize);
    if (isUsePrefix(value.length)) {
      final String keyValueStr = key.getValueStr();
      final int prefixSize = Math.min(keyValueStr.length(), value.length -
                             (hasChecksum ? checksumSize : 0) - 1 /* marker */);
      final String prefix = new String(value, 1, prefixSize);
      // Check prefix.
      if (!prefix.equals(keyValueStr.substring(0, prefixSize))) {
        LOG.fatal("Value mismatch for key: " + key.toString() +
                  ", expected to start with: " + keyValueStr +
                  ", got: " + prefix);
      }
    }
    if (hasChecksum) {
      // Verify checksum.
      checksum.reset();
      checksum.update(value, 0, value.length - checksumSize);
      long expectedCs;
      if (isAscii) {
        String csHexStr = new String(value, value.length - checksumSize, checksumSize);
        expectedCs = Long.parseUnsignedLong(csHexStr, 16);
      } else {
        expectedCs = 0;
        for (int i = value.length - 1; i >= value.length - checksumSize; --i) {
          expectedCs <<= Byte.SIZE;
          expectedCs |= (value[i] & 0xFF);
        }
      }
      if (checksum.getValue() != expectedCs) {
        LOG.fatal("Value mismatch for key: " + key.toString() +
                  ", expected checksum: " + expectedCs +
                  ", got: " + checksum.getValue());
      }
    }
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
    String drop_stmt = String.format("DROP TABLE IF EXISTS %s;", tableName);
    getCassandraClient().execute(new SimpleStatement(drop_stmt).setReadTimeoutMillis(60000));
    LOG.info("Dropped Cassandra table " + tableName + " using query: [" + drop_stmt + "]");
  }

  /**
   * The apps extending this base should create all the necessary tables in this method.
   */
  public void createTablesIfNeeded() {
    for (String create_stmt : getCreateTableStatements()) {
      Session session = getCassandraClient();
      // consistency level of one to allow cross DC requests.
      session.execute(
        session.prepare(create_stmt)
          .setConsistencyLevel(ConsistencyLevel.ONE)
          .bind());
      LOG.info("Created a Cassandra table using query: [" + create_stmt + "]");
    }
  }

  protected List<String> getCreateTableStatements() {
    return Arrays.asList();
  }

  /**
   * Returns the redis server that we are currently talking to.
   * Used for debug purposes. Returns "" for non-redis workloads.
   */
  public String getRedisServerInUse() {
    return (redisServerInUse != null ? redisServerInUse.ToString() : "");
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
   * @param enableMetrics Should metrics tracker be enabled.
   */
  public void workloadInit(CmdLineOpts configuration, boolean enableMetrics) {
    workloadStartTime = System.currentTimeMillis();
    this.configuration = configuration;
    initialize(configuration);
    if (enableMetrics) initMetricsTracker();
  }

  public void enableMetrics() {
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
      if (metricsTracker != null) {
        metricsTracker.getMetric(MetricName.Write).accumulate(count, endTs - startTs);
      }
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
      if (metricsTracker != null) {
        metricsTracker.getMetric(MetricName.Read).accumulate(count, endTs - startTs);
      }
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
