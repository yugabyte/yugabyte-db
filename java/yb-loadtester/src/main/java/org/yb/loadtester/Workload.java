package org.yb.loadtester;

import java.net.InetSocketAddress;
import java.util.ArrayList;
import java.util.List;

import org.apache.log4j.Logger;
import org.yb.loadtester.common.Configuration;
import org.yb.loadtester.common.Configuration.Node;

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
  // State variable to track if this workload has finished.
  protected boolean hasFinished = false;

  /**
   * The load tester framework call this method of the base class. This in turn calls the
   * 'initialize()' method which the plugins should implement.
   * @param configuration configuration of the load tester framework.
   */
  public void workloadInit(Configuration configuration) {
    this.configuration = configuration;
    initialize(null);
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
    if (doWrite()) {
      numKeysWritten++;
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
    if (doRead()) {
      numKeysRead++;
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
   * @return true if the read succeeded, false on failure.
   */
  public abstract boolean doRead();

  /**
   * As a part of this call, the plugin should perform a single write operation.
   * @return true if the write succeeded, false on failure.
   */
  public abstract boolean doWrite();

  /**
   * Terminate the workload (tear down connections if needed, etc).
   */
  public abstract void terminate();
}
