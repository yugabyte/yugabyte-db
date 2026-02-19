// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.commissioner.tasks.payload.NodeAgentRpcPayload;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.ServerSubTaskBase;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.LocalNodeManager;
import com.yugabyte.yw.common.NodeManager.NodeCommandType;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class PGUpgradeTServerCheck extends ServerSubTaskBase {

  private final KubernetesManagerFactory kubernetesManagerFactory;
  private final LocalNodeManager localNodeManager;
  private final AuditService auditService;
  private final NodeAgentRpcPayload nodeAgentRpcPayload;

  private static final String PG_UPGRADE_CHECK_LOG_FILE = "pg_upgrade_check.log";

  public static class Params extends ServerSubTaskParams {
    public String ybSoftwareVersion;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Inject
  protected PGUpgradeTServerCheck(
      BaseTaskDependencies baseTaskDependencies,
      KubernetesManagerFactory kubernetesManagerFactory,
      LocalNodeManager localNodeManager,
      AuditService auditService,
      NodeAgentRpcPayload nodeAgentRpcPayload) {
    super(baseTaskDependencies);
    this.kubernetesManagerFactory = kubernetesManagerFactory;
    this.localNodeManager = localNodeManager;
    this.auditService = auditService;
    this.nodeAgentRpcPayload = nodeAgentRpcPayload;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    boolean isK8sUniverse =
        universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .providerType
            .equals(CloudType.kubernetes);
    boolean isDedicatedNodeUniverse =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.dedicatedNodes;
    // For K8s and dedicated node universe, we can run the check on any node in the primary cluster.
    // For other universe types, we run the check on the master leader node as pg_catalog is present
    // on the master leader node.
    NodeDetails node =
        isK8sUniverse || isDedicatedNodeUniverse
            ? universe.getLiveTServersInPrimaryCluster().stream().findAny().get()
            : universe.getMasterLeaderNode();

    if (node == null && !isK8sUniverse && !isDedicatedNodeUniverse) {
      node = universe.getLiveTServersInPrimaryCluster().stream().findAny().get();
    }

    try {
      // Clean up the downloaded package from the node.
      cleanUpDownloadedPackage(universe, node, isK8sUniverse);
      // Download the package to the node.
      downloadPackage(universe, node, isK8sUniverse);
      // Run check on the node
      if (isK8sUniverse) {
        runCheckOnPod(universe, node);
      } else {
        runCheckOnNode(universe, node);
      }
    } finally {
      // Clean up the downloaded package from the node.
      cleanUpDownloadedPackage(universe, node, isK8sUniverse);
    }
  }

  private void cleanUpDownloadedPackage(
      Universe universe, NodeDetails node, boolean isK8sUniverse) {
    ReleaseContainer release = releaseManager.getReleaseByVersion(taskParams().ybSoftwareVersion);
    if (isK8sUniverse) {
      String ybServerPackage = release.getFilePath(getArchitectureOnK8sPod(universe, node));
      String packageName = extractPackageName(ybServerPackage);
      String versionName = extractVersionName(ybServerPackage);
      Map<String, String> podConfig = KubernetesUtil.getKubernetesConfigPerPod(universe, node);
      String namespace = node.cloudInfo.kubernetesNamespace;
      String podName = node.cloudInfo.kubernetesPodName;
      String dataDirectory = Util.getDataDirectoryPath(universe, node, this.config);
      List<String> deletePackageCommand =
          ImmutableList.of(
              "/bin/bash",
              "-c",
              String.format(
                  "rm -rf %s %s;",
                  dataDirectory + "/yw-data/" + packageName,
                  dataDirectory + "/yw-data/" + versionName));
      kubernetesManagerFactory
          .getManager()
          .executeCommandInPodContainer(
              podConfig, namespace, podName, "yb-tserver", deletePackageCommand);
    } else {
      String ybServerPackage = release.getFilePath(universe.getUniverseDetails().arch);
      String packageName = extractPackageName(ybServerPackage);
      String versionName = extractVersionName(ybServerPackage);
      String ybSoftwareDir = nodeUniverseManager.getYbHomeDir(node, universe) + "/yb-software/";
      Provider provider =
          Provider.getOrBadRequest(
              UUID.fromString(
                  universe.getUniverseDetails().getPrimaryCluster().userIntent.provider));
      String customTmpDirectory =
          confGetter.getConfForScope(provider, ProviderConfKeys.remoteTmpDirectory);
      nodeUniverseManager
          .runCommand(
              node,
              universe,
              ImmutableList.of(
                  "rm",
                  "-rf",
                  ybSoftwareDir + packageName,
                  ybSoftwareDir + versionName,
                  customTmpDirectory + "/" + PG_UPGRADE_CHECK_LOG_FILE),
              ShellProcessContext.builder().logCmdOutput(true).build())
          .processErrors();
    }
  }

  private void downloadPackage(Universe universe, NodeDetails node, boolean isk8sUniverse) {
    ReleaseContainer release = releaseManager.getReleaseByVersion(taskParams().ybSoftwareVersion);
    if (isk8sUniverse) {
      Map<String, String> podConfig = KubernetesUtil.getKubernetesConfigPerPod(universe, node);
      String namespace = node.cloudInfo.kubernetesNamespace;
      String podName = node.cloudInfo.kubernetesPodName;
      Architecture arch = getArchitectureOnK8sPod(universe, node);
      String ybServerPackage = release.getFilePath(arch);
      String packageName = extractPackageName(ybServerPackage);
      String versionName = extractVersionName(ybServerPackage);
      String dataDirectory = Util.getDataDirectoryPath(universe, node, this.config);
      // Copy the package to the node in temp directory
      if (release.isHttpDownload(ybServerPackage)) {
        kubernetesManagerFactory
            .getManager()
            .executeCommandInPodContainer(
                podConfig,
                namespace,
                podName,
                "yb-tserver",
                ImmutableList.of(
                    "/bin/bash",
                    "-c",
                    "curl -o "
                        + dataDirectory
                        + "/yw-data/"
                        + packageName
                        + " "
                        + ybServerPackage));
      } else {
        kubernetesManagerFactory
            .getManager()
            .copyFileToPod(
                podConfig,
                namespace,
                podName,
                "yb-tserver",
                ybServerPackage,
                dataDirectory + "/yw-data");
      }
      // Extract the package
      String command =
          String.format(
              "mkdir -p %s; tar -xvzf %s -C %s --strip-components=1",
              dataDirectory + "/yw-data/" + versionName,
              dataDirectory + "/yw-data/" + packageName,
              dataDirectory + "/yw-data/" + versionName);
      List<String> extractPackageCommand = ImmutableList.of("/bin/bash", "-c", command);
      kubernetesManagerFactory
          .getManager()
          .executeCommandInPodContainer(
              podConfig, namespace, podName, "yb-tserver", extractPackageCommand);
    } else {
      Optional<NodeAgent> optional =
          confGetter.getGlobalConf(GlobalConfKeys.nodeAgentDisableConfigureServer)
              ? Optional.empty()
              : nodeUniverseManager.maybeGetNodeAgent(universe, node, true /*check feature flag*/);
      AnsibleConfigureServers.Params params =
          getAnsibleConfigureServerParamsToDownloadSoftware(
              universe, node, taskParams().ybSoftwareVersion);
      if (!optional.isPresent()) {
        nodeManager.nodeCommand(NodeCommandType.Configure, params).processErrors();
      } else {
        nodeAgentClient.runDownloadSoftware(
            optional.get(),
            nodeAgentRpcPayload.setupDownloadSoftwareBits(universe, node, params, optional.get()),
            NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
      }
    }
  }

  private void runCheckOnPod(Universe universe, NodeDetails node) {
    ReleaseContainer release = releaseManager.getReleaseByVersion(taskParams().ybSoftwareVersion);
    Architecture arch = getArchitectureOnK8sPod(universe, node);
    String ybServerPackage = release.getFilePath(arch);
    String versionName = extractVersionName(ybServerPackage);
    String tmpDirectory = nodeUniverseManager.getRemoteTmpDir(node, universe);
    Map<String, String> podConfig = KubernetesUtil.getKubernetesConfigPerPod(universe, node);
    String dataDirectory = Util.getDataDirectoryPath(universe, node, this.config);

    String namespace = node.cloudInfo.kubernetesNamespace;
    String podName = node.cloudInfo.kubernetesPodName;
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    String pgUpgradeBinaryLocation =
        String.format("%s/%s/postgres/bin/pg_upgrade", dataDirectory + "/yw-data", versionName);
    String oldHost =
        primaryCluster.userIntent.enableYSQLAuth
            ? "$(ls -d -t " + tmpDirectory + "/.yb.* | head -1)"
            : podName;
    String oldPort =
        primaryCluster.userIntent.enableConnectionPooling
            ? String.valueOf(node.internalYsqlServerRpcPort)
            : String.valueOf(node.ysqlServerRpcPort);

    String upgradeCheckCommand =
        String.format(
            "cd %s;  %s -d %s/pg_data --old-host %s --old-port %s --username yugabyte --check",
            dataDirectory, pgUpgradeBinaryLocation, dataDirectory, oldHost, oldPort);
    List<String> pgUpgradeCheckCommand = ImmutableList.of("/bin/bash", "-c", upgradeCheckCommand);
    kubernetesManagerFactory
        .getManager()
        .executeCommandInPodContainer(
            podConfig, namespace, podName, "yb-tserver", pgUpgradeCheckCommand);
  }

  private void runCheckOnNode(Universe universe, NodeDetails node) {
    boolean localProviderTest =
        universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .providerType
            .equals(CloudType.local);
    List<String> command = new ArrayList<>();
    Architecture arch = universe.getUniverseDetails().arch;
    ReleaseContainer release = releaseManager.getReleaseByVersion(taskParams().ybSoftwareVersion);
    String ybServerPackage = release.getFilePath(arch);
    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    String customTmpDirectory =
        confGetter.getConfForScope(provider, ProviderConfKeys.remoteTmpDirectory);
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    String pgUpgradeBinaryLocation =
        nodeUniverseManager.getYbHomeDir(node, universe)
            + "/yb-software/"
            + extractVersionName(ybServerPackage)
            + "/postgres/bin/pg_upgrade";
    if (localProviderTest) {
      pgUpgradeBinaryLocation =
          localNodeManager.getVersionBinPath(taskParams().ybSoftwareVersion).replace("/bin", "")
              + "/postgres/bin/pg_upgrade";
    }
    command.add(pgUpgradeBinaryLocation);
    command.add("-d");
    String pgDataDir = Util.getDataDirectoryPath(universe, node, config) + "/pg_data";
    if (localProviderTest) {
      pgDataDir =
          localNodeManager.getNodeFSRoot(
                  primaryCluster.userIntent, localNodeManager.getNodeInfo(node))
              + "/pg_data";
    }
    command.add(pgDataDir);
    command.add("--old-host");
    boolean authEnabled = GFlagsUtil.isYsqlAuthEnabled(universe, node);
    if (authEnabled) {
      command.add(String.format("'$(ls -d -t %s/.yb.* | head -1)'", customTmpDirectory));
    } else {
      command.add(node.cloudInfo.private_ip);
    }
    command.add("--old-port");
    if (primaryCluster.userIntent.enableConnectionPooling) {
      command.add(String.valueOf(node.internalYsqlServerRpcPort));
    } else {
      command.add(String.valueOf(node.ysqlServerRpcPort));
    }
    command.add("--username");
    command.add("\"yugabyte\"");
    command.add("--check");
    command.add(">");
    command.add(String.format("%s/%s", customTmpDirectory, PG_UPGRADE_CHECK_LOG_FILE));
    command.add("2>&1");

    ShellProcessContext context =
        ShellProcessContext.builder()
            .logCmdOutput(true)
            .timeoutSecs(
                confGetter.getConfForScope(universe, UniverseConfKeys.pgUpgradeCheckTimeoutSec))
            .build();

    log.info("Running PG15 upgrade check on node: {} with command: {}", node.nodeName, command);
    nodeUniverseManager.runCommand(node, universe, command, context);
    List<String> readLogsCommand = new ArrayList<>();
    readLogsCommand.add("cat");
    readLogsCommand.add(String.format("%s/%s", customTmpDirectory, PG_UPGRADE_CHECK_LOG_FILE));
    log.info(
        "Reading PG15 upgrade check logs on node: {} with command: {}", node.nodeName, command);
    ShellResponse readLogsResponse =
        nodeUniverseManager.runCommand(node, universe, readLogsCommand, context).processErrors();
    ObjectNode output = parsePGUpgradeOutput(readLogsResponse.extractRunCommandOutput());
    log.info("PG upgrade check output on node: {} is: {}", node.nodeName, output);
    appendAuditDetails(output);
    if (output != null
        && output.has("overallStatus")
        && output.get("overallStatus").asText().equals("Failure, exiting")) {
      log.info("PG upgrade check failed on node: {} with error: {}", node.nodeName, output);
      throw new RuntimeException(
          "PG15 upgrade check failed on node: " + node.nodeName + "with error:" + output);
    } else {
      log.info("PG upgrade check passed on node: {}", node.nodeName);
    }
  }

  private void appendAuditDetails(ObjectNode output) {
    Audit audit = auditService.getFromTaskUUID(getUserTaskUUID());
    if (audit == null) {
      return;
    }
    JsonNode auditDetails = audit.getAdditionalDetails();
    ObjectNode modifiedNode;
    if (auditDetails != null) {
      modifiedNode = auditDetails.deepCopy();
    } else {
      ObjectMapper mapper = new ObjectMapper();
      modifiedNode = mapper.createObjectNode();
    }
    modifiedNode.setAll(output);
    log.debug("Software upgrade task audit details: {}", modifiedNode);
    auditService.updateAdditionalDetails(getUserTaskUUID(), modifiedNode);
  }

  private String extractVersionName(String ybServerPackage) {
    return extractPackageName(ybServerPackage).replace(".tar.gz", "");
  }

  private String extractPackageName(String ybServerPackage) {
    String[] parts = ybServerPackage.split("/");
    return parts[parts.length - 1];
  }

  private Architecture getArchitectureOnK8sPod(Universe universe, NodeDetails node) {
    Map<String, String> podConfig = KubernetesUtil.getKubernetesConfigPerPod(universe, node);
    String architecture =
        kubernetesManagerFactory
            .getManager()
            .executeCommandInPodContainer(
                podConfig,
                node.cloudInfo.kubernetesNamespace,
                node.cloudInfo.kubernetesPodName,
                "yb-controller",
                Arrays.asList("uname", "-m"));
    return Architecture.valueOf(architecture);
  }

  private AnsibleConfigureServers.Params getAnsibleConfigureServerParamsToDownloadSoftware(
      Universe universe, NodeDetails node, String ybSoftwareVersion) {
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    params.setUniverseUUID(universe.getUniverseUUID());
    params.deviceInfo = userIntent.getDeviceInfoForNode(node);
    params.instanceType = node.cloudInfo.instance_type;
    params.nodeName = node.nodeName;
    params.azUuid = node.azUuid;
    params.placementUuid = node.placementUuid;
    if (userIntent.providerType.equals(CloudType.onprem)) {
      params.instanceType = node.cloudInfo.instance_type;
    }

    params.type = UpgradeTaskType.Software;
    params.setProperty("processType", ServerType.TSERVER.toString());
    params.setProperty("taskSubType", UpgradeTaskSubType.Download.toString());
    // The software package to install for this cluster.
    params.ybSoftwareVersion = ybSoftwareVersion;

    params.setYbcSoftwareVersion(universe.getUniverseDetails().getYbcSoftwareVersion());
    if (!StringUtils.isEmpty(params.getYbcSoftwareVersion())) {
      params.setEnableYbc(true);
    }
    return params;
  }

  /***
   * Parses the output of the PG upgrade check command and returns a JSON representation.
   *
   * This method processes the output of the PG upgrade check command, extracting relevant
   * information such as the title, checks, and overall status. It organizes this information into
   *  a structured JSON format for easier consumption.
   *
   * example input:
   *    "Performing Consistency Checks on Old Live Server
   *      ------------------------------------------------
   *      Checking cluster versions                                   ok
   *      Checking attributes of the 'yugabyte' user                  ok
   *      Checking for all 3 system databases                         ok
   *      Checking database connection settings                       ok
   *      Checking for system-defined composite types in user tables  ok
   *      Checking for reg* data types in user tables                 fatal
   *
   *      In database: yugabyte
   *        public.tbl2.b
   *
   *      Your installation contains one of the reg* data types in user tables.
   *      These data types reference system OIDs that are not preserved by
   *      pg_upgrade, so this cluster cannot currently be upgraded.  You can
   *      drop the problem columns and restart the upgrade.
   *      A list of the problem columns is printed above and in the file:
   *          /mnt/d0/pg_data/pg_upgrade_output.d/20250416T195704.864/tables_using_reg.txt
   *
   *      Checking for removed "abstime" data type in user tables     ok
   *      Checking for removed "reltime" data type in user tables     ok
   *      Checking for removed "tinterval" data type in user tables   ok
   *      Checking for user-defined postfix operators                 ok
   *      Checking for incompatible polymorphic functions             ok
   *      Checking for invalid "sql_identifier" user columns          ok
   *      Checking installed extensions                               ok
   *
   *
   *      Failure, exiting"
   *
   */
  public static ObjectNode parsePGUpgradeOutput(String input) {
    Map<String, Object> result = new HashMap<>();
    String[] lines = input.split("\n");
    String title = lines[0].trim();
    result.put("title", title);

    List<Map<String, Object>> checks = new ArrayList<>();
    result.put("checks", checks);

    // Pattern to match check lines (name and status)
    Pattern checkPattern = Pattern.compile("^(Checking.+?)\\s{2,}(\\w+)$");

    Map<String, Object> currentCheck = null;
    StringBuilder detailsBuilder = new StringBuilder();

    for (int i = 2; i < lines.length; i++) { // Skip title and separator line
      String line = lines[i].trim();

      if (line.isEmpty()) continue;

      // Check if this is the overall status (last line)
      if (i == lines.length - 1 && !line.matches("^Checking.+")) {
        result.put("overallStatus", line);
        continue;
      }

      // Try to match as a check line
      Matcher matcher = checkPattern.matcher(line);
      if (matcher.find()) {
        // If we have a previous check with details, add those details
        if (currentCheck != null && detailsBuilder.length() > 0) {
          currentCheck.put("details", detailsBuilder.toString().trim());
          detailsBuilder.setLength(0); // Clear the builder
        }

        // Create a new check
        currentCheck = new HashMap<>();
        currentCheck.put("name", matcher.group(1).trim());
        currentCheck.put("status", matcher.group(2).trim());
        currentCheck.put("details", null); // Default to null
        checks.add(currentCheck);
      } else if (currentCheck != null) {
        // This line is part of the details for the current check
        if (detailsBuilder.length() > 0) {
          detailsBuilder.append(" ");
        }
        detailsBuilder.append(line);
      }
    }

    // Handle any remaining details
    if (currentCheck != null && detailsBuilder.length() > 0) {
      currentCheck.put("details", detailsBuilder.toString().trim());
    }

    ObjectMapper mapper = new ObjectMapper();
    mapper.enable(SerializationFeature.INDENT_OUTPUT);
    return mapper.valueToTree(result);
  }
}
