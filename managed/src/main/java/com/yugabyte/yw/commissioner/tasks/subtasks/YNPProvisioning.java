// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.YBAError;
import com.yugabyte.yw.models.helpers.YBAError.Code;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class YNPProvisioning extends AbstractTaskBase {

  private final NodeUniverseManager nodeUniverseManager;
  private final NodeAgentManager nodeAgentManager;
  private final RuntimeConfGetter confGetter;
  private final CloudQueryHelper queryHelper;
  // Create ObjectMapper instance
  private final ObjectMapper mapper = new ObjectMapper();
  private ShellProcessContext shellContext =
      ShellProcessContext.builder().logCmdOutput(true).build();

  @Inject
  protected YNPProvisioning(
      BaseTaskDependencies baseTaskDependencies,
      NodeUniverseManager nodeUniverseManager,
      NodeAgentManager nodeAgentManager,
      RuntimeConfGetter confGetter,
      CloudQueryHelper queryHelper) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
    this.nodeAgentManager = nodeAgentManager;
    this.confGetter = confGetter;
    this.queryHelper = queryHelper;
  }

  public static class Params extends NodeTaskParams {
    public String sshUser;
    public UUID customerUuid;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  public void getProvisionArguments(
      Universe universe, NodeDetails node, String outputFilePath, Path nodeAgentHome) {
    ObjectNode rootNode = mapper.createObjectNode();
    ObjectNode ynpNode = mapper.createObjectNode();
    ynpNode.put("node_ip", node.cloudInfo.private_ip);
    ynpNode.put("is_install_node_agent", false);
    ynpNode.put("yb_user_id", "1994");
    rootNode.set("ynp", ynpNode);

    ObjectNode extraNode = mapper.createObjectNode();
    extraNode.put("cloud_type", node.cloudInfo.cloud);
    extraNode.put("is_cloud", true);
    if (taskParams().deviceInfo.mountPoints != null) {
      extraNode.put("mount_paths", taskParams().deviceInfo.mountPoints);
    } else {
      int numVolumes =
          universe.getCluster(node.placementUuid).userIntent.getDeviceInfoForNode(node).numVolumes;
      StringBuilder volumePaths = new StringBuilder();
      for (int i = 0; i < numVolumes; i++) {
        if (i > 0) {
          volumePaths.append(" ");
        }
        volumePaths.append("/mnt/d").append(i);
      }
      extraNode.put("mount_paths", volumePaths.toString());
    }
    if (node.cloudInfo.cloud.equals(Common.CloudType.azu.toString())
        && node.cloudInfo.lun_indexes.length > 0) {
      StringBuilder sb = new StringBuilder();
      for (int i = 0; i < node.cloudInfo.lun_indexes.length; i++) {
        sb.append(node.cloudInfo.lun_indexes[i]);
        if (i < node.cloudInfo.lun_indexes.length - 1) {
          sb.append(" ");
        }
      }
      extraNode.put("disk_lun_indexes", sb.toString());
    }
    String buildRelease = nodeAgentManager.getSoftwareVersion();
    String localPackagePath = nodeAgentHome.toString() + "/thirdparty";
    if (localPackagePath != null) {
      extraNode.put("package_path", localPackagePath);
    }

    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(universe.getCluster(node.placementUuid).userIntent.provider));
    List<String> devicePaths =
        this.queryHelper.getDeviceNames(
            provider,
            Common.CloudType.valueOf(node.cloudInfo.cloud),
            Integer.toString(taskParams().deviceInfo.numVolumes),
            taskParams().deviceInfo.storageType.toString().toLowerCase(),
            node.cloudInfo.region,
            node.cloudInfo.instance_type);
    String paths = String.join(" ", devicePaths);

    extraNode.put("device_paths", paths);
    rootNode.set("extra", extraNode);

    ObjectNode loggingNode = mapper.createObjectNode();
    loggingNode.put("level", "DEBUG");
    rootNode.set("logging", loggingNode);

    try {
      String jsonString = mapper.writerWithDefaultPrettyPrinter().writeValueAsString(rootNode);
      // Write the JSON string to the specified file
      Path outputPath = Paths.get(outputFilePath);
      Files.write(
          outputPath,
          jsonString.getBytes(),
          StandardOpenOption.CREATE,
          StandardOpenOption.TRUNCATE_EXISTING);
    } catch (Exception e) {
      log.error("Failed parsing JSON file: {}", e.getMessage());
    }
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNodeOrBadRequest(taskParams().nodeName);
    NodeAgent nodeAgent = null;
    Optional<NodeAgent> optional = NodeAgent.maybeGetByIp(node.cloudInfo.private_ip);
    if (optional.isPresent()) {
      nodeAgent = optional.get();
    } else {
      log.error("Node Agent does not exist. Skipping.");
      return;
    }
    if (taskParams().sshUser != null) {
      shellContext = shellContext.toBuilder().sshUser(taskParams().sshUser).build();
    }

    StringBuilder sb = new StringBuilder();

    String tmpDirectory =
        confGetter.getGlobalConf(GlobalConfKeys.ybTmpDirectoryPath) + "/config.json";
    getProvisionArguments(universe, node, tmpDirectory, Paths.get(nodeAgent.getHome()));
    nodeUniverseManager.uploadFileToNode(
        node, universe, tmpDirectory, nodeAgent.getHome() + "/", "755", shellContext);

    String buildRelease = nodeAgentManager.getSoftwareVersion();
    sb = new StringBuilder();
    sb.append("cd ").append(nodeAgent.getHome()).append(" && ");

    String configFilePath = nodeAgent.getHome() + "/config.json";
    sb.append("sudo ./node-agent-provision.sh --extra_vars ")
        .append(configFilePath)
        .append(" --cloud_type ")
        .append(node.cloudInfo.cloud);
    List<String> command = getCommand("/bin/bash", "-c", sb.toString());
    log.debug("Running YNP installation command: {}", command);
    try {
      nodeUniverseManager
          .runCommand(node, universe, command, shellContext)
          .processErrors("Installation failed");
    } catch (Exception e) {
      nodeAgent.updateLastError(new YBAError(Code.INSTALLATION_ERROR, e.getMessage()));
      throw e;
    }
  }

  private List<String> getCommand(String... args) {
    ImmutableList.Builder<String> commandBuilder = ImmutableList.builder();
    return commandBuilder.add(args).build();
  }
}
