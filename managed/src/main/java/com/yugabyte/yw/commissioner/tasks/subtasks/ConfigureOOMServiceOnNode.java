// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.AdditionalServicesStateData;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.nodeagent.ConfigureServiceInput;
import com.yugabyte.yw.nodeagent.ConfigureServiceOutput;
import com.yugabyte.yw.nodeagent.EarlyoomConfig;
import com.yugabyte.yw.nodeagent.Service;
import com.yugabyte.yw.nodeagent.ServiceConfig;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;

@Slf4j
public class ConfigureOOMServiceOnNode extends NodeTaskBase {

  @Inject
  protected ConfigureOOMServiceOnNode(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public AdditionalServicesStateData.EarlyoomConfig earlyoomConfig;
    public boolean earlyoomEnabled;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Running for {}", getName());
    UUID universeUUID = taskParams().getUniverseUUID();
    String nodeName = taskParams().nodeName;
    Universe universe = Universe.getOrBadRequest(universeUUID);
    NodeDetails node = universe.getNodeOrBadRequest(nodeName);
    if (node == null) {
      throw new IllegalStateException("Failed to find node with name " + nodeName);
    }
    Optional<NodeAgent> nodeAgent = nodeUniverseManager.maybeGetNodeAgent(universe, node, true);
    if (nodeAgent.isEmpty()) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST, "Cannot upgrade " + nodeName + ": node agent is not available");
    }
    String homeDir =
        GFlagsUtil.getYbHomeDir(universe.getCluster(node.placementUuid).userIntent.provider);
    AdditionalServicesStateData.EarlyoomConfig earlyoomConfig = taskParams().earlyoomConfig;
    EarlyoomConfig.Builder earlyoomConfigBuilder =
        EarlyoomConfig.newBuilder()
            .setStartArgs(AdditionalServicesStateData.toArgs(earlyoomConfig));
    ConfigureServiceInput input =
        ConfigureServiceInput.newBuilder()
            .setService(Service.EARLYOOM)
            .setEnabled(taskParams().earlyoomEnabled)
            .setConfig(
                ServiceConfig.newBuilder()
                    .setYbHomeDir(homeDir)
                    .setEarlyoomConfig(earlyoomConfigBuilder)
                    .build())
            .build();
    ConfigureServiceOutput configureServiceOutput =
        nodeAgentClient.runConfigureEarlyoom(nodeAgent.get(), input, NodeManager.YUGABYTE_USER);
    log.debug(
        "Updated {}: has error {}", nodeAgent.get().getIp(), configureServiceOutput.hasError());
    if (configureServiceOutput.hasError()) {
      throw new PlatformServiceException(
          Http.Status.INTERNAL_SERVER_ERROR,
          "Failed to upgrade "
              + nodeName
              + ". Got "
              + configureServiceOutput.getError().getMessage());
    }
  }
}
