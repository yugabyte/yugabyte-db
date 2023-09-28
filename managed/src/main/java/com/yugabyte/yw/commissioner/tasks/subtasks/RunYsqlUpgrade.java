/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.api.client.util.Throwables;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.services.config.YbClientConfig;
import com.yugabyte.yw.common.services.config.YbClientConfigFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.client.IsInitDbDoneResponse;
import org.yb.client.UpgradeYsqlResponse;
import org.yb.client.YBClient;

@Slf4j
public class RunYsqlUpgrade extends UniverseTaskBase {

  private static final String MIN_YSQL_UPGRADE_RELEASE = "2.8.0.0";
  private static final String NO_YSQL_UPGRADE_RELEASE = "2.9.0.0";
  private static final String NEXT_YSQL_UPGRADE_RELEASE = "2.11.0.0";

  private static final int MAX_ATTEMPTS = 3;
  private static final int DELAY_BETWEEN_ATTEMPTS_SEC = 60;

  @VisibleForTesting
  public static final String USE_SINGLE_CONNECTION_PARAM =
      "yb.upgrade.single_connection_ysql_upgrade";

  private static final String USE_SINGLE_CONNECTION_ARG = "use_single_connection";

  private final NodeUniverseManager nodeUniverseManager;

  private final YbClientConfigFactory ybClientConfigFactory;

