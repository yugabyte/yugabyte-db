// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.ServerSubTaskBase;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeManager.NodeCommandType;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class PGUpgradeTServerCheck extends ServerSubTaskBase {

  private final NodeUniverseManager nodeUniverseManager;
  private final NodeManager nodeManager;
  private final KubernetesManagerFactory kubernetesManagerFactory;

  private final int PG_UPGRADE_CHECK_TIMEOUT = 300;

  private final String K8S_PG_UPGRADE_CHECK_LOC = "/mnt/disk0/yw-data";

  public static class Params extends ServerSubTaskParams {
    public String ybSoftwareVersion;
    public boolean downloadPackage = true;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Inject
  protected PGUpgradeTServerCheck(
      BaseTaskDependencies baseTaskDependencies,
      NodeUniverseManager nodeUniverseManager,
      NodeManager nodeManager,
      KubernetesManagerFactory kubernetesManagerFactory) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
    this.nodeManager = nodeManager;
    this.kubernetesManagerFactory = kubernetesManagerFactory;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getTServersInPrimaryCluster().stream().findAny().get();
    boolean isK8sUniverse =
        universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .providerType
            .equals(CloudType.kubernetes);

    try {
      if (taskParams().downloadPackage) {
        // Clean up the downloaded package from the node.
        cleanUpDownloadedPackage(universe, node, isK8sUniverse);
        // Download the package to the node.
        downloadPackage(universe, node, isK8sUniverse);
      }
      // Run check on the node
      if (isK8sUniverse) {
        runCheckOnPod(universe, node);
      } else {
        runCheckOnNode(universe, node);
      }
    } finally {
      if (taskParams().downloadPackage) {
        // Clean up the downloaded package from the node.
        cleanUpDownloadedPackage(universe, node, isK8sUniverse);
      }
    }
  }

  private void cleanUpDownloadedPackage(
      Universe universe, NodeDetails node, boolean isK8sUniverse) {
    ReleaseContainer release = releaseManager.getReleaseByVersion(taskParams().ybSoftwareVersion);
    if (isK8sUniverse) {
      String ybServerPackage = release.getFilePath(getArchitectureOnK8sPod(node));
      String packageName = extractPackageName(ybServerPackage);
      String versionName = extractVersionName(ybServerPackage);
      Map<String, String> zoneConfig =
          CloudInfoInterface.fetchEnvVars(AvailabilityZone.getOrBadRequest(node.azUuid));
      String namespace = node.cloudInfo.kubernetesNamespace;
      String podName = node.cloudInfo.kubernetesPodName;
      List<String> deletePackageCommand =
          ImmutableList.of(
              "/bin/bash",
              "-c",
              String.format(
                  "rm -rf %s %s;",
                  K8S_PG_UPGRADE_CHECK_LOC + "/" + packageName,
                  K8S_PG_UPGRADE_CHECK_LOC + "/" + versionName));
      kubernetesManagerFactory
          .getManager()
          .executeCommandInPodContainer(
              zoneConfig, namespace, podName, "yb-tserver", deletePackageCommand);
    } else {
      String ybServerPackage = release.getFilePath(universe.getUniverseDetails().arch);
      String packageName = extractPackageName(ybServerPackage);
      String versionName = extractVersionName(ybServerPackage);
      String ybSoftwareDir = nodeUniverseManager.getYbHomeDir(node, universe) + "/yb-software/";
      nodeUniverseManager
          .runCommand(
              node,
              universe,
              ImmutableList.of(
                  "rm", "-rf", ybSoftwareDir + packageName, ybSoftwareDir + versionName),
              ShellProcessContext.builder().logCmdOutput(true).build())
          .processErrors();
    }
  }

  private void downloadPackage(Universe universe, NodeDetails node, boolean isk8sUniverse) {
    ReleaseContainer release = releaseManager.getReleaseByVersion(taskParams().ybSoftwareVersion);
    if (isk8sUniverse) {
      Map<String, String> zoneConfig =
          CloudInfoInterface.fetchEnvVars(AvailabilityZone.getOrBadRequest(node.azUuid));
      String namespace = node.cloudInfo.kubernetesNamespace;
      String podName = node.cloudInfo.kubernetesPodName;
      Architecture arch = getArchitectureOnK8sPod(node);
      String ybServerPackage = release.getFilePath(arch);
      String packageName = extractPackageName(ybServerPackage);
      String versionName = extractVersionName(ybServerPackage);
      // Copy the package to the node in temp directory
      if (release.isHttpDownload(ybServerPackage)) {
        kubernetesManagerFactory
            .getManager()
            .executeCommandInPodContainer(
                zoneConfig,
                namespace,
                podName,
                "yb-tserver",
                ImmutableList.of(
                    "/bin/bash",
                    "-c",
                    "curl -o "
                        + K8S_PG_UPGRADE_CHECK_LOC
                        + "/"
                        + packageName
                        + " "
                        + ybServerPackage));
      } else {
        kubernetesManagerFactory
            .getManager()
            .copyFileToPod(
                zoneConfig,
                namespace,
                podName,
                "yb-tserver",
                ybServerPackage,
                K8S_PG_UPGRADE_CHECK_LOC);
      }
      // Extract the package
      String command =
          String.format(
              "mkdir -p %s; tar -xvzf %s -C %s --strip-components=1",
              K8S_PG_UPGRADE_CHECK_LOC + "/" + versionName,
              K8S_PG_UPGRADE_CHECK_LOC + "/" + packageName,
              K8S_PG_UPGRADE_CHECK_LOC + "/" + versionName);
      List<String> extractPackageCommand = ImmutableList.of("/bin/bash", "-c", command);
      kubernetesManagerFactory
          .getManager()
          .executeCommandInPodContainer(
              zoneConfig, namespace, podName, "yb-tserver", extractPackageCommand);
    } else {
      AnsibleConfigureServers.Params params =
          getAnsibleConfigureServerParamsToDownloadSoftware(
              universe, node, taskParams().ybSoftwareVersion);
      nodeManager.nodeCommand(NodeCommandType.Configure, params).processErrors();
    }
  }

  private void runCheckOnPod(Universe universe, NodeDetails node) {
    ReleaseContainer release = releaseManager.getReleaseByVersion(taskParams().ybSoftwareVersion);
    Architecture arch = getArchitectureOnK8sPod(node);
    String ybServerPackage = release.getFilePath(arch);
    String versionName = extractVersionName(ybServerPackage);
    String tmpDirectory = nodeUniverseManager.getRemoteTmpDir(node, universe);

    Map<String, String> zoneConfig =
        CloudInfoInterface.fetchEnvVars(AvailabilityZone.getOrBadRequest(node.azUuid));
    String namespace = node.cloudInfo.kubernetesNamespace;
    String podName = node.cloudInfo.kubernetesPodName;
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    String pgUpgradeBinaryLocation =
        String.format("%s/%s/postgres/bin/pg_upgrade", K8S_PG_UPGRADE_CHECK_LOC, versionName);
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
            "%s -d %s/pg_data --old-host %s --old-port %s --username yugabyte --check",
            pgUpgradeBinaryLocation,
            Util.getDataDirectoryPath(universe, node, config),
            oldHost,
            oldPort);
    List<String> pgUpgradeCheckCommand = ImmutableList.of("/bin/bash", "-c", upgradeCheckCommand);
    kubernetesManagerFactory
        .getManager()
        .executeCommandInPodContainer(
            zoneConfig, namespace, podName, "yb-tserver", pgUpgradeCheckCommand);
  }

  private void runCheckOnNode(Universe universe, NodeDetails node) {
    List<String> command = new ArrayList<>();
    Architecture arch = universe.getUniverseDetails().arch;
    ReleaseContainer release = releaseManager.getReleaseByVersion(taskParams().ybSoftwareVersion);
    String ybServerPackage = release.getFilePath(arch);
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    String pgUpgradeBinaryLocation =
        nodeUniverseManager.getYbHomeDir(node, universe)
            + "/yb-software/"
            + extractVersionName(ybServerPackage)
            + "/postgres/bin/pg_upgrade";
    command.add(pgUpgradeBinaryLocation);
    command.add("-d");
    String pgDataDir = Util.getDataDirectoryPath(universe, node, config) + "/pg_data";
    command.add(pgDataDir);
    command.add("--old-host");
    if (primaryCluster.userIntent.enableYSQLAuth) {
      String customTmpDirectory = GFlagsUtil.getCustomTmpDirectory(node, universe);
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
    // Pipe stdout to stderr to capture the failed checks in logs.
    command.add("1>&2");

    ShellProcessContext context =
        ShellProcessContext.builder()
            .logCmdOutput(true)
            .timeoutSecs(PG_UPGRADE_CHECK_TIMEOUT)
            .build();

    log.info("Running PG15 upgrade check on node: {} with command: ", node.nodeName, command);
    ShellResponse response =
        nodeUniverseManager.runCommand(node, universe, command, context).processErrors();
    if (response.code != 0) {
      log.info(
          "PG upgrade check failed on node: {} with error: {}", node.nodeName, response.message);
      throw new RuntimeException("PG15 upgrade check failed on node: " + node.nodeName);
    }
  }

  private String extractVersionName(String ybServerPackage) {
    return extractPackageName(ybServerPackage).replace(".tar.gz", "");
  }

  private String extractPackageName(String ybServerPackage) {
    String[] parts = ybServerPackage.split("/");
    return parts[parts.length - 1];
  }

  private Architecture getArchitectureOnK8sPod(NodeDetails node) {
    Map<String, String> zoneConfig =
        CloudInfoInterface.fetchEnvVars(AvailabilityZone.getOrBadRequest(node.azUuid));
    String architecture =
        kubernetesManagerFactory
            .getManager()
            .executeCommandInPodContainer(
                zoneConfig,
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
}
