// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class SetupYNP extends NodeTaskBase {
  private final NodeAgentManager nodeAgentManager;
  private final ShellProcessContext defaultShellContext =
      ShellProcessContext.builder().useSshConnectionOnly(true).logCmdOutput(true).build();

  @Inject
  protected SetupYNP(
      BaseTaskDependencies baseTaskDependencies,
      NodeUniverseManager nodeUniverseManager,
      NodeAgentManager nodeAgentManager,
      RuntimeConfGetter confGetter) {
    super(baseTaskDependencies);
    this.nodeAgentManager = nodeAgentManager;
  }

  public static class Params extends NodeTaskParams {
    public String nodeAgentInstallDir;
    public String sshUser;
    public UUID customerUuid;
    public boolean sudoAccess;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public int getRetryLimit() {
    return 2;
  }

  void removeNodeAgentDirectory(
      NodeDetails node, Universe universe, ShellProcessContext shellContext, String nodeAgentHome) {
    List<String> command = getCommand("/bin/bash", "-c", "rm -rf", nodeAgentHome);
    log.info("Clearing node-agent directory: {}", command);
    nodeUniverseManager.runCommand(node, universe, command, shellContext).isSuccess();
  }

  private Path getNodeAgentPackagePath(
      Universe universe, NodeDetails node, ShellProcessContext shellContext) {
    String output =
        nodeUniverseManager
            .runCommand(node, universe, Arrays.asList("uname", "-sm"), shellContext)
            .processErrors()
            .extractRunCommandOutput();
    if (StringUtils.isBlank(output)) {
      throw new RuntimeException("Unknown OS and Arch output: " + output);
    }
    // Output is like Linux x86_64.
    String[] parts = output.split("\\s+", 2);
    if (parts.length != 2) {
      throw new RuntimeException("Unknown OS and Arch output: " + output);
    }
    return nodeAgentManager.getNodeAgentPackagePath(
        OSType.parse(parts[0].trim()), ArchType.parse(parts[1].trim()));
  }

  @Override
  public void run() {
    ShellProcessContext shellContext = defaultShellContext;
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNodeOrBadRequest(taskParams().nodeName);
    if (taskParams().sshUser != null) {
      shellContext = defaultShellContext.toBuilder().sshUser(taskParams().sshUser).build();
    }
    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(universe.getCluster(node.placementUuid).userIntent.provider));

    String customTmpDirectory =
        confGetter.getConfForScope(provider, ProviderConfKeys.remoteTmpDirectory);
    Path ynpStagingDir = Paths.get(customTmpDirectory, "ynp");
    Path targetPackagePath = ynpStagingDir.resolve(Paths.get("release", "node-agent.tgz"));
    Path nodeAgentHomePath = Paths.get(taskParams().nodeAgentInstallDir, NodeAgent.NODE_AGENT_DIR);

    // Clean up the previous stale data.
    NodeAgent.maybeGetByIp(node.cloudInfo.private_ip).ifPresent(nodeAgentManager::purge);
    removeNodeAgentDirectory(node, universe, shellContext, nodeAgentHomePath.toString());
    Path packagePath = getNodeAgentPackagePath(universe, node, shellContext);
    // Clean and create the staging path where the node agent release will be uploaded.
    StringBuilder sb = new StringBuilder();
    sb.append("rm -rf ").append(ynpStagingDir);
    sb.append(" && mkdir -m 777 -p ").append(ynpStagingDir);

    List<String> command = getCommand("/bin/bash", "-c", sb.toString());
    log.info("Creating staging directory: {}", command);
    nodeUniverseManager.runCommand(node, universe, command, shellContext).processErrors();

    // Create the release path and upload file as the current SSH user.
    command =
        ImmutableList.<String>builder()
            .add("mkdir", "-p")
            .add(targetPackagePath.getParent().toString())
            .build();
    log.info("Creating release path in staging directory: {}", command);

    nodeUniverseManager.runCommand(node, universe, command, shellContext).processErrors();
    log.info("Uploading {} to {}", packagePath, targetPackagePath);
    nodeUniverseManager.uploadFileToNode(
        node, universe, packagePath.toString(), targetPackagePath.toString(), "755", shellContext);

    Path nodeAgentInstallPath = nodeAgentHomePath.getParent();

    sb.setLength(0);
    // Remove existing node agent folder.
    sb.append("rm -rf ").append(nodeAgentHomePath);
    // Create the node agent home directory.
    sb.append(" && mkdir -m 755 -p ").append(nodeAgentInstallPath);
    // Extract only the installer file.
    sb.append(" && tar --no-same-owner -zxf ").append(targetPackagePath);
    sb.append(" --strip-components=1 --exclude='*/devops' -C ").append(ynpStagingDir);
    // Move the node-agent source folder to the right location.
    sb.append(" && mv -f ").append(ynpStagingDir);
    sb.append(" ").append(nodeAgentHomePath);
    // Change the owner to the current user.
    sb.append(" && chown -R $(id -u):$(id -g) ").append(nodeAgentHomePath);
    sb.append(" && chmod 755 ").append(nodeAgentHomePath);
    command = getCommand("/bin/bash", "-c", sb.toString());
    nodeUniverseManager
        .runCommand(node, universe, command, shellContext)
        .processErrors("Extracting node-agent failed");
  }

  private List<String> getCommand(String... args) {
    ImmutableList.Builder<String> commandBuilder = ImmutableList.builder();
    if (taskParams().sudoAccess) {
      commandBuilder.add("sudo", "-H");
    }
    return commandBuilder.add(args).build();
  }
}
