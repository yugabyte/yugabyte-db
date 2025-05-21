// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.payload;

import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.cloud.gcp.GCPCloudImpl;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.ManageOtelCollector;
import com.yugabyte.yw.common.FileHelperService;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.audit.otel.OtelCollectorConfigGenerator;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import com.yugabyte.yw.models.helpers.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.audit.UniverseLogsExporterConfig;
import com.yugabyte.yw.models.helpers.audit.YCQLAuditConfig;
import com.yugabyte.yw.models.helpers.telemetry.AWSCloudWatchConfig;
import com.yugabyte.yw.models.helpers.telemetry.GCPCloudMonitoringConfig;
import com.yugabyte.yw.nodeagent.ConfigureServerInput;
import com.yugabyte.yw.nodeagent.InstallOtelCollectorInput;
import com.yugabyte.yw.nodeagent.InstallSoftwareInput;
import com.yugabyte.yw.nodeagent.InstallYbcInput;
import com.yugabyte.yw.nodeagent.ServerGFlagsInput;
import java.io.File;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class NodeAgentRpcPayload {
  public static final String DEFAULT_CONFIGURE_USER = "yugabyte";
  private final ReleaseManager releaseManager;
  private final Config appConfig;
  private final OtelCollectorConfigGenerator otelCollectorConfigGenerator;
  private final TelemetryProviderService telemetryProviderService;
  private final FileHelperService fileHelperService;
  private final RuntimeConfGetter confGetter;
  private final NodeAgentClient nodeAgentClient;
  private final NodeUniverseManager nodeUniverseManager;
  private final NodeManager nodeManager;

  @Inject
  public NodeAgentRpcPayload(
      ReleaseManager releaseManager,
      Config appConfig,
      RuntimeConfGetter confGetter,
      OtelCollectorConfigGenerator otelCollectorConfigGenerator,
      TelemetryProviderService telemetryProviderService,
      FileHelperService fileHelperService,
      NodeAgentClient nodeAgentClient,
      NodeUniverseManager nodeUniverseManager,
      NodeManager nodeManager) {
    this.releaseManager = releaseManager;
    this.appConfig = appConfig;
    this.otelCollectorConfigGenerator = otelCollectorConfigGenerator;
    this.telemetryProviderService = telemetryProviderService;
    this.fileHelperService = fileHelperService;
    this.confGetter = confGetter;
    this.nodeAgentClient = nodeAgentClient;
    this.nodeUniverseManager = nodeUniverseManager;
    this.nodeManager = nodeManager;
  }

  private List<String> getMountPoints(NodeTaskParams params) {
    if (StringUtils.isNotBlank(params.deviceInfo.mountPoints)) {
      return Arrays.stream(params.deviceInfo.mountPoints.split("\\s*,\\s*"))
          .map(String::trim)
          .filter(s -> !s.isEmpty())
          .collect(Collectors.toList());
    } else if (params.deviceInfo.numVolumes != null
        && params.getProvider().getCloudCode() != Common.CloudType.onprem) {
      List<String> mountPoints = new ArrayList<>();
      for (int i = 0; i < params.deviceInfo.numVolumes; i++) {
        mountPoints.add("/mnt/d" + i);
      }
      return mountPoints;
    }
    return Collections.emptyList();
  }

  private String getYbPackage(ReleaseContainer release, Architecture arch, Region region) {
    String ybServerPackage = null;
    if (release != null) {
      if (arch != null) {
        ybServerPackage = release.getFilePath(arch);
      } else {
        ybServerPackage = release.getFilePath(region);
      }
    }

    return ybServerPackage;
  }

  private String getThirdpartyPackagePath() {
    String packagePath = appConfig.getString("yb.thirdparty.packagePath");
    if (packagePath != null && !packagePath.isEmpty()) {
      File thirdpartyPackagePath = new File(packagePath);
      if (thirdpartyPackagePath.exists() && thirdpartyPackagePath.isDirectory()) {
        return packagePath;
      }
    }

    return null;
  }

  private String getOtelCollectorPackagePath(Architecture arch) {
    String architecture = "";
    if (arch.equals(Architecture.x86_64)) {
      architecture = "amd64";
    } else {
      architecture = "arm64";
    }
    return String.format(
        "otelcol-contrib_%s_%s_%s.tar.gz",
        ManageOtelCollector.OtelCollectorVersion,
        ManageOtelCollector.OtelCollectorPlatform,
        architecture);
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
    ReleaseContainer release = releaseManager.getReleaseByVersion(ybSoftwareVersion);
    String ybServerPackage = getYbPackage(release, arch, region);
    installSoftwareInputBuilder.setYbPackage(ybServerPackage);
    if (release.isS3Download(ybServerPackage)) {
      installSoftwareInputBuilder.setS3RemoteDownload(true);
      String accessKey = envConfig.get("AWS_ACCESS_KEY_ID");
      if (StringUtils.isEmpty(accessKey)) {
        // TODO: This will be removed once iTest moves to new release API.
        accessKey = release.getAwsAccessKey(arch);
      }
      if (StringUtils.isEmpty(accessKey)) {
        accessKey = System.getenv("AWS_ACCESS_KEY_ID");
      }
      if (StringUtils.isNotBlank(accessKey)) {
        installSoftwareInputBuilder.setAwsAccessKey(accessKey);
      }
      String secretKey = envConfig.get("AWS_SECRET_ACCESS_KEY");
      if (StringUtils.isEmpty(secretKey)) {
        secretKey = release.getAwsSecretKey(arch);
      }
      if (StringUtils.isEmpty(secretKey)) {
        secretKey = System.getenv("AWS_SECRET_ACCESS_KEY");
      }
      if (StringUtils.isNotBlank(secretKey)) {
        installSoftwareInputBuilder.setAwsSecretKey(secretKey);
      }
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
          DEFAULT_CONFIGURE_USER,
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
          DEFAULT_CONFIGURE_USER,
          0,
          null);
      installSoftwareInputBuilder.setYbPackage(
          customTmpDirectory + "/" + Paths.get(ybServerPackage).getFileName().toString());
    }
    if (!node.isInPlacement(universe.getUniverseDetails().getPrimaryCluster().uuid)) {
      // For RR we don't setup master
      installSoftwareInputBuilder.addSymLinkFolders("tserver");
    } else {
      installSoftwareInputBuilder.addSymLinkFolders("tserver");
      installSoftwareInputBuilder.addSymLinkFolders("master");
    }
    installSoftwareInputBuilder.setRemoteTmp(customTmpDirectory);
    installSoftwareInputBuilder.setYbHomeDir(provider.getYbHome());
    return installSoftwareInputBuilder;
  }

  public InstallSoftwareInput setupInstallSoftwareBits(
      Universe universe, NodeDetails nodeDetails, NodeTaskParams taskParams, NodeAgent nodeAgent) {
    InstallSoftwareInput.Builder installSoftwareInputBuilder = InstallSoftwareInput.newBuilder();
    String ybSoftwareVersion = "";
    if (taskParams instanceof AnsibleConfigureServers.Params) {
      AnsibleConfigureServers.Params params = (AnsibleConfigureServers.Params) taskParams;
      ybSoftwareVersion = params.ybSoftwareVersion;
    }
    Cluster cluster = universe.getCluster(nodeDetails.placementUuid);
    Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
    String customTmpDirectory =
        confGetter.getConfForScope(provider, ProviderConfKeys.remoteTmpDirectory);
    installSoftwareInputBuilder =
        fillYbReleaseMetadata(
            universe,
            provider,
            nodeDetails,
            ybSoftwareVersion,
            taskParams.getRegion(),
            universe.getUniverseDetails().arch,
            installSoftwareInputBuilder,
            nodeAgent,
            customTmpDirectory);

    return installSoftwareInputBuilder.build();
  }

  public InstallYbcInput setupInstallYbcSoftwareBits(
      Universe universe, NodeDetails nodeDetails, NodeTaskParams taskParams, NodeAgent nodeAgent) {
    InstallYbcInput.Builder installYbcInputBuilder = InstallYbcInput.newBuilder();
    String ybSoftwareVersion = "";
    if (taskParams instanceof AnsibleConfigureServers.Params) {
      AnsibleConfigureServers.Params params = (AnsibleConfigureServers.Params) taskParams;
      ybSoftwareVersion = params.ybSoftwareVersion;
    }
    ReleaseContainer release = releaseManager.getReleaseByVersion(ybSoftwareVersion);
    String ybServerPackage =
        getYbPackage(release, universe.getUniverseDetails().arch, taskParams.getRegion());
    Cluster cluster = universe.getCluster(nodeDetails.placementUuid);
    Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
    String customTmpDirectory =
        confGetter.getConfForScope(provider, ProviderConfKeys.remoteTmpDirectory);
    String ybcPackage = null;
    Pair<String, String> ybcPackageDetails =
        Util.getYbcPackageDetailsFromYbServerPackage(ybServerPackage);
    String stableYbc = confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion);
    ReleaseManager.ReleaseMetadata releaseMetadata =
        releaseManager.getYbcReleaseByVersion(
            stableYbc, ybcPackageDetails.getFirst(), ybcPackageDetails.getSecond());
    if (releaseMetadata == null) {
      throw new RuntimeException(
          String.format("Ybc package metadata: %s cannot be empty with ybc enabled", stableYbc));
    }
    if (universe.getUniverseDetails().arch != null) {
      ybcPackage = releaseMetadata.getFilePath(universe.getUniverseDetails().arch);
    } else {
      // Fallback to region in case arch is not present
      ybcPackage = releaseMetadata.getFilePath(taskParams.getRegion());
    }
    if (StringUtils.isBlank(ybcPackage)) {
      throw new RuntimeException("Ybc package cannot be empty with ybc enabled");
    }
    installYbcInputBuilder.setYbcPackage(ybcPackage);
    nodeAgentClient.uploadFile(
        nodeAgent,
        ybcPackage,
        customTmpDirectory + "/" + Paths.get(ybcPackage).getFileName().toString(),
        DEFAULT_CONFIGURE_USER,
        0,
        null);
    installYbcInputBuilder.setRemoteTmp(customTmpDirectory);
    installYbcInputBuilder.setYbHomeDir(provider.getYbHome());
    installYbcInputBuilder.addAllMountPoints(getMountPoints(taskParams));
    return installYbcInputBuilder.build();
  }

  public ConfigureServerInput setUpConfigureServerBits(
      Universe universe, NodeDetails nodeDetails, NodeTaskParams taskParams, NodeAgent nodeAgent) {
    ConfigureServerInput.Builder configureServerInputBuilder = ConfigureServerInput.newBuilder();
    Cluster cluster = universe.getCluster(nodeDetails.placementUuid);
    Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
    String customTmpDirectory =
        confGetter.getConfForScope(provider, ProviderConfKeys.remoteTmpDirectory);

    configureServerInputBuilder.setRemoteTmp(customTmpDirectory);
    configureServerInputBuilder.setYbHomeDir(provider.getYbHome());
    configureServerInputBuilder.addAllMountPoints(getMountPoints(taskParams));
    if (!nodeDetails.isInPlacement(universe.getUniverseDetails().getPrimaryCluster().uuid)) {
      // For RR we don't setup master
      configureServerInputBuilder.addProcesses("tserver");
    } else {
      // For dedicated nodes, both are set up.
      configureServerInputBuilder.addProcesses("master");
      configureServerInputBuilder.addProcesses("tserver");
    }

    Integer num_cores_to_keep =
        confGetter.getConfForScope(universe, UniverseConfKeys.numCoresToKeep);
    configureServerInputBuilder.setNumCoresToKeep(num_cores_to_keep);
    return configureServerInputBuilder.build();
  }

  public InstallOtelCollectorInput setupInstallOtelCollectorBits(
      Universe universe, NodeDetails nodeDetails, NodeTaskParams taskParams, NodeAgent nodeAgent) {
    InstallOtelCollectorInput.Builder installOtelCollectorInputBuilder =
        InstallOtelCollectorInput.newBuilder();
    Cluster cluster = universe.getCluster(nodeDetails.placementUuid);
    Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
    String customTmpDirectory =
        confGetter.getConfForScope(provider, ProviderConfKeys.remoteTmpDirectory);
    AuditLogConfig config = null;
    if (taskParams instanceof ManageOtelCollector.Params) {
      ManageOtelCollector.Params params = (ManageOtelCollector.Params) taskParams;
      config = params.auditLogConfig;
    } else if (taskParams instanceof AnsibleConfigureServers.Params) {
      AnsibleConfigureServers.Params params = (AnsibleConfigureServers.Params) taskParams;
      config = params.auditLogConfig;
    }
    Map<String, String> gflags =
        GFlagsUtil.getGFlagsForAZ(
            taskParams.azUuid,
            UniverseTaskBase.ServerType.TSERVER,
            cluster,
            universe.getUniverseDetails().clusters);

    installOtelCollectorInputBuilder.setRemoteTmp(customTmpDirectory);
    installOtelCollectorInputBuilder.setYbHomeDir(provider.getYbHome());
    String otelCollectorPackagePath =
        getThirdpartyPackagePath()
            + "/"
            + getOtelCollectorPackagePath(universe.getUniverseDetails().arch);
    nodeAgentClient.uploadFile(
        nodeAgent,
        otelCollectorPackagePath,
        customTmpDirectory + "/" + getOtelCollectorPackagePath(universe.getUniverseDetails().arch),
        DEFAULT_CONFIGURE_USER,
        0,
        null);
    installOtelCollectorInputBuilder.setOtelColPackagePath(
        getOtelCollectorPackagePath(universe.getUniverseDetails().arch));
    String ycqlAuditLogLevel = "NONE";
    if (config.getYcqlAuditConfig() != null) {
      YCQLAuditConfig.YCQLAuditLogLevel logLevel =
          config.getYcqlAuditConfig().getLogLevel() != null
              ? config.getYcqlAuditConfig().getLogLevel()
              : YCQLAuditConfig.YCQLAuditLogLevel.ERROR;
      ycqlAuditLogLevel = logLevel.name();
    }
    installOtelCollectorInputBuilder.setYcqlAuditLogLevel(ycqlAuditLogLevel);
    installOtelCollectorInputBuilder.addAllMountPoints(getMountPoints(taskParams));

    if (config.isExportActive()
        && CollectionUtils.isNotEmpty(config.getUniverseLogsExporterConfig())) {
      String otelCollectorConfigFile =
          otelCollectorConfigGenerator
              .generateConfigFile(
                  taskParams,
                  provider,
                  universe.getUniverseDetails().getPrimaryCluster().userIntent,
                  config,
                  GFlagsUtil.getLogLinePrefix(gflags.get(GFlagsUtil.YSQL_PG_CONF_CSV)),
                  NodeManager.getOtelColMetricsPort(taskParams))
              .toAbsolutePath()
              .toString();
      nodeAgentClient.uploadFile(
          nodeAgent,
          otelCollectorConfigFile,
          customTmpDirectory + "/" + Paths.get(otelCollectorConfigFile).getFileName().toString(),
          DEFAULT_CONFIGURE_USER,
          0,
          null);
      installOtelCollectorInputBuilder.setOtelColConfigFile(
          customTmpDirectory + "/" + Paths.get(otelCollectorConfigFile).getFileName().toString());

      for (UniverseLogsExporterConfig logsExporterConfig : config.getUniverseLogsExporterConfig()) {
        TelemetryProvider telemetryProvider =
            telemetryProviderService.get(logsExporterConfig.getExporterUuid());
        switch (telemetryProvider.getConfig().getType()) {
          case AWS_CLOUDWATCH -> {
            AWSCloudWatchConfig awsCloudWatchConfig =
                (AWSCloudWatchConfig) telemetryProvider.getConfig();
            if (StringUtils.isNotEmpty(awsCloudWatchConfig.getAccessKey())) {
              installOtelCollectorInputBuilder.setOtelColAwsAccessKey(
                  awsCloudWatchConfig.getAccessKey());
            }
            if (StringUtils.isNotEmpty(awsCloudWatchConfig.getSecretKey())) {
              installOtelCollectorInputBuilder.setOtelColAwsSecretKey(
                  awsCloudWatchConfig.getSecretKey());
            }
          }
          case GCP_CLOUD_MONITORING -> {
            GCPCloudMonitoringConfig gcpCloudMonitoringConfig =
                (GCPCloudMonitoringConfig) telemetryProvider.getConfig();
            if (gcpCloudMonitoringConfig.getCredentials() != null) {
              Path path =
                  fileHelperService.createTempFile(
                      "otel_collector_gcp_creds_"
                          + taskParams.getUniverseUUID()
                          + "_"
                          + taskParams.nodeUuid,
                      ".json");
              String filePath = path.toAbsolutePath().toString();
              FileUtils.writeJsonFile(filePath, gcpCloudMonitoringConfig.getCredentials());
              nodeAgentClient.uploadFile(
                  nodeAgent,
                  filePath,
                  customTmpDirectory + "/" + Paths.get(filePath).getFileName().toString(),
                  DEFAULT_CONFIGURE_USER,
                  0,
                  null);
              installOtelCollectorInputBuilder.setOtelColGcpCredsFile(
                  customTmpDirectory + "/" + Paths.get(filePath).getFileName().toString());
            }
          }
        }
      }
    }

    return installOtelCollectorInputBuilder.build();
  }

  public void runServerGFlagsWithNodeAgent(
      NodeAgent nodeAgent, Universe universe, NodeDetails nodeDetails, NodeTaskParams taskParams) {
    String processType = taskParams.getProperty("processType");
    if (!processType.equals(ServerType.CONTROLLER.toString())
        && !processType.equals(ServerType.MASTER.toString())
        && !processType.equals(ServerType.TSERVER.toString())) {
      throw new RuntimeException("Invalid processType: " + processType);
    }
    runServerGFlagsWithNodeAgent(nodeAgent, universe, nodeDetails, processType, taskParams);
  }

  public void runServerGFlagsWithNodeAgent(
      NodeAgent nodeAgent,
      Universe universe,
      NodeDetails nodeDetails,
      String processType,
      NodeTaskParams nodeTaskParams) {
    String serverName = processType.toLowerCase();
    String serverHome =
        Paths.get(nodeUniverseManager.getYbHomeDir(nodeDetails, universe), serverName).toString();
    boolean useHostname =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.useHostname
            || !Util.isIpAddress(nodeDetails.cloudInfo.private_ip);
    AnsibleConfigureServers.Params taskParams = null;
    if (nodeTaskParams instanceof AnsibleConfigureServers.Params) {
      taskParams = (AnsibleConfigureServers.Params) nodeTaskParams;
    }
    UserIntent userIntent = nodeManager.getUserIntentFromParams(universe, taskParams);
    ServerGFlagsInput.Builder builder =
        ServerGFlagsInput.newBuilder()
            .setServerHome(serverHome)
            .setServerName(serverHome)
            .setServerName(serverName);
    Map<String, String> gflags =
        new HashMap<>(
            GFlagsUtil.getAllDefaultGFlags(
                taskParams, universe, userIntent, useHostname, appConfig, confGetter));
    if (processType.equals(ServerType.CONTROLLER.toString())) {
      // TODO Is the check taskParam.isEnableYbc() required here?
      Map<String, String> ybcFlags =
          GFlagsUtil.getYbcFlags(universe, taskParams, confGetter, appConfig, taskParams.ybcGflags);
      // Override for existing keys as this has higher precedence.
      gflags.putAll(ybcFlags);
    } else if (processType.equals(ServerType.MASTER.toString())
        || processType.equals(ServerType.TSERVER.toString())) {
      // Override for existing keys as this has higher precedence.
      gflags.putAll(taskParams.gflags);
      nodeManager.processGFlags(appConfig, universe, nodeDetails, taskParams, gflags, useHostname);
      if (!appConfig.getBoolean("yb.cloud.enabled")) {
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
      if (taskParams.resetMasterState) {
        builder.setResetMasterState(true);
      }
    }
    ServerGFlagsInput input = builder.putAllGflags(gflags).build();
    log.debug("Setting gflags using node agent: {}", input.getGflagsMap());
    nodeAgentClient.runServerGFlags(nodeAgent, input, DEFAULT_CONFIGURE_USER);
  }
}
