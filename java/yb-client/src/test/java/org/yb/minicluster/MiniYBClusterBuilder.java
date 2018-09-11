/**
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 */
package org.yb.minicluster;

import java.util.List;

public class MiniYBClusterBuilder {

  private int numMasters = 1;
  private int numTservers = MiniYBCluster.DEFAULT_NUM_TSERVERS;
  private int numShardsPerTServer = MiniYBCluster.DEFAULT_NUM_SHARDS_PER_TSERVER;
  private boolean useIpWithCertificate = MiniYBCluster.DEFAULT_USE_IP_WITH_CERTIFICATE;
  private int defaultTimeoutMs = 50000;
  private List<String> masterArgs = null;
  private List<List<String>> tserverArgs = null;
  private String testClassName = null;

  public MiniYBClusterBuilder numMasters(int numMasters) {
    this.numMasters = numMasters;
    return this;
  }

  public MiniYBClusterBuilder numTservers(int numTservers) {
    this.numTservers = numTservers;
    return this;
  }

  public MiniYBClusterBuilder numShardsPerTServer(int numShardsPerTServer) {
    this.numShardsPerTServer = numShardsPerTServer;
    return this;
  }

  public MiniYBClusterBuilder useIpWithCertificate(boolean value) {
    this.useIpWithCertificate = value;
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
  public MiniYBClusterBuilder tserverArgs(List<List<String>> tserverArgs) {
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
        numShardsPerTServer, testClassName, useIpWithCertificate);
  }
}
