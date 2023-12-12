/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.services.config.YbClientConfig;
import com.yugabyte.yw.common.services.config.YbClientConfigFactory;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.CommonNet;
import org.yb.CommonTypes;
import org.yb.client.ChangeConfigResponse;
import org.yb.client.GetMasterRegistrationResponse;
import org.yb.client.YBClient;

@Slf4j
public class ChangeMasterConfig extends UniverseTaskBase {

  private static final int WAIT_FOR_CHANGE_COMPLETED_INITIAL_DELAY_MILLIS = 1000;
  private static final int WAIT_FOR_CHANGE_COMPLETED_MAX_DELAY_MILLIS = 20000;

  private static final Duration YBCLIENT_ADMIN_OPERATION_TIMEOUT = Duration.ofMinutes(15);
  private final YbClientConfigFactory ybcClientConfigFactory;

  @Inject
  protected ChangeMasterConfig(
      BaseTaskDependencies baseTaskDependencies, YbClientConfigFactory ybcConfigFactory) {
    super(baseTaskDependencies);
    this.ybcClientConfigFactory = ybcConfigFactory;
  }

  // Create an enum specifying the operation type.
  public enum OpType {
    AddMaster,
    RemoveMaster
  }

  // Parameters for change master config task.
  public static class Params extends NodeTaskParams {
    // When AddMaster, the master is added to the current master quorum, otherwise it is deleted.
    public OpType opType;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName()
        + "("
        + taskParams().nodeName
        + ", "
        + taskParams().opType.toString()
        + ")";
  }

  @Override
  public String toString() {
    return getName();
  }

  @Override
  public void run() {
    // Get the master addresses.
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String masterAddresses = universe.getMasterAddresses();
    log.info(
        "Running {}: universe = {}, masterAddress = {}",
        getName(),
        taskParams().getUniverseUUID(),
        masterAddresses);
    if (StringUtils.isBlank(masterAddresses)) {
      throw new IllegalStateException(
          "No master host/ports for a change config op in " + taskParams().getUniverseUUID());
    }
    boolean isAddMasterOp = (taskParams().opType == OpType.AddMaster);
    // Get the node details and perform the change config operation.
    NodeDetails node = universe.getNode(taskParams().nodeName);
    if (node == null) {
      boolean throwExc = true;
      // on K8s we don't raise exception here.
      if (isAddMasterOp == false) {
        if (Util.isKubernetesBasedUniverse(universe)) {
          throwExc = false;
        }
      }
      if (throwExc) {
        throw new RuntimeException("Cannot find node " + taskParams().nodeName);
      } else {
        log.info(
            "Config change remove is already done for node {} as node is no longer in universe",
            taskParams().nodeName);
        return;
      }
    }
    // If the cluster has a secondary IP, we want to ensure that we use the correct addresses.
    // The ipToUse is the address that we need to add to the config.
    // The ipForPlatform is the address that the platform uses to connect to the host.
    boolean hasSecondaryIp =
        node.cloudInfo.secondary_private_ip != null
            && !node.cloudInfo.secondary_private_ip.equals("null");
    boolean shouldUseSecondary =
        universe.getConfig().getOrDefault(Universe.DUAL_NET_LEGACY, "true").equals("false");
    String ipToUse =
        hasSecondaryIp && shouldUseSecondary
            ? node.cloudInfo.secondary_private_ip
            : node.cloudInfo.private_ip;
    String certificate = universe.getCertificateNodetoNode();
    YbClientConfig config = ybcClientConfigFactory.create(masterAddresses, certificate);
    // The call changeMasterConfig is not idempotent. The client library internally keeps retrying
    // for a long time until it gives up if the node is already added or removed.
    // This optional check ensures that changeMasterConfig is not invoked if the operation is
    // already done for the node.
    if (isChangeMasterConfigDone(universe, node, isAddMasterOp, ipToUse)) {
      log.info(
          "Config change (add={}) is already done for node {}({}:{})",
          isAddMasterOp,
          node.nodeName,
          node.cloudInfo.private_ip,
          node.masterRpcPort);
      if (isAddMasterOp) {
        // Even if it's returned as one of the masters - it can be not bootstrapped yet.
        waitForChangeToComplete(config, node, ipToUse);
      }
      return;
    }
    // The param useHostPort should be true when removing a dead master.
    // Otherwise, ybclient will attempt to fetch the UUID of the master being changed.
    // Add operation always requires useHostPort=false.
    boolean useHostPort = false;
    if (!isAddMasterOp) {
      // True if master server is not alive else false.
      // TODO Use isMasterAliveOnNode once isMaster is set to true for all tasks.
      // AddNodeToUniverse does not set it to true when this is called.
      useHostPort = !isServerAlive(node, ServerType.MASTER, masterAddresses);
    }
    ChangeConfigResponse response = null;
    config.setAdminOperationTimeout(YBCLIENT_ADMIN_OPERATION_TIMEOUT);
    YBClient client = ybService.getClientWithConfig(config);
    try {
      log.info(
          "Starting changeMasterConfig({}:{}, op={}, useHost={})",
          node.cloudInfo.private_ip,
          node.masterRpcPort,
          taskParams().opType,
          useHostPort);
      response =
          client.changeMasterConfig(
              node.cloudInfo.private_ip, node.masterRpcPort, isAddMasterOp, useHostPort, ipToUse);
    } catch (Exception e) {
      String msg =
          String.format(
              "Error while performing master change config on node %s (%s:%d) - %s",
              node.nodeName, ipToUse, node.masterRpcPort, e.getMessage());
      log.error(msg, e);
      throw new RuntimeException(msg);
    } finally {
      ybService.closeClient(client, masterAddresses);
    }
    // If there was an error, throw an exception.
    if (response != null && response.hasError()) {
      String msg =
          String.format(
              "ChangeConfig response has error for node %s (%s:%d) - %s",
              node.nodeName, ipToUse, node.masterRpcPort, response.errorMessage());
      log.error(msg);
      throw new RuntimeException(msg);
    }

    waitForChangeToComplete(config, node, ipToUse);
  }

