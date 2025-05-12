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
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.cloud.gcp.GCPCloudImpl;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.CallHomeManager.CollectionLevel;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeManager.CertRotateAction;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.CertsRotateParams.CertRotationType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.forms.VMImageUpgradeParams.VmUpgradeTaskType;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.NodeStatus;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.helpers.UpgradeDetails.YsqlMajorVersionUpgradeState;
import com.yugabyte.yw.models.helpers.audit.AuditLogConfig;
import com.yugabyte.yw.nodeagent.InstallSoftwareInput;
import com.yugabyte.yw.nodeagent.ServerGFlagsInput;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.function.Supplier;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class AnsibleConfigureServers extends NodeTaskBase {
  private final ReleaseManager releaseManager;

  @Inject
  protected AnsibleConfigureServers(
      BaseTaskDependencies baseTaskDependencies, ReleaseManager releaseManager) {
    super(baseTaskDependencies);
    this.releaseManager = releaseManager;
  }

  private InstallSoftwareInput.Builder fillYbReleaseMetadata(
      Universe universe,
      Provider provider,
      NodeDetails node,
      String ybSoftwareVersion,
      Region region,
      Architecture arch,
      InstallSoftwareInput.Builder installSoftwareInputBuilder,
      NodeAgent nodeAgent,
      String customTmpDirectory) {
    Map<String, String> envConfig = CloudInfoInterface.fetchEnvVars(provider);
    String ybServerPackage = null;
    ReleaseContainer release = releaseManager.getReleaseByVersion(ybSoftwareVersion);
    if (release != null) {
      if (arch != null) {
        ybServerPackage = release.getFilePath(arch);
      } else {
        ybServerPackage = release.getFilePath(region);
      }
    }
    installSoftwareInputBuilder.setYbPackage(ybServerPackage);
    if (release.isS3Download(ybServerPackage)) {
      installSoftwareInputBuilder.setS3RemoteDownload(true);
      installSoftwareInputBuilder.setAwsAccessKey(envConfig.get("AWS_ACCESS_KEY_ID"));
      installSoftwareInputBuilder.setAwsSecretKey(envConfig.get("AWS_SECRET_ACCESS_KEY"));
    } else if (release.isGcsDownload(ybServerPackage)) {
      installSoftwareInputBuilder.setGcsRemoteDownload(true);
      // Upload the Credential json to the remote host.
      nodeAgentClient.uploadFile(
          nodeAgent,
          envConfig.get(GCPCloudImpl.GCE_PROJECT_PROPERTY),
          customTmpDirectory
              + "/"
              + Paths.get(envConfig.get(GCPCloudImpl.GCE_PROJECT_PROPERTY))
                  .getFileName()
                  .toString(),
          "yugabyte",
          0,
          null);
      installSoftwareInputBuilder.setGcsCredentialsJson(
          customTmpDirectory
              + "/"
              + Paths.get(envConfig.get(GCPCloudImpl.GCE_PROJECT_PROPERTY))
                  .getFileName()
                  .toString());
    } else if (release.isHttpDownload(ybServerPackage)) {
      installSoftwareInputBuilder.setHttpRemoteDownload(true);
      if (StringUtils.isNotBlank(release.getHttpChecksum())) {
        installSoftwareInputBuilder.setHttpPackageChecksum(release.getHttpChecksum().toLowerCase());
      }
    } else if (release.hasLocalRelease()) {
      // Upload the release to the node.
      nodeAgentClient.uploadFile(
          nodeAgent,
          ybServerPackage,
          customTmpDirectory + "/" + Paths.get(ybServerPackage).getFileName().toString(),
          "yugabyte",
          0,
          null);
      installSoftwareInputBuilder.setYbPackage(
          customTmpDirectory + "/" + Paths.get(ybServerPackage).getFileName().toString());
    }
    installSoftwareInputBuilder.setRemoteTmp(customTmpDirectory);
    installSoftwareInputBuilder.setYbHomeDir(provider.getYbHome());
    return installSoftwareInputBuilder;
  }

  private InstallSoftwareInput setupInstallSoftwareBits(
      Universe universe, NodeDetails nodeDetails, Params taskParams, NodeAgent nodeAgent) {
    InstallSoftwareInput.Builder installSoftwareInputBuilder = InstallSoftwareInput.newBuilder();
    Cluster cluster = universe.getCluster(nodeDetails.placementUuid);
    Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
    String customTmpDirectory =
        confGetter.getConfForScope(provider, ProviderConfKeys.remoteTmpDirectory);
    installSoftwareInputBuilder =
        fillYbReleaseMetadata(
            universe,
            provider,
            nodeDetails,
            taskParams.ybSoftwareVersion,
            taskParams.getRegion(),
            universe.getUniverseDetails().arch,
            installSoftwareInputBuilder,
            nodeAgent,
            customTmpDirectory);

    return installSoftwareInputBuilder.build();
  }

  public static class Params extends NodeTaskParams {
    public UpgradeTaskType type = UpgradeTaskType.Everything;
    public String ybSoftwareVersion = null;

    // Optional params.
    public boolean isMasterInShellMode = false;
    public boolean isMaster = false;
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
        confGetter.getGlobalConf(GlobalConfKeys.nodeAgentEnableConfigureServer)
            ? nodeUniverseManager.maybeGetNodeAgent(
                getUniverse(), nodeDetails, true /*check feature flag*/)
            : Optional.empty();
    taskParams().skipDownloadSoftware = optional.isPresent();
    if (optional.isPresent() && taskParams().type == UpgradeTaskType.GFlags) {
      log.info("Updating gflags using node agent {}", optional.get());
      runServerGFlagsWithNodeAgent(optional.get(), universe, nodeDetails);
      return;
    }
    // Execute the ansible command.
    ShellResponse response =
        getNodeManager()
            .nodeCommand(NodeManager.NodeCommandType.Configure, taskParams())
            .processErrors();
    if (optional.isPresent()
        && (taskParams().type == UpgradeTaskType.Everything
            || taskParams().type == UpgradeTaskType.Software
            || taskParams().type == UpgradeTaskType.YbcGFlags)) {
      log.info("Installing software using node agent {}", optional.get());
      nodeAgentClient.runInstallSoftware(
          optional.get(),
          setupInstallSoftwareBits(universe, nodeDetails, taskParams(), optional.get()),
          "yugabyte");
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

  private void runServerGFlagsWithNodeAgent(
      NodeAgent nodeAgent, Universe universe, NodeDetails nodeDetails) {
    String processType = taskParams().getProperty("processType");
    if (!processType.equals(ServerType.CONTROLLER.toString())
        && !processType.equals(ServerType.MASTER.toString())
        && !processType.equals(ServerType.TSERVER.toString())) {
      throw new RuntimeException("Invalid processType: " + processType);
    }
    String serverName = processType.toLowerCase();
    String serverHome =
        Paths.get(nodeUniverseManager.getYbHomeDir(nodeDetails, universe), serverName).toString();
    boolean useHostname =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.useHostname
            || !Util.isIpAddress(nodeDetails.cloudInfo.private_ip);
    UserIntent userIntent = getNodeManager().getUserIntentFromParams(universe, taskParams());
    ServerGFlagsInput.Builder builder =
        ServerGFlagsInput.newBuilder()
            .setServerHome(serverHome)
            .setServerName(serverHome)
            .setServerName(serverName);
    Map<String, String> gflags =
        new HashMap<>(
            GFlagsUtil.getAllDefaultGFlags(
                taskParams(), universe, userIntent, useHostname, config, confGetter));
    if (processType.equals(ServerType.CONTROLLER.toString())) {
      // TODO Is the check taskParam.isEnableYbc() required here?
      Map<String, String> ybcFlags =
          GFlagsUtil.getYbcFlags(
              universe, taskParams(), confGetter, config, taskParams().ybcGflags);
      // Override for existing keys as this has higher precedence.
      gflags.putAll(ybcFlags);
    } else if (processType.equals(ServerType.MASTER.toString())
        || processType.equals(ServerType.TSERVER.toString())) {
      // Override for existing keys as this has higher precedence.
      gflags.putAll(taskParams().gflags);
      getNodeManager()
          .processGFlags(config, universe, nodeDetails, taskParams(), gflags, useHostname);
      if (!config.getBoolean("yb.cloud.enabled")) {
        if (gflags.containsKey(GFlagsUtil.YSQL_HBA_CONF_CSV)) {
          String hbaConfValue = gflags.get(GFlagsUtil.YSQL_HBA_CONF_CSV);
          if (hbaConfValue.contains(GFlagsUtil.JWT_AUTH)) {
            Path tmpDirectoryPath =
                FileUtils.getOrCreateTmpDirectory(
                    confGetter.getGlobalConf(GlobalConfKeys.ybTmpDirectoryPath));
            Path localGflagFilePath =
                tmpDirectoryPath.resolve(nodeDetails.getNodeUuid().toString());
            String providerUUID = userIntent.provider;
            String ybHomeDir = GFlagsUtil.getYbHomeDir(providerUUID);
            String remoteGFlagPath = ybHomeDir + GFlagsUtil.GFLAG_REMOTE_FILES_PATH;
            nodeAgentClient.uploadFile(nodeAgent, localGflagFilePath.toString(), remoteGFlagPath);
          }
        }
      }
      if (taskParams().resetMasterState) {
        builder.setResetMasterState(true);
      }
    }
    ServerGFlagsInput input = builder.putAllGflags(gflags).build();
    log.debug("Setting gflags using node agent: {}", input.getGflagsMap());
    nodeAgentClient.runServerGFlags(nodeAgent, input, "yugabyte");
  }

  @Override
  public int getRetryLimit() {
    return 2;
  }
}
