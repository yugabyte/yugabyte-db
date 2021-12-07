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

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.NodeStatus;

import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class AnsibleCreateServer extends NodeTaskBase {

  @Inject
  protected AnsibleCreateServer(
      BaseTaskDependencies baseTaskDependencies, NodeManager nodeManager) {
    super(baseTaskDependencies, nodeManager);
  }

  // Additional parameters for this task.
  public static class Params extends NodeTaskParams {
    // The VPC into which the node is to be provisioned.
    public String subnetId;
    // The secondary subnet into which the node's network interface needs to be provisioned.
    public String secondarySubnetId = null;

    public boolean assignPublicIP = true;
    public boolean assignStaticPublicIP = false;

    // If this is set to the universe's AWS KMS CMK arn, AWS EBS volume
    // encryption will be enabled
    public String cmkArn;

    // If set, we will use this Amazon Resource Name of the user's
    // instance profile instead of an access key id and secret
    public String ipArnString;
    public String machineImage;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Provider p = taskParams().getProvider();
    List<AccessKey> accessKeys = AccessKey.getAll(p.uuid);
    boolean skipProvision = false;

    // For now we will skipProvision if it's set in accessKeys.
    if (p.code.equals(Common.CloudType.onprem.name()) && accessKeys.size() > 0) {
      skipProvision = accessKeys.get(0).getKeyInfo().skipProvisioning;
    }

    if (skipProvision) {
      log.info("Skipping ansible creation.");
    } else if (instanceExists(taskParams())) {
      log.info("Skipping creation of already existing instance {}", taskParams().nodeName);
    } else {
      //   Execute the ansible command.
      ShellResponse response =
          getNodeManager().nodeCommand(NodeManager.NodeCommandType.Create, taskParams());
      processShellResponse(response);
      setNodeStatus(NodeStatus.builder().nodeState(NodeState.InstanceCreated).build());
    }
  }
}
