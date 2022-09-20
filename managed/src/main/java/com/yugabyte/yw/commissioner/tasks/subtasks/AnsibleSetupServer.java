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
import com.yugabyte.yw.forms.VMImageUpgradeParams.VmUpgradeTaskType;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeStatus;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;

import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class AnsibleSetupServer extends NodeTaskBase {

  @Inject
  protected AnsibleSetupServer(BaseTaskDependencies baseTaskDependencies, NodeManager nodeManager) {
    super(baseTaskDependencies, nodeManager);
  }

  // Additional parameters for this task.
  public static class Params extends NodeTaskParams {
    // The subnet into which the node's network interface needs to be provisioned.
    public String subnetId;

    // For AWS, this will dictate if we use the Time Sync Service.
    public boolean useTimeSync = false;

    public String machineImage;

    // Systemd vs Cron Option (Default: Cron)
    public boolean useSystemd = false;

    // For cron to systemd upgrades
    public boolean isSystemdUpgrade = false;
    // To use custom image flow if it is a VM upgrade with custom images.
    public VmUpgradeTaskType vmUpgradeTaskType = VmUpgradeTaskType.None;

    // In case a node doesn't have custom AMI, ignore the value of USE_CUSTOM_IMAGE config.
    public boolean ignoreUseCustomImageConfig = false;
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

    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    taskParams().useSystemd =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd;

    // For now we will skipProvision if it's set in accessKeys.
    if (p.code.equals(Common.CloudType.onprem.name()) && accessKeys.size() > 0) {
      skipProvision = accessKeys.get(0).getKeyInfo().skipProvisioning;
    }

    if (skipProvision) {
      log.info("Skipping ansible provision.");
    } else {
      // Execute the ansible command.
      getNodeManager()
          .nodeCommand(NodeManager.NodeCommandType.Provision, taskParams())
          .processErrors();
      setNodeStatus(NodeStatus.builder().nodeState(NodeState.ServerSetup).build());
    }
  }

  @Override
  public int getRetryLimit() {
    return 2;
  }
}
