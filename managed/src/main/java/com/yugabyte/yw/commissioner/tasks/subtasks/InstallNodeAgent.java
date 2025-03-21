// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.YBAError;
import com.yugabyte.yw.models.helpers.YBAError.Code;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class InstallNodeAgent extends AbstractTaskBase {
  public static final int DEFAULT_NODE_AGENT_PORT = 9070;

  private final NodeUniverseManager nodeUniverseManager;
  private final NodeAgentManager nodeAgentManager;
  private ShellProcessContext shellContext =
      ShellProcessContext.builder().logCmdOutput(true).build();

  @Inject
  protected InstallNodeAgent(
      BaseTaskDependencies baseTaskDependencies,
      NodeUniverseManager nodeUniverseManager,
      NodeAgentManager nodeAgentManager) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
    this.nodeAgentManager = nodeAgentManager;
  }

  public static class Params extends NodeTaskParams {
    public int nodeAgentPort = DEFAULT_NODE_AGENT_PORT;
    public String nodeAgentInstallDir;
    public String sshUser;
    public UUID customerUuid;
    public boolean reinstall;
    public boolean airgap;
    public boolean sudoAccess;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  private SetupYNP.Params getSetupYNPParams(InstallNodeAgent.Params taskParams) {
    SetupYNP.Params params = new SetupYNP.Params();
    params.sshUser = taskParams.sshUser;
    params.nodeName = taskParams.nodeName;
    params.customerUuid = taskParams.customerUuid;
    params.setUniverseUUID(taskParams.getUniverseUUID());
    params.nodeAgentInstallDir = taskParams.nodeAgentInstallDir;
    params.nodeAgentPort = taskParams.nodeAgentPort;
    params.sudoAccess = taskParams.sudoAccess;

    return params;
  }

  boolean doesNodeAgentDirectoryExists(
      NodeDetails node, Universe universe, ShellProcessContext shellContext, String nodeAgentHome) {
    StringBuilder sb = new StringBuilder();
    sb.append("test -d ");
    sb.append(nodeAgentHome);
    sb.append("/");
    List<String> command = getCommand("/bin/bash", "-c", sb.toString());
    log.info("Checking if node-agent directory exists: {}", command);
    boolean directoryExistence =
        nodeUniverseManager.runCommand(node, universe, command, shellContext).isSuccess();
    if (directoryExistence) {
      return true;
    }
    return false;
  }

  public NodeAgent install() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNodeOrBadRequest(taskParams().nodeName);
    NodeAgent nodeAgent = null;
    Optional<NodeAgent> optional = NodeAgent.maybeGetByIp(node.cloudInfo.private_ip);
    if (taskParams().sshUser != null) {
      shellContext = shellContext.toBuilder().sshUser(taskParams().sshUser).build();
    }
    String customTmpDirectory = GFlagsUtil.getCustomTmpDirectory(node, universe);

    boolean shouldSetUp = !optional.isPresent();
    if (optional.isPresent()) {
      nodeAgent = optional.get();
      if (nodeAgent.getState() == State.READY && !taskParams().reinstall) {
        // Already ready and it is not a reinstall.
        return nodeAgent;
      }
      if (taskParams().reinstall
          || doesNodeAgentDirectoryExists(node, universe, shellContext, nodeAgent.getHome())
              == false) {
        // If it is a reinstall, we don't care the state. Always go for new setup.
        // Also if the directory is not present, dangling node agent is found. We need to start new.
        nodeAgentManager.purge(nodeAgent);
        shouldSetUp = true;
      }
    }

    if (shouldSetUp) {
      SetupYNP task = createTask(SetupYNP.class);
      task.initialize(getSetupYNPParams(taskParams()));
      task.run();
      optional = NodeAgent.maybeGetByIp(node.cloudInfo.private_ip);
      nodeAgent = optional.get();
    }

    StringBuilder sb = new StringBuilder();
    List<String> command;

    Path nodeAgentHomePath = Paths.get(nodeAgent.getHome());
    Path nodeAgentInstallPath = nodeAgentHomePath.getParent();
    Path nodeAgentInstallerPath = nodeAgentHomePath.resolve("node-agent-installer.sh");
    String certDirectoryPath = nodeAgentHomePath.toString() + "/cert/";

    command =
        getCommand(
            "find",
            certDirectoryPath,
            "-mindepth",
            "1",
            "-maxdepth",
            "1",
            "-type",
            "d",
            "-exec",
            "basename",
            "{}",
            "\\;");
    log.info("Retreiving cert Directory: {}", command);
    String certDirectory =
        nodeUniverseManager
            .runCommand(node, universe, command, shellContext)
            .processErrors()
            .extractRunCommandOutput();

    // Change the owner to the current user.
    sb.append("chown -R $(id -u):$(id -g) ").append(nodeAgentHomePath);
    // Give executable permission to the installer script.
    sb.append(" && chmod +x ").append(nodeAgentInstallerPath);
    // Run the installer script.
    sb.append(" && ").append(nodeAgentInstallerPath);
    sb.append(" -c install --skip_verify_cert --disable_egress");
    sb.append(" --install_path ").append(nodeAgentInstallPath);
    sb.append(" --id ").append(nodeAgent.getUuid());
    sb.append(" --customer_id ").append(nodeAgent.getCustomerUuid());
    sb.append(" --cert_dir ").append(certDirectory);
    sb.append(" --node_name ").append(node.getNodeName());
    sb.append(" --node_ip ").append(node.cloudInfo.private_ip);
    sb.append(" --node_port ").append(String.valueOf(taskParams().nodeAgentPort));
    if (taskParams().airgap) {
      sb.append(" --airgap");
    }
    command = getCommand("/bin/bash", "-c", sb.toString());
    log.debug("Running node agent installation command: {}", command);

    try {
      nodeUniverseManager
          .runCommand(node, universe, command, shellContext)
          .processErrors("Installation failed");
    } catch (RuntimeException e) {
      nodeAgent.updateLastError(new YBAError(Code.INSTALLATION_ERROR, e.getMessage()));
      throw e;
    }
    nodeAgent.saveState(State.REGISTERED);
    sb.setLength(0);
    sb.append("systemctl");
    if (!taskParams().sudoAccess) {
      sb.append(" --user");
    }
    sb.append(" is-active --quiet yb-node-agent");
    command = getCommand("/bin/bash", "-c", sb.toString());
    log.debug("Waiting for node agent service to be running");
    log.debug("Running systemd command: {}", command);
    try {
      nodeUniverseManager
          .runCommand(node, universe, command, shellContext)
          .processErrors("Service startup failed");
    } catch (Exception e) {
      nodeAgent.updateLastError(new YBAError(Code.SERVICE_START_ERROR, e.getMessage()));
      throw e;
    }
    return nodeAgent;
  }

  @Override
  public void run() {
    install();
  }

  private List<String> getCommand(String... args) {
    ImmutableList.Builder<String> commandBuilder = ImmutableList.builder();
    if (taskParams().sudoAccess) {
      commandBuilder.add("sudo", "-H");
    }
    return commandBuilder.add(args).build();
  }
}
