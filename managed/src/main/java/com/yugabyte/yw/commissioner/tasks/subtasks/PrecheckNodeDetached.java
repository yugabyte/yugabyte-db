// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.DetachedNodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.NodeConfig.Type;
import com.yugabyte.yw.models.helpers.NodeConfig.ValidationResult;
import com.yugabyte.yw.models.helpers.NodeConfigValidator;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import play.libs.Json;

public class PrecheckNodeDetached extends AbstractTaskBase {

  private final NodeManager nodeManager;
  private final NodeConfigValidator nodeConfigValidator;

  @Inject
  protected PrecheckNodeDetached(
      BaseTaskDependencies baseTaskDependencies,
      NodeManager nodeManager,
      NodeConfigValidator nodeConfigValidator) {
    super(baseTaskDependencies);
    this.nodeManager = nodeManager;
    this.nodeConfigValidator = nodeConfigValidator;
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
          response.message = Json.toJson(failedChecks).toPrettyString();
        }
      } catch (IllegalArgumentException e) {
        for (JsonNode node : responseJson) {
          if (!node.isBoolean() || !node.asBoolean()) {
            // If a check failed, change the return code so processShellResponse errors.
            response.code = 1;
            break;
          }
        }
      }
    }
    response.processErrors();
  }

  @Override
  public void run() {
    ShellResponse response =
        getNodeManager().detachedNodeCommand(NodeManager.NodeCommandType.Precheck, taskParams());
    processPreflightResponse(
        nodeConfigValidator,
        taskParams().getProvider(),
        taskParams().getNodeUuid(),
        true,
        response);
  }
}
