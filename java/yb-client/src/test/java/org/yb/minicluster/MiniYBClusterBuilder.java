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

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public class MiniYBClusterBuilder {

  private int numMasters = 1;
  private int numTservers = MiniYBCluster.DEFAULT_NUM_TSERVERS;
  private int numShardsPerTServer = MiniYBCluster.DEFAULT_NUM_SHARDS_PER_TSERVER;
  private boolean useIpWithCertificate = MiniYBCluster.DEFAULT_USE_IP_WITH_CERTIFICATE;
  private int defaultTimeoutMs = 50000;
  private List<String> masterArgs = new ArrayList<>();

  /** Arguments for each tablet server. */
  private List<List<String>> perTServerArgs = new ArrayList<>();

  /** Extra arguments added to each tablet server's command line. */
  private List<String> commonTServerArgs = new ArrayList<>();

  private String testClassName = null;
  private int replicationFactor = -1;
  private boolean startPgSqlProxy = false;
  private boolean pgTransactionsEnabled = false;

  private String certFile = null;

  private Map<String, String> tserverEnvVars = new TreeMap<String, String>();

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

  public MiniYBClusterBuilder sslCertFile(String certFile) {
    this.certFile = certFile;
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
   * Configure additional command-line arguments for starting master. This replaces the list of
   * existing additional master arguments.
   *
   * @param masterArgs additional command-line arguments
   * @return this instance
   */
  public MiniYBClusterBuilder masterArgs(List<String> masterArgs) {
    this.masterArgs = masterArgs;
    return this;
  }

  /**
   * Configure additional command-line arguments for starting master. This appends to the list of
   * additional master arguments.
   */
  public MiniYBClusterBuilder addMasterArgs(String... newArgs) {
    this.masterArgs.addAll(Arrays.asList(newArgs));
    return this;
  }


  /**
   * Configure additional command-line arguments for starting tserver.
   */
  public MiniYBClusterBuilder perTServerArgs(List<List<String>> tserverArgs) {
    this.perTServerArgs = tserverArgs;
    return this;
  }

  public MiniYBClusterBuilder commonTServerArgs(List<String> commonTServerArgs) {
    this.commonTServerArgs = commonTServerArgs;
    return this;
  }

  public MiniYBClusterBuilder addCommonTServerArgs(String... newArgs) {
    if (this.commonTServerArgs == null) {
      this.commonTServerArgs = new ArrayList<>();
    }
    this.commonTServerArgs.addAll(Arrays.asList(newArgs));
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

  /**
   * Sets the replication factor for the mini-cluster.
   */
  public MiniYBClusterBuilder replicationFactor(int replicationFactor) {
    this.replicationFactor = replicationFactor;
    return this;
  }

  /**
   * Enable PostgreSQL server API in tablet servers.
   */
  public MiniYBClusterBuilder enablePostgres(boolean enablePostgres) {
    this.startPgSqlProxy = enablePostgres;
    return this;
  }

  /**
   * Enable PostgreSQL server API in tablet servers.
   */
  public MiniYBClusterBuilder enablePgTransactions(boolean enablePgTransactions) {
    if (enablePgTransactions) {
      enablePostgres(true);
    }
    this.pgTransactionsEnabled = enablePgTransactions;
    return this;
  }

  /**
   * Add environment variables.
   */
  public MiniYBClusterBuilder addEnvironmentVariables(Map<String, String> tserverEnvVars) {
    this.tserverEnvVars.putAll(tserverEnvVars);
    return this;
  }

  public MiniYBCluster build() throws Exception {
    if (perTServerArgs != null && perTServerArgs.size() != numTservers) {
      throw new AssertionError(
          "Per-tablet-server arguments list has " + perTServerArgs.size() + " elements (" +
              perTServerArgs + ") but numTServers=" + numTservers);
    }

    return new MiniYBCluster(
        numMasters,
        numTservers,
        defaultTimeoutMs,
        masterArgs,
        perTServerArgs,
        commonTServerArgs,
        tserverEnvVars,
        numShardsPerTServer,
        testClassName,
        useIpWithCertificate,
        replicationFactor,
        startPgSqlProxy,
        certFile,
        pgTransactionsEnabled);
  }
}
