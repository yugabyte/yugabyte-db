/**
 * Copyright (c) YugaByte, Inc.
 */
package org.yb.minicluster;

import java.util.List;

public class MiniYBClusterBuilder {

  private int numMasters = 1;
  private int numTservers = 3;
  private int defaultTimeoutMs = 50000;
  private List<String> masterArgs = null;
  private List<String> tserverArgs = null;
  private String testClassName = null;

  public MiniYBClusterBuilder numMasters(int numMasters) {
    this.numMasters = numMasters;
    return this;
  }

  public MiniYBClusterBuilder numTservers(int numTservers) {
    this.numTservers = numTservers;
    return this;
  }

  /**
   * Configures the internal client to use the given timeout for all operations. Also uses the
   * timeout for tasks like waiting for tablet servers to check in with the master.
   * @param defaultTimeoutMs timeout in milliseconds
   * @return this instance
   */
  public MiniYBClusterBuilder defaultTimeoutMs(int defaultTimeoutMs) {
    this.defaultTimeoutMs = defaultTimeoutMs;
    return this;
  }

  /**
   * Configure additional command-line arguments for starting master.
   * @param masterArgs additional command-line arguments
   * @return this instance
   */
  public MiniYBClusterBuilder masterArgs(List<String> masterArgs) {
    this.masterArgs = masterArgs;
    return this;
  }

  /**
   * Configure additional command-line arguments for starting tserver.
   */
  public MiniYBClusterBuilder tserverArgs(List<String> tserverArgs) {
    this.tserverArgs = tserverArgs;
    return this;
  }

  /**
   * Sets test class name that this mini cluster is created for. We're including this in daemon
   * command line to be able to identify stuck master/tablet server processes later.
   */
  public MiniYBClusterBuilder testClassName(String testClassName) {
    this.testClassName = testClassName;
    return this;
  }

  public MiniYBCluster build() throws Exception {
    return new MiniYBCluster(numMasters, numTservers, defaultTimeoutMs, masterArgs, tserverArgs,
        testClassName);
  }
}
