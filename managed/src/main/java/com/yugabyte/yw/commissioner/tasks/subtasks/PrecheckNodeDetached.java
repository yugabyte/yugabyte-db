// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.DetachedNodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.payload.YNPConfigGenerator;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.NodeConfig.Type;
import com.yugabyte.yw.models.helpers.NodeConfig.ValidationResult;
import com.yugabyte.yw.models.helpers.NodeConfigValidator;
import com.yugabyte.yw.nodeagent.YnpPreflightCheckInput;
import com.yugabyte.yw.nodeagent.YnpPreflightCheckOutput;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Slf4j
public class PrecheckNodeDetached extends AbstractTaskBase {

  private final NodeConfigValidator nodeConfigValidator;
  private final YNPConfigGenerator ynpConfigGenerator;

  @Inject
  protected PrecheckNodeDetached(
      BaseTaskDependencies baseTaskDependencies,
      NodeConfigValidator nodeConfigValidator,
      YNPConfigGenerator ynpConfigGenerator) {
    super(baseTaskDependencies);
    this.nodeConfigValidator = nodeConfigValidator;
    this.ynpConfigGenerator = ynpConfigGenerator;
  }

  public NodeManager getNodeManager() {
    return nodeManager;
  }

  @Override
  protected DetachedNodeTaskParams taskParams() {
    return (DetachedNodeTaskParams) taskParams;
  }

  public static void processPreflightResponse(
      NodeConfigValidator nodeConfigValidator,
      Provider provider,
      UUID nodeUuid,
      String nodeName,
      boolean isDetached,
      ShellResponse response) {
    if (response.code == 0) {
      JsonNode responseJson = Json.parse(response.message);
      try {
        // Try converting to node instance data first for node-agent.
        NodeInstanceData instanceData =
            Json.mapper()
                .copy()
                .enable(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES)
                .convertValue(responseJson, NodeInstanceData.class);
        Map<Type, ValidationResult> result =
            nodeConfigValidator.validateNodeConfigs(
                provider, nodeUuid, instanceData.nodeConfigs, isDetached);
        List<ValidationResult> failedChecks =
            result.values().stream()
                .filter(v -> !v.isValid() && v.isRequired())
                .collect(Collectors.toList());
        if (failedChecks.size() > 0) {
          response.code = 1;
          List<String> failedCheckNames =
              failedChecks.stream().map(v -> v.getType().toString()).collect(Collectors.toList());
          log.error("Node {} has failed preflight checks: {}", nodeName, failedCheckNames);
          response.message = Json.toJson(failedChecks).toPrettyString();
        }
      } catch (IllegalArgumentException e) {
        for (JsonNode node : responseJson) {
          if (!node.isBoolean() || !node.asBoolean()) {
            // If a check failed, change the return code so processShellResponse errors.
            log.error("Node {} has failed preflight checks", nodeName, e);
            response.code = 1;
            break;
          }
        }
      }
    }
    String errMsg = null;
    if (response.code != ShellResponse.ERROR_CODE_SUCCESS) {
      errMsg =
          String.format(
              "Failed preflight checks for node %s",
              StringUtils.isEmpty(nodeName) ? nodeUuid : nodeName);
    }
    response.processErrors(errMsg);
  }

  private void runLegacyPreflightChecks() {
    ShellResponse response =
        getNodeManager().detachedNodeCommand(NodeManager.NodeCommandType.Precheck, taskParams());
    processPreflightResponse(
        nodeConfigValidator,
        taskParams().getProvider(),
        taskParams().getNodeUuid(),
        taskParams().getNodeName(),
        true,
        response);
  }

  private void runYnpPreflightChecks(
      Provider provider, NodeInstance instance, NodeAgent nodeAgent) {
    Path nodeAgentHome = Paths.get(nodeAgent.getHome());
    YNPConfigGenerator.ConfigParams configParams =
        YNPConfigGenerator.ConfigParams.builder()
            .nodeAgentHome(nodeAgentHome)
            .provider(provider)
            .nodeInstance(instance)
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
    log.info("YNP preflight check for node instance {} ran successfully", instance.getNodeName());
    if (checkOutput.getExitCode() != 0) {
      String errMsg =
          String.format(
              "Failed YNP preflight checks for node %s with exit code %d. Output: %s",
              instance.getNodeName(), checkOutput.getExitCode(), checkOutput.getOutput());
      log.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
  }

  @Override
  public void run() {
    Provider provider = taskParams().getProvider();
    if (provider.isManualOnprem()) {
      // Node agent must already be present and running for onprem manual (non-sudo).
      NodeInstance instance = NodeInstance.getOrBadRequest(taskParams().getNodeUuid());
      NodeAgent nodeAgent = nodeAgentClient.getAndUpgradeOrThrow(instance.getDetails().ip);
      if (confGetter.getGlobalConf(GlobalConfKeys.disableYnpNodePreflightCheck)) {
        // Run preflight_checks.sh.
        runLegacyPreflightChecks();
      } else {
        // Run the new YNP preflight checks implemented in node-agent.
        runYnpPreflightChecks(provider, instance, nodeAgent);
      }
    } else {
      // Sudo access is available, and YNP is not set up yet.
      runLegacyPreflightChecks();
    }
  }
}
