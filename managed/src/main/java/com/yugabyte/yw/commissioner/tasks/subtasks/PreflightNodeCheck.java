// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.payload.YNPConfigGenerator;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeConfigValidator;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.nodeagent.YnpPreflightCheckInput;
import com.yugabyte.yw.nodeagent.YnpPreflightCheckOutput;
import java.nio.file.Path;
import java.nio.file.Paths;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class PreflightNodeCheck extends NodeTaskBase {

  private final NodeConfigValidator nodeConfigValidator;
  private final YNPConfigGenerator ynpConfigGenerator;

  @Inject
  protected PreflightNodeCheck(
      BaseTaskDependencies baseTaskDependencies,
      NodeConfigValidator nodeConfigValidator,
      YNPConfigGenerator ynpConfigGenerator) {
    super(baseTaskDependencies);
    this.nodeConfigValidator = nodeConfigValidator;
    this.ynpConfigGenerator = ynpConfigGenerator;
  }

  // Parameters for precheck task.
  public static class Params extends NodeTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  private void runLegacyPreflightChecks() {
    ShellResponse response =
        getNodeManager().nodeCommand(NodeManager.NodeCommandType.Precheck, taskParams());
    try {
      PrecheckNodeDetached.processPreflightResponse(
          nodeConfigValidator,
          taskParams().getProvider(),
          taskParams().nodeUuid,
          taskParams().getNodeName(),
          false,
          response);
    } catch (RuntimeException e) {
      log.error(
          "Failed preflight checks for node {}:\n{}", taskParams().nodeName, response.message);
      throw e;
    }
  }

  private void runYnpPreflightChecks(
      Provider provider, NodeInstance instance, NodeAgent nodeAgent, @Nullable Universe universe) {
    Path nodeAgentHome = Paths.get(nodeAgent.getHome());
    NodeDetails node = universe == null ? null : universe.getNode(taskParams().nodeName);
    YNPConfigGenerator.ConfigParams configParams =
        YNPConfigGenerator.ConfigParams.builder()
            .nodeAgentHome(nodeAgentHome)
            .provider(provider)
            .nodeInstance(instance)
            .nodeDetails(node)
            .universe(universe)
            .build();
    ObjectNode rootNode = ynpConfigGenerator.generateConfig(configParams);
    if (confGetter.getGlobalConf(GlobalConfKeys.enableYnpVersionCheck)) {
      Object ynpVersion =
          configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata).get("ynp_version");
      if (ynpVersion != null) {
        ((ObjectNode) rootNode.get("extra"))
            .put("expected_ynp_version", ynpVersion.toString().trim());
      }
    }
    String customTmpDirectory =
        confGetter.getConfForScope(provider, ProviderConfKeys.remoteTmpDirectory);
    YnpPreflightCheckInput checkParams =
        YnpPreflightCheckInput.newBuilder()
            .setConfigJson(rootNode.toString())
            .setRemoteTmp(customTmpDirectory)
            .build();
    YnpPreflightCheckOutput checkOutput =
        nodeAgentClient.runYnpPreflightCheck(nodeAgent, checkParams, null /* custom user */);
    log.info("YNP preflight check for universe node {} ran successfully", taskParams().nodeName);
    if (checkOutput.getExitCode() != 0) {
      String errMsg =
          String.format(
              "Failed YNP preflight checks for node %s with exit code %d. Output: %s",
              taskParams().nodeName, checkOutput.getExitCode(), checkOutput.getOutput());
      log.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
  }

  @Override
  public void run() {
    log.info("Running preflight checks for node {}.", taskParams().nodeName);
    Provider provider = taskParams().getProvider();
    if (provider.isManualOnprem()) {
      // Node agent must already be present and running for onprem manual (non-sudo).
      NodeInstance instance = NodeInstance.getOrBadRequest(taskParams().nodeUuid);
      NodeAgent nodeAgent = nodeAgentClient.getAndUpgradeOrThrow(instance.getDetails().ip);
      if (confGetter.getGlobalConf(GlobalConfKeys.disableYnpNodePreflightCheck)) {
        // Run preflight_checks.sh.
        runLegacyPreflightChecks();
      } else {
        // Run the new YNP preflight checks implemented in node-agent.
        runYnpPreflightChecks(provider, instance, nodeAgent, getUniverse());
      }
    } else {
      // Sudo access is available, and YNP is not set up yet.
      runLegacyPreflightChecks();
    }
  }
}
