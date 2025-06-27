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
import com.yugabyte.yw.forms.UpgradeTaskParams;
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
import com.yugabyte.yw.nodeagent.DownloadSoftwareInput;
import com.yugabyte.yw.nodeagent.InstallOtelCollectorInput;
import com.yugabyte.yw.nodeagent.InstallSoftwareInput;
import com.yugabyte.yw.nodeagent.InstallYbcInput;
import com.yugabyte.yw.nodeagent.ServerGFlagsInput;
import com.yugabyte.yw.nodeagent.SetupCGroupInput;
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

  private static Map<String, String> filterCertsAndTlsGFlags(
      AnsibleConfigureServers.Params taskParam, Universe universe, List<String> flags) {
    Map<String, String> result =
        new HashMap<>(GFlagsUtil.getCertsAndTlsGFlags(taskParam, universe));
    result.keySet().retainAll(flags);
    return result;
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

  private DownloadSoftwareInput.Builder fillYbReleaseMetadata(
      Universe universe,
      Provider provider,
      NodeDetails node,
      String ybSoftwareVersion,
      Region region,
      Architecture arch,
      DownloadSoftwareInput.Builder downloadSoftwareInputBuilder,
      NodeAgent nodeAgent,
      String customTmpDirectory) {
    Map<String, String> envConfig = CloudInfoInterface.fetchEnvVars(provider);
    ReleaseContainer release = releaseManager.getReleaseByVersion(ybSoftwareVersion);
    String ybServerPackage = getYbPackage(release, arch, region);
    downloadSoftwareInputBuilder.setYbPackage(ybServerPackage);
    if (release.isS3Download(ybServerPackage)) {
      downloadSoftwareInputBuilder.setS3RemoteDownload(true);
      String accessKey = envConfig.get("AWS_ACCESS_KEY_ID");
      if (StringUtils.isEmpty(accessKey)) {
        // TODO: This will be removed once iTest moves to new release API.
        accessKey = release.getAwsAccessKey(arch);
      }
      if (StringUtils.isEmpty(accessKey)) {
        accessKey = System.getenv("AWS_ACCESS_KEY_ID");
      }
      if (StringUtils.isNotBlank(accessKey)) {
        downloadSoftwareInputBuilder.setAwsAccessKey(accessKey);
      }
      String secretKey = envConfig.get("AWS_SECRET_ACCESS_KEY");
      if (StringUtils.isEmpty(secretKey)) {
        secretKey = release.getAwsSecretKey(arch);
      }
      if (StringUtils.isEmpty(secretKey)) {
        secretKey = System.getenv("AWS_SECRET_ACCESS_KEY");
      }
      if (StringUtils.isNotBlank(secretKey)) {
        downloadSoftwareInputBuilder.setAwsSecretKey(secretKey);
      }
    } else if (release.isGcsDownload(ybServerPackage)) {
      downloadSoftwareInputBuilder.setGcsRemoteDownload(true);
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
      downloadSoftwareInputBuilder.setGcsCredentialsJson(
          customTmpDirectory
              + "/"
              + Paths.get(envConfig.get(GCPCloudImpl.GCE_PROJECT_PROPERTY))
                  .getFileName()
                  .toString());
    } else if (release.isHttpDownload(ybServerPackage)) {
      downloadSoftwareInputBuilder.setHttpRemoteDownload(true);
      if (StringUtils.isNotBlank(release.getHttpChecksum())) {
        downloadSoftwareInputBuilder.setHttpPackageChecksum(
            release.getHttpChecksum().toLowerCase());
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
      downloadSoftwareInputBuilder.setYbPackage(
          customTmpDirectory + "/" + Paths.get(ybServerPackage).getFileName().toString());
    }
    downloadSoftwareInputBuilder.setRemoteTmp(customTmpDirectory);
    downloadSoftwareInputBuilder.setYbHomeDir(provider.getYbHome());
    int numReleasesToKeep =
        confGetter.getConfForScope(universe, UniverseConfKeys.ybNumReleasesToKeepDefault);
    if (!appConfig.getBoolean("yb.cloud.enabled")) {
      numReleasesToKeep =
          confGetter.getConfForScope(universe, UniverseConfKeys.ybNumReleasesToKeepCloud);
    }
    downloadSoftwareInputBuilder.setNumReleasesToKeep(numReleasesToKeep);
    return downloadSoftwareInputBuilder;
  }

  private Map<String, String> populateTLSRotateFlags(
      Universe universe,
      AnsibleConfigureServers.Params taskParams,
      String taskSubType,
      Map<String, String> gflags) {
    // Populate gFlags based on the rotation round.
    final List<String> tlsGflagsToReplace =
        Arrays.asList(
            GFlagsUtil.USE_NODE_TO_NODE_ENCRYPTION,
            GFlagsUtil.USE_CLIENT_TO_SERVER_ENCRYPTION,
            GFlagsUtil.ALLOW_INSECURE_CONNECTIONS,
            GFlagsUtil.CERTS_DIR,
            GFlagsUtil.CERTS_FOR_CLIENT_DIR);
    if (UpgradeTaskParams.UpgradeTaskSubType.Round1GFlagsUpdate.name().equals(taskSubType)) {
      if (taskParams.nodeToNodeChange > 0) {
        gflags.putAll(filterCertsAndTlsGFlags(taskParams, universe, tlsGflagsToReplace));
        gflags.put(GFlagsUtil.ALLOW_INSECURE_CONNECTIONS, "true");
      } else if (taskParams.nodeToNodeChange < 0) {
        gflags.put(GFlagsUtil.ALLOW_INSECURE_CONNECTIONS, "true");
      } else {
        gflags.putAll(filterCertsAndTlsGFlags(taskParams, universe, tlsGflagsToReplace));
      }
    } else if (UpgradeTaskParams.UpgradeTaskSubType.Round2GFlagsUpdate.name().equals(taskSubType)) {
      if (taskParams.nodeToNodeChange > 0) {
        gflags.putAll(
            filterCertsAndTlsGFlags(
                taskParams,
                universe,
                Collections.singletonList(GFlagsUtil.ALLOW_INSECURE_CONNECTIONS)));
      } else if (taskParams.nodeToNodeChange < 0) {
        gflags.putAll(filterCertsAndTlsGFlags(taskParams, universe, tlsGflagsToReplace));
      } else {
        log.warn("Round2 upgrade not required when there is no change in node-to-node");
      }
    }
    return gflags;
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
    ReleaseContainer release = releaseManager.getReleaseByVersion(ybSoftwareVersion);
    String ybServerPackage =
        getYbPackage(release, universe.getUniverseDetails().arch, taskParams.getRegion());
    installSoftwareInputBuilder.setYbPackage(ybServerPackage);
    if (!nodeDetails.isInPlacement(universe.getUniverseDetails().getPrimaryCluster().uuid)) {
      // For RR we don't setup master
      installSoftwareInputBuilder.addSymLinkFolders("tserver");
    } else {
      installSoftwareInputBuilder.addSymLinkFolders("tserver");
      installSoftwareInputBuilder.addSymLinkFolders("master");
    }
    installSoftwareInputBuilder.setRemoteTmp(customTmpDirectory);
    installSoftwareInputBuilder.setYbHomeDir(provider.getYbHome());

    return installSoftwareInputBuilder.build();
  }

  public DownloadSoftwareInput setupDownloadSoftwareBits(
      Universe universe, NodeDetails nodeDetails, NodeTaskParams taskParams, NodeAgent nodeAgent) {
    DownloadSoftwareInput.Builder downloadSoftwareInputBuilder = DownloadSoftwareInput.newBuilder();
    String ybSoftwareVersion = "";
    if (taskParams instanceof AnsibleConfigureServers.Params) {
      AnsibleConfigureServers.Params params = (AnsibleConfigureServers.Params) taskParams;
      ybSoftwareVersion = params.ybSoftwareVersion;
    }
    Cluster cluster = universe.getCluster(nodeDetails.placementUuid);
    Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
    String customTmpDirectory =
        confGetter.getConfForScope(provider, ProviderConfKeys.remoteTmpDirectory);
    downloadSoftwareInputBuilder =
        fillYbReleaseMetadata(
            universe,
            provider,
            nodeDetails,
            ybSoftwareVersion,
            taskParams.getRegion(),
            universe.getUniverseDetails().arch,
            downloadSoftwareInputBuilder,
            nodeAgent,
            customTmpDirectory);
    return downloadSoftwareInputBuilder.build();
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
    String taskSubType = taskParams.getProperty("taskSubType");
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
    gflags = populateTLSRotateFlags(universe, taskParams, taskSubType, gflags);
    ServerGFlagsInput input = builder.putAllGflags(gflags).build();
    log.debug("Setting gflags using node agent: {}", input.getGflagsMap());
    nodeAgentClient.runServerGFlags(nodeAgent, input, DEFAULT_CONFIGURE_USER);
  }

  public SetupCGroupInput setupSetupCGroupBits(
      Universe universe, NodeDetails nodeDetails, NodeTaskParams taskParams, NodeAgent nodeAgent) {
    SetupCGroupInput.Builder setupSetupCGroupBuilder = SetupCGroupInput.newBuilder();
    Cluster cluster = universe.getCluster(nodeDetails.placementUuid);
    Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));

    setupSetupCGroupBuilder.setYbHomeDir(provider.getYbHome());
    if (taskParams instanceof AnsibleConfigureServers.Params) {
      AnsibleConfigureServers.Params params = (AnsibleConfigureServers.Params) taskParams;
      setupSetupCGroupBuilder.setPgMaxMemMb(params.cgroupSize);
    }

    return setupSetupCGroupBuilder.build();
  }
}
