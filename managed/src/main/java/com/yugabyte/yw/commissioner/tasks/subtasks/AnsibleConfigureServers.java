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

import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.CallHomeManager.CollectionLevel;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeManager.CertRotateAction;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.CertsRotateParams.CertRotationType;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.forms.VMImageUpgradeParams.VmUpgradeTaskType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeStatus;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class AnsibleConfigureServers extends NodeTaskBase {

  @Inject
  protected AnsibleConfigureServers(
      BaseTaskDependencies baseTaskDependencies, NodeManager nodeManager) {
    super(baseTaskDependencies, nodeManager);
  }

  public static class Params extends NodeTaskParams {
    public UpgradeTaskType type = UpgradeTaskParams.UpgradeTaskType.Everything;
    public String ybSoftwareVersion = null;

    // Optional params.
    public boolean isMasterInShellMode = false;
    public boolean isMaster = false;
    public boolean enableYSQL = false;
    public boolean enableYCQL = false;
    public boolean enableYSQLAuth = false;
    public boolean enableYCQLAuth = false;
    public boolean enableYEDIS = false;
    public Map<String, String> gflags = new HashMap<>();
    public Set<String> gflagsToRemove = new HashSet<>();
    public boolean updateMasterAddrsOnly = false;
    public CollectionLevel callhomeLevel;
    // Development params.
    public String itestS3PackagePath = "";
    // ToggleTls params.
    public boolean enableNodeToNodeEncrypt = false;
    public boolean enableClientToNodeEncrypt = false;
    public boolean allowInsecure = true;
    // 0 => No change in node-to-node encryption
    // > 0 => node-to-node encryption is enabled
    // < 0 => node-to-node encryption is disabled
    public int nodeToNodeChange = 0;
    // Cert rotation related params
    public CertRotationType rootCARotationType = CertRotationType.None;
    public CertRotationType clientRootCARotationType = CertRotationType.None;
    public CertRotateAction certRotateAction = CertRotateAction.ROTATE_CERTS;

    // For cron to systemd upgrades
    public boolean isSystemdUpgrade = false;
    // To use custom image flow if it is a VM upgrade with custom images.
    public VmUpgradeTaskType vmUpgradeTaskType = VmUpgradeTaskType.None;

    // In case a node doesn't have custom AMI, ignore the value of USE_CUSTOM_IMAGE config.
    public boolean ignoreUseCustomImageConfig = false;

    // This sets the flag master_join_existing_universe to true by default in the conf file, unless
    // it is overridden e.g in CreateUniverse.
    public boolean masterJoinExistingCluster = true;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.debug("AnsibleConfigureServers run called for {}", taskParams().universeUUID);
    Universe universe_temp = Universe.getOrBadRequest(taskParams().universeUUID);
    taskParams().useSystemd =
        universe_temp.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd;
    // Execute the ansible command.
    ShellResponse response =
        getNodeManager()
            .nodeCommand(NodeManager.NodeCommandType.Configure, taskParams())
            .processErrors();

    if (taskParams().type == UpgradeTaskParams.UpgradeTaskType.Everything
        && !taskParams().updateMasterAddrsOnly) {
      // Check cronjob status if installing software.
      if (!taskParams().useSystemd) {
        response =
            getNodeManager().nodeCommand(NodeManager.NodeCommandType.CronCheck, taskParams());
      }

      // Create an alert if the cronjobs failed to be created on this node.
      Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
      if (response.code != 0 || taskParams().useSystemd) {
        String nodeName = taskParams().nodeName;

        // Persist node cronjob status into the DB.
        UniverseUpdater updater =
            new UniverseUpdater() {
              @Override
              public void run(Universe universe) {
                NodeDetails node = universe.getNode(nodeName);
                node.cronsActive = false;
                log.info(
                    "Updated "
                        + nodeName
                        + " cronjob status to inactive from universe "
                        + taskParams().universeUUID);
              }
            };
        saveUniverseDetails(updater);
      }

      if (!taskParams().useSystemd) {
        long inactiveCronNodes =
            universe.getNodes().stream().filter(node -> !node.cronsActive).count();
        metricService.setMetric(
            buildMetricTemplate(PlatformMetrics.UNIVERSE_INACTIVE_CRON_NODES, universe),
            inactiveCronNodes);
      }

      // AnsibleConfigureServers performs multiple operations based on the parameters.
      String processType = taskParams().getProperty("processType");
      if (ServerType.MASTER.toString().equalsIgnoreCase(processType)) {
        setNodeStatus(NodeStatus.builder().masterState(MasterState.Configured).build());
      } else {
        // We set the node state to SoftwareInstalled when configuration type is Everything.
        // TODO: Why is upgrade task type used to map to node state update?
        setNodeStatus(NodeStatus.builder().nodeState(NodeState.SoftwareInstalled).build());
      }
    }
  }
}