  private void waitForChangeToComplete(
      YbClientConfig clientConfig, NodeDetails node, String masterIp) {

    boolean waitForMasterConfigChangeCheckEnabled =
        confGetter.getConfForScope(getUniverse(), UniverseConfKeys.changeMasterConfigCheckEnabled);
    if (!waitForMasterConfigChangeCheckEnabled) {
      return;
    }
    Duration maxWaitTime =
        confGetter.getConfForScope(getUniverse(), UniverseConfKeys.changeMasterConfigCheckTimeout);
    YBClient client = ybService.getClientWithConfig(clientConfig);
    CommonTypes.PeerRole role;
    try {
      long endTime = System.currentTimeMillis() + maxWaitTime.toMillis();
      boolean checkSuccessful = false;
      int iterationNumber = 0;
      do {
        log.info(
            "Checking master registration response for ({}:{}, op={})",
            masterIp,
            node.masterRpcPort,
            taskParams().opType);
        List<GetMasterRegistrationResponse> masterRegistrationResponseList =
            client.getMasterRegistrationResponseList();
        GetMasterRegistrationResponse registrationResponse =
            getMasterRegistrationResponse(
                masterRegistrationResponseList, masterIp, node.masterRpcPort);
        role = registrationResponse != null ? registrationResponse.getRole() : null;
        log.info(
            "Master ({}:{}) role is {}",
            masterIp,
            node.masterRpcPort,
            role != null ? role.name() : null);
        if (taskParams().opType == OpType.AddMaster) {
          if (role != null
              && (role.equals(CommonTypes.PeerRole.LEADER)
                  || role.equals(CommonTypes.PeerRole.FOLLOWER))) {
            checkSuccessful = true;
            break;
          }
        } else if (taskParams().opType == OpType.RemoveMaster) {
          if (role == null || role.equals(CommonTypes.PeerRole.NON_PARTICIPANT)) {
            checkSuccessful = true;
            break;
          }
        }
        waitFor(
            Duration.ofMillis(
                Util.getExponentialBackoffDelayMs(
                    WAIT_FOR_CHANGE_COMPLETED_INITIAL_DELAY_MILLIS,
                    WAIT_FOR_CHANGE_COMPLETED_MAX_DELAY_MILLIS,
                    iterationNumber++)));
      } while (System.currentTimeMillis() < endTime);
      if (!checkSuccessful) {
        throw new RuntimeException(
            taskParams().opType.name() + " operation has not completed within " + maxWaitTime);
      }
    } catch (Exception e) {
      String msg =
          String.format(
              "Error while checking master registration status on node %s (%s:%d) - %s",
              node.nodeName, masterIp, node.masterRpcPort, e.getMessage());
      log.error(msg, e);
      throw new RuntimeException(msg);
    } finally {
      ybService.closeClient(client, clientConfig.getMasterHostPorts());
    }
  }

  private GetMasterRegistrationResponse getMasterRegistrationResponse(
      List<GetMasterRegistrationResponse> masterRegistrationResponseList,
      String privateIp,
      int port) {
    for (GetMasterRegistrationResponse response : masterRegistrationResponseList) {
      for (CommonNet.HostPortPB hostPortPB :
          response.getServerRegistration().getPrivateRpcAddressesList()) {
        if (hostPortPB.getHost().equals(privateIp) && (hostPortPB.getPort() == port)) {
          return response;
        }
      }
    }
    return null;
  }
}