  @Inject
  protected RunYsqlUpgrade(
      BaseTaskDependencies baseTaskDependencies,
      NodeUniverseManager nodeUniverseManager,
      YbClientConfigFactory ybClientConfigFactory) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
    this.ybClientConfigFactory = ybClientConfigFactory;
  }

  // Parameters for YSQL upgrade task.
  public static class Params extends UniverseTaskParams {
    public String ybSoftwareVersion;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    if (!CommonUtils.isReleaseEqualOrAfter(
        MIN_YSQL_UPGRADE_RELEASE, taskParams().ybSoftwareVersion)) {
      log.info("Skipping YSQL upgrade as current YB version is {}", taskParams().ybSoftwareVersion);
      return;
    }
    if (CommonUtils.isReleaseBetween(
        NO_YSQL_UPGRADE_RELEASE, NEXT_YSQL_UPGRADE_RELEASE, taskParams().ybSoftwareVersion)) {
      log.info("Skipping YSQL upgrade as current YB version is {}", taskParams().ybSoftwareVersion);
      return;
    }
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();

    if (!primaryCluster.userIntent.enableYSQL) {
      log.info("Skipping YSQL upgrade as the universe isn't configured for YSQL.");
      return;
    }

    String masterHostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();

    // Ysql upgrade will take longer for multi-region universes when the active tserver chosen
    //   is not in the same region as the master leader.
    long ysqlUpgradeAdminOperationTimeoutMs =
        confGetter.getConfForScope(universe, UniverseConfKeys.ysqlUpgradeTimeoutSec) * 1000;
    YbClientConfig clientConfig =
        ybClientConfigFactory.create(
            masterHostPorts,
            certificate,
            Math.max(
                ysqlUpgradeAdminOperationTimeoutMs,
                confGetter.getGlobalConf(GlobalConfKeys.ybcAdminOperationTimeoutMs)));

    try (YBClient client = ybService.getClientWithConfig(clientConfig)) {
      validateAndUpgrade(client, universe);
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      Throwables.propagate(e);
    }
  }

  private void validateAndUpgrade(YBClient client, Universe universe) {
    boolean useSingleConnection =
        confGetter.getConfForScope(universe, UniverseConfKeys.singleConnectionYsqlUpgrade);
    int numAttempts = 0;
    String errorMessage = "";

    while (true) {
      // We have failed to upgrade YSQL and will retry.
      if (!StringUtils.isBlank(errorMessage)) {
        log.debug(
            "Failed to perform YSQL upgrade. Attempt {}, error '{}'", numAttempts, errorMessage);
        if (numAttempts == MAX_ATTEMPTS) {
          throw new RuntimeException(
              String.format(
                  "YSQL upgrade, max number attempts reached: %s. Failing...", numAttempts));
        }
        log.debug("Will retry YSQL upgrade in {} seconds.", DELAY_BETWEEN_ATTEMPTS_SEC);
        waitFor(Duration.ofSeconds(DELAY_BETWEEN_ATTEMPTS_SEC));
        errorMessage = "";
      }
      numAttempts++;

      try {
        IsInitDbDoneResponse isInitDbDoneResponse = client.getIsInitDbDone();

        // IsDone bit determines whether or not YSQL is disabled.
        if (!isInitDbDoneResponse.isDone()) {
          log.debug("YSQL Upgrade is not needed since YSQL is disabled, skipping...");
          break;
        }

        if (isInitDbDoneResponse.hasError()) {
          log.debug(
              "YSQL is not ready, Initdb finished with an error: {}",
              isInitDbDoneResponse.errorMessage());
          errorMessage =
              String.format("isInitDbDone rpc failed: %s", isInitDbDoneResponse.errorMessage());
          continue;
        }

        NodeDetails aliveTServer = findAliveTServer(client, universe);

        // No alive tservers found.
        if (aliveTServer == null) {
          log.debug("Failed to find alive tservers for universe {}", universe.getUniverseUUID());
          errorMessage =
              String.format("No alive tservers found in universe: %s", universe.getUniverseUUID());
          continue;
        }

        log.debug(
            "Tserver: {} is alive in region {}, with ip and port: {}:{}",
            aliveTServer.getNodeName(),
            aliveTServer.getRegion(),
            aliveTServer.cloudInfo.private_ip,
            aliveTServer.tserverRpcPort);
        HostAndPort hp =
            HostAndPort.fromParts(aliveTServer.cloudInfo.private_ip, aliveTServer.tserverRpcPort);

        UpgradeYsqlResponse upgradeYsqlResponse = client.upgradeYsql(hp, useSingleConnection);
        if (upgradeYsqlResponse.hasError()) {
          log.debug(
              "YSQL failed to upgrade for universe {}, to the lastest version. Error: {}",
              universe.getUniverseUUID(),
              upgradeYsqlResponse.errorMessage());
          errorMessage =
              String.format("upgradeYsql rpc failed: %s", upgradeYsqlResponse.errorMessage());
          continue;
        }
      } catch (Exception e) {
        log.error("{} hit error : {}", getName(), e.getMessage());
        errorMessage += String.format(" YBClient error: %s", e.getMessage());
        continue;
      }

      log.debug("Successfully performed YSQL upgrade for universe {}", universe.getUniverseUUID());
      break;
    }
  }

  /*
   * Tries to find alive tserver in same region as the master leader region. If not,
   *   will try to find an alive tserver in any region.
   */
  private NodeDetails findAliveTServer(YBClient client, Universe universe) {
    NodeDetails masterLeaderNode = universe.getMasterLeaderNode();
    String masterLeaderRegion = masterLeaderNode.getRegion();
    NodeDetails aliveTServer = null;

    List<NodeDetails> tserverNodes = universe.getTServers();
    for (NodeDetails currentNode : tserverNodes) {
      log.debug(
          "Current node: {} in region {}, with ip and port: {}:{}",
          currentNode.getNodeName(),
          currentNode.getRegion(),
          currentNode.cloudInfo.private_ip,
          currentNode.tserverRpcPort);
      boolean tServerAlive = isTserverAliveOnNode(currentNode, universe.getMasterAddresses());
      if (tServerAlive && isNodesInSameRegion(masterLeaderNode, currentNode)) {
        return currentNode;
      } else if (tServerAlive) {
        aliveTServer = currentNode;
      }
    }
    return aliveTServer;
  }

  private boolean isNodesInSameRegion(NodeDetails node1, NodeDetails node2) {
    return node1.getRegion().equals(node2.getRegion());
  }
}
