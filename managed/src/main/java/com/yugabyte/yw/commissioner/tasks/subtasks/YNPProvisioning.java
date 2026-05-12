// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.payload.YNPConfigGenerator;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Instant;
import java.util.List;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class YNPProvisioning extends NodeTaskBase {
  private final YNPConfigGenerator ynpConfigGenerator;
  private ShellProcessContext shellContext =
      ShellProcessContext.builder().useSshConnectionOnly(true).logCmdOutput(true).build();

  @Inject
  protected YNPProvisioning(
      BaseTaskDependencies baseTaskDependencies, YNPConfigGenerator ynpConfigGenerator) {
    super(baseTaskDependencies);
    this.ynpConfigGenerator = ynpConfigGenerator;
  }

  public static class Params extends NodeTaskParams {
    public String sshUser;
    public UUID customerUuid;
    public String nodeAgentInstallDir;
    public boolean isYbPrebuiltImage;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public int getRetryLimit() {
    return 2;
  }

  @VisibleForTesting
  Path generateProvisionConfig(
      Universe universe,
      NodeDetails node,
      Provider provider,
      Path nodeAgentHome,
      UserIntent userIntent) {
    YNPConfigGenerator.ConfigParams configParams =
        YNPConfigGenerator.ConfigParams.builder()
            .nodeAgentHome(nodeAgentHome)
            .provider(provider)
            .nodeDetails(node)
            .universe(universe)
            .userIntent(userIntent)
            .isYbPrebuiltImage(taskParams().isYbPrebuiltImage)
            .build();
    return ynpConfigGenerator.generateConfigFile(configParams);
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNodeOrBadRequest(taskParams().nodeName);
    if (taskParams().sshUser != null) {
      shellContext = shellContext.toBuilder().sshUser(taskParams().sshUser).build();
    }
    Path nodeAgentHomePath = Paths.get(taskParams().nodeAgentInstallDir, NodeAgent.NODE_AGENT_DIR);

    Path nodeAgentScriptsPath = nodeAgentHomePath.resolve("scripts");
    Provider provider = Util.getProviderForNode(node, universe);

    /*
     *  But First, setup the dual NIC on YBM if needed. Let's do that even before we run
     *  YNP based provisioning because dual NIC setup will require a reboot which might
     *  clean up the tmp directory where the YNP config is created.
     */
    AnsibleSetupServer.Params ansibleParams =
        buildDualNicSetupParams(universe, node, provider, taskParams().userIntent);
    nodeManager
        .nodeCommand(NodeManager.NodeCommandType.Provision, ansibleParams)
        .processErrors("Dual NIC setup failed");
    boolean disableGolangYnpDriver =
        confGetter.getGlobalConf(GlobalConfKeys.disableGolangYnpDriver);
    String customTmpDirectory =
        confGetter.getConfForScope(provider, ProviderConfKeys.remoteTmpDirectory);
    String targetConfigPath =
        Paths.get(
                customTmpDirectory, String.format("config_%d.json", Instant.now().getEpochSecond()))
            .toString();
    Path tmpConfigFilepath =
        generateProvisionConfig(
            universe, node, provider, nodeAgentHomePath, taskParams().userIntent);
    nodeUniverseManager.uploadFileToNode(
        node, universe, tmpConfigFilepath.toString(), targetConfigPath, "755", shellContext);
    // Copy the conf file to scripts folder and run the provisioning script as in manual onprem.
    StringBuilder sb = new StringBuilder();
    sb.append("cd ").append(nodeAgentScriptsPath);
    sb.append(" && mv -f ").append(targetConfigPath);
    sb.append(" config.json && chmod +x node-agent-provision.sh");
    sb.append(" && ./node-agent-provision.sh --extra_vars config.json");
    if (disableGolangYnpDriver) {
      sb.append(" --use_python_driver");
    }
    sb.append(" --cloud_type ").append(node.cloudInfo.cloud);
    if (provider.getDetails().airGapInstall) {
      sb.append(" --is_airgap");
    }
    sb.append(" && chown -R $(id -u):$(id -g) ").append(nodeAgentHomePath);
    List<String> command = getCommand("/bin/bash", "-c", sb.toString());
    log.debug("Running YNP installation command: {}", command);

    nodeUniverseManager
        .runCommand(node, universe, command, shellContext)
        .processErrors("Installation failed");
  }

  private AnsibleSetupServer.Params buildDualNicSetupParams(
      Universe universe, NodeDetails node, Provider provider, UserIntent taskUserIntent) {
    UserIntent userIntent =
        taskUserIntent == null
            ? universe.getCluster(node.placementUuid).userIntent
            : taskUserIntent;
    AnsibleSetupServer.Params ansibleParams = new AnsibleSetupServer.Params();
    fillSetupParamsForNode(ansibleParams, userIntent, node);
    ansibleParams.sshUserOverride = node.sshUserOverride;
    ansibleParams.sshPortOverride = node.sshPortOverride;
    return ansibleParams;
  }

  private List<String> getCommand(String... args) {
    ImmutableList.Builder<String> commandBuilder = ImmutableList.builder();
    return commandBuilder.add("sudo").add(args).build();
  }
}
