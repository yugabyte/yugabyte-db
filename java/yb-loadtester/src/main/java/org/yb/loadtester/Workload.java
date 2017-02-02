package org.yb.loadtester;

import java.net.InetSocketAddress;
import java.util.ArrayList;
import java.util.List;

import org.apache.log4j.Logger;
import org.yb.loadtester.common.Configuration;
import org.yb.loadtester.common.Configuration.Node;
import org.yb.loadtester.common.SimpleLoadGenerator;
import org.yb.loadtester.metrics.MetricsTracker;
import org.yb.loadtester.metrics.MetricsTracker.MetricName;

import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.Session;

import redis.clients.jedis.Jedis;

public abstract class Workload {
  private static final Logger LOG = Logger.getLogger(Workload.class);
  // Instance of the workload configuration.
  public static WorkloadConfig workloadConfig = new WorkloadConfig();
  // The configuration of the load tester.
  private Configuration configuration;
  // The number of keys written so far.
  protected int numKeysWritten = 0;
  // The number of keys that have been read so far.
  protected int numKeysRead = 0;
  // Object to track read and write metrics.
  private static MetricsTracker metricsTracker = new MetricsTracker();
  // State variable to track if this workload has finished.
  protected boolean hasFinished = false;
  // The Cassandra client variables.
  protected Cluster cassandra_cluster = null;
  protected Session cassandra_session = null;
  // The Java redis client.
  private Jedis jedisClient;
  // Instance of the load generator.
  private static SimpleLoadGenerator loadGenerator = null;


  /**
   * The load tester framework call this method of the base class. This in turn calls the
   * 'initialize()' method which the plugins should implement.
   * @param configuration configuration of the load tester framework.
   */
  public void workloadInit(Configuration configuration) {
    this.configuration = configuration;
    initialize(null);
    metricsTracker.createMetric(MetricName.Read);
    metricsTracker.createMetric(MetricName.Write);
    metricsTracker.start();
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
    return hasFinished;
  }

  /**
   * Called by the load test to perform a write operation.
   */
  public void workloadWrite() {
    // If we have written enough keys we are done.
    if (workloadConfig.numKeysToWrite > 0 && numKeysWritten >= workloadConfig.numKeysToWrite - 1) {
      hasFinished = true;
      return;
    }
    // Perform the write and track the number of successfully written keys.
    long startTs = System.currentTimeMillis();
    long count = doWrite();
    long endTs = System.currentTimeMillis();
    if (count > 0) {
      numKeysWritten += count;
      metricsTracker.getMetric(MetricName.Write).accumulate(count, endTs - startTs);
    }
  }

  /**
   * Called by the load test to perform a read operation.
   */
  public void workloadRead() {
    // If we have read enough keys we are done.
    if (workloadConfig.numKeysToRead > 0 && numKeysRead >= workloadConfig.numKeysToRead - 1) {
      hasFinished = true;
      return;
    }
    // Perform the read and track the number of successfully read keys.
    long startTs = System.currentTimeMillis();
    long count = doRead();
    long endTs = System.currentTimeMillis();
    if (count > 0) {
      numKeysRead += count;
      metricsTracker.getMetric(MetricName.Read).accumulate(count, endTs - startTs);
    }
  }

  /**
   * Initialize the plugin with various params.
   */
  public abstract void initialize(String args);

  /**
   * Call to tell the plugin to create tables as needed.
   */
  public abstract void createTableIfNeeded();

  /**
   * As a part of this call, the plugin should perform a single read operation.
   * @return Number of reads done, a value <= 0 indicates no ops were done.
   */
  public abstract long doRead();

  /**
   * As a part of this call, the plugin should perform a single write operation.
   * @return Number of writes done, a value <= 0 indicates no ops were done.
   */
  public abstract long doWrite();

  /**
   * Terminate the workload (tear down connections if needed, etc).
   */
  public void terminate() {
    destroyClients();
  }

  protected synchronized Session getCassandraClient() {
    if (cassandra_cluster == null) {
      createCassandraClient();
    }
    return cassandra_session;
  }

  private synchronized void createCassandraClient() {
    if (cassandra_cluster == null) {
      cassandra_cluster = Cluster.builder()
                       .addContactPointsWithPorts(getNodesAsInet())
                       .build();
      LOG.info("Connected to cluster: " + cassandra_cluster.getClusterName());
    }
    if (cassandra_session == null) {
      LOG.info("Creating a session...");
      cassandra_session = cassandra_cluster.connect();
    }
  }

  protected synchronized Jedis getRedisClient() {
    if (jedisClient == null) {
      Node node = getRandomNode();
      jedisClient = new Jedis(node.getHost(), node.getPort());
    }
    return jedisClient;
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
      synchronized (this) {
        if (loadGenerator == null) {
          loadGenerator = new SimpleLoadGenerator(0, workloadConfig.numUniqueKeysToWrite);
        }
      }
    }
    return loadGenerator;
  }
}
