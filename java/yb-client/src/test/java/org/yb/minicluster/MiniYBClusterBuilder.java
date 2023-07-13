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
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.TreeMap;

import com.google.common.base.Preconditions;

public class MiniYBClusterBuilder {

  private MiniYBClusterParameters clusterParameters = new MiniYBClusterParameters();

  /** Extra flags added to each master's command line. */
  private Map<String, String> masterFlags = new TreeMap<>();

  /** Extra flags added to each tablet server's command line. */
  private Map<String, String> commonTServerFlags = new TreeMap<>();

  /** Flags for each tablet server. */
  private List<Map<String, String>> perTServerFlags = new ArrayList<>();

  private String testClassName = null;

  private String certFile = null;

  // The client cert files for mTLS.
  private String clientCertFile = null;
  private String clientKeyFile = null;

  // This is used as the default bind address (Used only for mTLS verification).
  private String clientHost = null;
  private int clientPort = 0;

  private Map<String, String> tserverEnvVars = new TreeMap<String, String>();

  public MiniYBClusterBuilder numMasters(int numMasters) {
    this.clusterParameters.numMasters = numMasters;
    return this;
  }

  public MiniYBClusterBuilder numTservers(int numTservers) {
    this.clusterParameters.numTservers = numTservers;
    return this;
  }

  public MiniYBClusterBuilder numShardsPerTServer(int numShardsPerTServer) {
    this.clusterParameters.numShardsPerTServer = numShardsPerTServer;
    return this;
  }

  public MiniYBClusterBuilder useIpWithCertificate(boolean value) {
    this.clusterParameters.useIpWithCertificate = value;
    return this;
  }

  public MiniYBClusterBuilder sslCertFile(String certFile) {
    this.certFile = certFile;
    return this;
  }

  public MiniYBClusterBuilder sslClientCertFiles(String certFile, String keyFile) {
    this.clientCertFile = certFile;
    this.clientKeyFile = keyFile;
    return this;
  }

  public MiniYBClusterBuilder bindHostAddress(String clientHost, int clientPort) {
    this.clientHost = clientHost;
    this.clientPort = clientPort;
    return this;
  }

  /**
   * Configures the internal client to use the given timeout for all operations. Also uses the
   * timeout for tasks like waiting for tablet servers to check in with the master.
   * @param defaultTimeoutMs timeout in milliseconds
   * @return this instance
   */
  public MiniYBClusterBuilder defaultTimeoutMs(int defaultTimeoutMs) {
    this.clusterParameters.defaultTimeoutMs = defaultTimeoutMs;
    return this;
  }

  /**
   * Configure additional command-line flags for starting master. This replaces the list of
   * existing additional master flags.
   *
   * @param masterFlags additional command-line flags
   * @return this instance
   */
  public MiniYBClusterBuilder masterFlags(Map<String, String> masterFlags) {
    this.masterFlags = masterFlags;
    return this;
  }

  /**
   * Configure additional command-line flags for starting master. This appends to the list of
   * additional master arguments.
   */
  public MiniYBClusterBuilder addMasterFlags(Map<String, String> masterFlags) {
    this.masterFlags.putAll(masterFlags);
    return this;
  }

  public MiniYBClusterBuilder addMasterFlag(String flag, String value) {
    this.masterFlags.put(flag, value);
    return this;
  }

  /**
   * Configure additional command-line arguments for starting each tserver, taking priority over
   * common tserver flags. This replaces the list of existing additional per-tserver flags.
   */
  public MiniYBClusterBuilder perTServerFlags(List<Map<String, String>> perTServerFlags) {
    Preconditions.checkNotNull(perTServerFlags);
    this.perTServerFlags = perTServerFlags;
    return this;
  }

  /**
   * Configure additional command-line arguments for starting tserver. This replaces the list of
   * existing additional common tserver flags.
   */
  public MiniYBClusterBuilder commonTServerFlags(Map<String, String> commonTServerFlags) {
    this.commonTServerFlags = commonTServerFlags;
    return this;
  }

  public MiniYBClusterBuilder addCommonTServerFlags(Map<String, String> commonTServerFlags) {
    this.commonTServerFlags.putAll(commonTServerFlags);
    return this;
  }

  public MiniYBClusterBuilder addCommonTServerFlag(String flag, String value) {
    this.commonTServerFlags.put(flag, value);
    return this;
  }

  /**
   * Configure additional command-line arguments for starting both master and tserver.
   */
  public MiniYBClusterBuilder addCommonFlags(Map<String, String> flags) {
    addMasterFlags(flags);
    addCommonTServerFlags(flags);
    return this;
  }

  public MiniYBClusterBuilder addCommonFlag(String flag, String value) {
    addMasterFlag(flag, value);
    addCommonTServerFlag(flag, value);
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
    this.clusterParameters.replicationFactor = replicationFactor;
    return this;
  }

  /**
   * Enable YSQL server API in tablet servers.
   */
  public MiniYBClusterBuilder enableYsql(boolean enableYsql) {
    this.clusterParameters.startYsqlProxy = enableYsql;
    return this;
  }

  /**
   * Enable Ysql Connection Manager in tablet servers.
   */
  public MiniYBClusterBuilder enableYsqlConnMgr(boolean enableYsqlConnMgr) {
    this.clusterParameters.startYsqlConnMgr = enableYsqlConnMgr;
    return this;
  }

  public MiniYBClusterBuilder ysqlSnapshotVersion(YsqlSnapshotVersion ysqlSnapshotVersion) {
    Preconditions.checkState(this.clusterParameters.startYsqlProxy, "YSQL is not enabled");
    this.clusterParameters.ysqlSnapshotVersion = ysqlSnapshotVersion;
    return this;
  }

  /**
   * Enable PostgreSQL server API in tablet servers.
   */
  public MiniYBClusterBuilder enablePgTransactions(boolean enablePgTransactions) {
    if (enablePgTransactions) {
      enableYsql(true);
    }
    this.clusterParameters.pgTransactionsEnabled = enablePgTransactions;
    return this;
  }

  public MiniYBClusterBuilder tserverHeartbeatTimeoutMs(final int tserverHeartbeatTimeoutMs) {
    this.clusterParameters.tserverHeartbeatTimeoutMsOpt = Optional.of(tserverHeartbeatTimeoutMs);
    return this;
  }

  public MiniYBClusterBuilder yqlSystemPartitionsVtableRefreshSecs(
      final int yqlSystemPartitionsVtableRefreshSecs) {
    this.clusterParameters.yqlSystemPartitionsVtableRefreshSecsOpt =
        Optional.of(yqlSystemPartitionsVtableRefreshSecs);
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
    Preconditions.checkArgument(
        perTServerFlags.isEmpty() || perTServerFlags.size() == clusterParameters.numTservers,
        "Per-tablet-server arguments list has %s elements (%s) but numTServers=%s",
        perTServerFlags.size(), perTServerFlags, clusterParameters.numTservers);

    return new MiniYBCluster(
        clusterParameters,
        masterFlags,
        commonTServerFlags,
        perTServerFlags,
        tserverEnvVars,
        testClassName,
        certFile,
        clientCertFile,
        clientKeyFile,
        clientHost,
        clientPort);
  }
}
