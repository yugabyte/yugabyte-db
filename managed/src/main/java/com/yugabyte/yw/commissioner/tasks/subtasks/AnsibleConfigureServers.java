/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.
 * txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.CallHomeManager.CollectionLevel;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeManager.CertRotateAction;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.CertsRotateParams.CertRotationType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.forms.VMImageUpgradeParams.VmUpgradeTaskType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.NodeStatus;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.helpers.audit.AuditLogConfig;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.function.Supplier;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class AnsibleConfigureServers extends NodeTaskBase {

  @Inject
  protected AnsibleConfigureServers(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public UpgradeTaskType type = UpgradeTaskType.Everything;
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

    public boolean installThirdPartyPackages = false;

    // Set it to clean previous master state on restart. It is just a hint to clean
    // old master state but may not be used if it is illegal.
    public boolean resetMasterState = false;

    // This sets the flag master_join_existing_universe to true by default in the conf file, unless
    // it is overridden e.g in CreateUniverse.
    public boolean masterJoinExistingCluster = true;

    public AuditLogConfig auditLogConfig = null;
    public Map<String, String> ybcGflags = new HashMap<>();
    public boolean overrideNodePorts = false;
    // Supplier for master addresses override which is invoked only when the subtask starts
    // execution.
    @Nullable public Supplier<String> masterAddrsOverride;

    @JsonIgnore
    public String getMasterAddrsOverride() {
      String masterAddresses = masterAddrsOverride == null ? null : masterAddrsOverride.get();
      if (StringUtils.isNotBlank(masterAddresses)) {
        log.info("Using the master addresses {} from the override", masterAddresses);
      }
      return masterAddresses;
    }
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.debug("AnsibleConfigureServers run called for {}", taskParams().getUniverseUUID());
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    taskParams().useSystemd =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd;
    String processType = taskParams().getProperty("processType");
    boolean resetMasterState = false;
    if (taskParams().resetMasterState
        && taskParams().isMasterInShellMode
        && ServerType.MASTER.toString().equalsIgnoreCase(processType)) {
      // The check for flag isMasterInShellMode also makes sure that this node is intended
      // to join an existing cluster.
      NodeDetails node = universe.getNode(taskParams().nodeName);
      if (node.masterState != MasterState.Configured) {
        // Reset may be set only if node is not a master.
        // Once isMaster is set, it can be tied to a cluster.
        resetMasterState =
            isChangeMasterConfigDone(universe, node, false, node.cloudInfo.private_ip);
      }
    }
    log.debug(
        "Reset master state is now {} for universe {}. It was {}",
        resetMasterState,
        universe.getUniverseUUID(),
        taskParams().resetMasterState);
    taskParams().resetMasterState = resetMasterState;
    // Execute the ansible command.
    ShellResponse response =
        getNodeManager()
            .nodeCommand(NodeManager.NodeCommandType.Configure, taskParams())
            .processErrors();

    if (taskParams().type == UpgradeTaskType.Everything && !taskParams().updateMasterAddrsOnly) {
      // Check cronjob status if installing software.
      if (!taskParams().useSystemd) {
        response =
            getNodeManager().nodeCommand(NodeManager.NodeCommandType.CronCheck, taskParams());
      }

      universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
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
                    "Updated {} cronjob status to inactive from universe {}",
                    nodeName,
                    taskParams().getUniverseUUID());
              }
            };
        saveUniverseDetails(updater);
      }

      long inactiveCronNodes = 0;
      if (!taskParams().useSystemd && !taskParams().isSystemdUpgrade) {
        inactiveCronNodes = universe.getNodes().stream().filter(node -> !node.cronsActive).count();
      }
      // Create an alert if the cronjobs failed to be created.
      metricService.setMetric(
          buildMetricTemplate(PlatformMetrics.UNIVERSE_INACTIVE_CRON_NODES, universe),
          inactiveCronNodes);

      // AnsibleConfigureServers performs multiple operations based on the parameters.
      if (StringUtils.isBlank(processType)) {
        // Set node state to SoftwareInstalled only when-
        // configuration type Everything + not updating master address only = installing software
        // TODO: Why is upgrade task type used to map to node state update?
        setNodeStatus(NodeStatus.builder().nodeState(NodeState.SoftwareInstalled).build());
      }
    }
  }

  @Override
  public int getRetryLimit() {
    return 2;
  }
}
