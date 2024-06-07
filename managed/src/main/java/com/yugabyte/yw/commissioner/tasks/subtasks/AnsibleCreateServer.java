/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.RecoverableException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.NodeStatus;
import javax.inject.Inject;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class AnsibleCreateServer extends NodeTaskBase {

  @Inject
  protected AnsibleCreateServer(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Additional parameters for this task.
  public static class Params extends NodeTaskParams {
    // The VPC into which the node is to be provisioned.
    public String subnetId;
    // The secondary subnet into which the node's network interface needs to be provisioned.
    public String secondarySubnetId = null;

    public boolean assignPublicIP = true;
    public boolean assignStaticPublicIP = false;

    public boolean useSpotInstance = false;
    public Double spotPrice = 0.0;

    // If set, we will use this Amazon Resource Name of the user's
    // instance profile instead of an access key id and secret
    public String ipArnString;
    @Getter @Setter private String machineImage;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Provider p = taskParams().getProvider();
    boolean skipProvision = false;

    if (p.getCode().equals(Common.CloudType.onprem.name())) {
      skipProvision = p.getDetails().skipProvisioning;
    }

    if (skipProvision) {
      log.info("Skipping ansible creation.");
    } else if (instanceExists(taskParams())) {
      log.info("Waiting for SSH to succeed on existing instance {}", taskParams().nodeName);
      getNodeManager()
          .nodeCommand(NodeManager.NodeCommandType.Wait_For_Connection, taskParams())
          .processErrors();
      setNodeStatus(NodeStatus.builder().nodeState(NodeState.InstanceCreated).build());
    } else {
      // Execute the ansible command to create the node.
      // It waits for SSH connection to work.
      ShellResponse response =
          getNodeManager()
              .nodeCommand(NodeManager.NodeCommandType.Create, taskParams())
              .processErrors();
      setNodeStatus(NodeStatus.builder().nodeState(NodeState.InstanceCreated).build());
      if (p.getCode().equals(CloudType.azu.name())) {
        // Parse into a json object.
        JsonNode jsonNodeTmp = Json.parse(response.message);
        if (jsonNodeTmp.isArray()) {
          jsonNodeTmp = jsonNodeTmp.get(0);
        }
        final JsonNode jsonNode = jsonNodeTmp;
        String nodeName = taskParams().nodeName;

        // Update the node details and persist into the DB.
        UniverseUpdater updater =
            new UniverseUpdater() {
              @Override
              public void run(Universe universe) {
                // Get the details of the node to be updated.
                NodeDetails node = universe.getNode(nodeName);
                JsonNode lunIndexesJson = jsonNode.get("lun_indexes");
                if (lunIndexesJson != null && lunIndexesJson.isArray()) {
                  node.cloudInfo.lun_indexes = new Integer[lunIndexesJson.size()];
                  for (int i = 0; i < lunIndexesJson.size(); i++) {
                    node.cloudInfo.lun_indexes[i] = lunIndexesJson.get(i).asInt();
                  }
                }
              }
            };
        // Save the updated universe object.
        saveUniverseDetails(updater);
      }
    }
  }

  @Override
  public int getRetryLimit() {
    return 2;
  }

  @Override
  public boolean onFailure(TaskInfo taskInfo, Throwable cause) {
    Params params = taskParams();

    if (instanceExists(taskParams())) {
      if (cause instanceof RecoverableException) {
        return super.onFailure(taskInfo, cause);
      } else {
        // TODO: retry in a different AZ?
        log.warn("Instance creation in {} failed", params.getAZ().getName());

        AnsibleDestroyServer.Params destroyParams = new AnsibleDestroyServer.Params();
        destroyParams.deviceInfo = params.deviceInfo;
        destroyParams.azUuid = params.azUuid;
        destroyParams.nodeName = params.nodeName;
        destroyParams.nodeUuid = params.nodeUuid;
        destroyParams.setUniverseUUID(params.getUniverseUUID());
        destroyParams.isForceDelete = false;
        destroyParams.deleteNode = false;
        destroyParams.deleteRootVolumes = true;
        destroyParams.instanceType = params.instanceType;
        AnsibleDestroyServer task = createTask(AnsibleDestroyServer.class);
        task.initialize(destroyParams);
        task.setUserTaskUUID(getUserTaskUUID());
        task.run();
        return true;
      }
    }

    return false;
  }
}
