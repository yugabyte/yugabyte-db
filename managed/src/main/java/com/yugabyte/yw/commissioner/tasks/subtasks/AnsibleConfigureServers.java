/*
 * Copyright 2019 YugabyteDB, Inc. and Contributors
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
import com.yugabyte.yw.commissioner.tasks.payload.NodeAgentRpcPayload;
import com.yugabyte.yw.common.CallHomeManager.CollectionLevel;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeManager.CertRotateAction;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.forms.CertsRotateParams.CertRotationType;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.forms.VMImageUpgradeParams.VmUpgradeTaskType;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.NodeStatus;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.helpers.UpgradeDetails.YsqlMajorVersionUpgradeState;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.function.Supplier;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class AnsibleConfigureServers extends NodeTaskBase {
  private final NodeAgentRpcPayload nodeAgentRpcPayload;

  @Inject
  protected AnsibleConfigureServers(
      BaseTaskDependencies baseTaskDependencies, NodeAgentRpcPayload nodeAgentRpcPayload) {
    super(baseTaskDependencies);
    this.nodeAgentRpcPayload = nodeAgentRpcPayload;
  }

  public static class Params extends NodeTaskParams {
    public UpgradeTaskType type = UpgradeTaskType.Everything;
    public String ybSoftwareVersion = null;

    // Optional params.
    public boolean isMasterInShellMode = false;
    public boolean enableYSQL = false;
    public boolean enableConnectionPooling = false;
    public boolean enableYCQL = false;
    public boolean enableYSQLAuth = false;
    public boolean enableYCQLAuth = false;
    public boolean enableYEDIS = false;
    public Map<String, String> gflags = new HashMap<>();
    public Set<String> gflagsToRemove = new HashSet<>();
    public boolean updateMasterAddrsOnly = false;
    public CollectionLevel callhomeLevel;
    // ToggleTls params.
    public boolean enableNodeToNodeEncrypt = false;
    public boolean enableClientToNodeEncrypt = false;
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
    public QueryLogConfig queryLogConfig = null;
    public MetricsExportConfig metricsExportConfig = null;
    public Map<String, String> ybcGflags = new HashMap<>();
    public boolean overrideNodePorts = false;
    // Amount of memory to limit the postgres process to via the ysql cgroup (in megabytes)
    public int cgroupSize = 0;
    // If configured will skip install-package role in ansible and use node-agent rpc instead.
    public boolean skipDownloadSoftware = false;
    // Supplier for master addresses override which is invoked only when the subtask starts
    // execution.
    @JsonIgnore @Nullable public Supplier<String> masterAddrsOverride;

    @JsonIgnore
    public String getMasterAddrsOverride() {
      String masterAddresses = masterAddrsOverride == null ? null : masterAddrsOverride.get();
      if (StringUtils.isNotBlank(masterAddresses)) {
        log.info("Using the master addresses {} from the override", masterAddresses);
      }
      return masterAddresses;
    }

    public YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState = null;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.debug("AnsibleConfigureServers run called for {}", taskParams().getUniverseUUID());
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails nodeDetails = universe.getNode(taskParams().nodeName);
    taskParams().useSystemd =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd;
    String processType = taskParams().getProperty("processType");
    boolean resetMasterState = false;
    if (taskParams().resetMasterState
        && taskParams().isMasterInShellMode
        && ServerType.MASTER.toString().equalsIgnoreCase(processType)) {
      // The check for flag isMasterInShellMode also makes sure that this node is intended
      // to join an existing cluster.
      if (nodeDetails.masterState != MasterState.Configured) {
        // Reset may be set only if node is not a master.
        // Once isMaster is set, it can be tied to a cluster.
        resetMasterState =
            isChangeMasterConfigDone(
                universe, nodeDetails, false, nodeDetails.cloudInfo.private_ip);
      }
    }

    log.debug(
        "Reset master state is now {} for universe {}. It was {}",
        resetMasterState,
        universe.getUniverseUUID(),
        taskParams().resetMasterState);
    taskParams().resetMasterState = resetMasterState;
    Optional<NodeAgent> optional =
        confGetter.getGlobalConf(GlobalConfKeys.nodeAgentDisableConfigureServer)
            ? Optional.empty()
            : nodeUniverseManager.maybeGetNodeAgent(
                getUniverse(), nodeDetails, true /*check feature flag*/);
    taskParams().skipDownloadSoftware = optional.isPresent();
    if (optional.isPresent()
        && (taskParams().type == UpgradeTaskType.GFlags
            || taskParams().type == UpgradeTaskType.YbcGFlags)) {
      log.info("Updating gflags using node agent {}", optional.get());
      nodeAgentRpcPayload.runServerGFlagsWithNodeAgent(
          optional.get(), universe, nodeDetails, taskParams());
      return;
    }
    // Execute the ansible command.
    ShellResponse response =
        getNodeManager()
            .nodeCommand(NodeManager.NodeCommandType.Configure, taskParams())
            .processErrors();
    String taskSubType = taskParams().getProperty("taskSubType");
    if (optional.isPresent()
        && (taskParams().type == UpgradeTaskType.Everything
            || taskParams().type == UpgradeTaskType.Software)) {
      log.info("Installing software using node agent {}", optional.get());
      nodeAgentClient.runConfigureServer(
          optional.get(),
          nodeAgentRpcPayload.setUpConfigureServerBits(
              universe, nodeDetails, taskParams(), optional.get()),
          NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
      if (taskParams().type == UpgradeTaskType.Software) {
        if (taskSubType == null) {
          throw new RuntimeException("Invalid taskSubType property: " + taskSubType);
        } else if (taskSubType.equals(UpgradeTaskParams.UpgradeTaskSubType.Download.toString())) {
          nodeAgentClient.runDownloadSoftware(
              optional.get(),
              nodeAgentRpcPayload.setupDownloadSoftwareBits(
                  universe, nodeDetails, taskParams(), optional.get()),
              NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
        } else if (taskSubType.equals(UpgradeTaskParams.UpgradeTaskSubType.Install.toString())) {
          nodeAgentClient.runInstallSoftware(
              optional.get(),
              nodeAgentRpcPayload.setupInstallSoftwareBits(
                  universe, nodeDetails, taskParams(), optional.get()),
              NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
        }
      } else if (shouldInstallDbSoftware(
          universe, taskParams().ignoreUseCustomImageConfig, taskParams().vmUpgradeTaskType)) {
        nodeAgentClient.runDownloadSoftware(
            optional.get(),
            nodeAgentRpcPayload.setupDownloadSoftwareBits(
                universe, nodeDetails, taskParams(), optional.get()),
            NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
        nodeAgentClient.runInstallSoftware(
            optional.get(),
            nodeAgentRpcPayload.setupInstallSoftwareBits(
                universe, nodeDetails, taskParams(), optional.get()),
            NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
      }

      if (taskParams().isEnableYbc()) {
        log.info("Installing YBC using node agent {}", optional.get());
        nodeAgentClient.runInstallYbcSoftware(
            optional.get(),
            nodeAgentRpcPayload.setupInstallYbcSoftwareBits(
                universe, nodeDetails, taskParams(), optional.get()),
            NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
        nodeAgentRpcPayload.runServerGFlagsWithNodeAgent(
            optional.get(), universe, nodeDetails, ServerType.CONTROLLER.toString(), taskParams());
      }
      if (taskParams().otelCollectorEnabled) {
        nodeAgentClient.runInstallOtelCollector(
            optional.get(),
            nodeAgentRpcPayload.setupInstallOtelCollectorBits(
                universe, nodeDetails, taskParams(), optional.get()),
            NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
      }
      if (taskParams().cgroupSize > 0) {
        nodeAgentClient.runSetupCGroupInput(
            optional.get(),
            nodeAgentRpcPayload.setupSetupCGroupBits(
                universe, nodeDetails, taskParams(), optional.get()),
            NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
      }
    }
    if (optional.isPresent() && taskParams().type == UpgradeTaskType.ToggleTls) {
      if (UpgradeTaskParams.UpgradeTaskSubType.Round1GFlagsUpdate.name().equals(taskSubType)
          || UpgradeTaskParams.UpgradeTaskSubType.Round2GFlagsUpdate.name().equals(taskSubType)
          || UpgradeTaskParams.UpgradeTaskSubType.YbcGflagsUpdate.name().equals(taskSubType)) {
        log.info(
            "Updating toggle TLS gflags using node agent {} for taskSubType {}",
            optional.get(),
            taskSubType);
        nodeAgentRpcPayload.runServerGFlagsWithNodeAgent(
            optional.get(), universe, nodeDetails, taskParams());
        return;
      }
    }

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
