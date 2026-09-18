// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.NodeAgentHandler;
import api.v2.models.NodeAgentUpgradeSpec;
import api.v2.models.YBATask;
import com.google.inject.Inject;
import java.util.UUID;
import play.mvc.Http;

public class NodeAgentApiControllerImp extends NodeAgentApiControllerImpInterface {
  private final NodeAgentHandler nodeAgentHandler;

  @Inject
  public NodeAgentApiControllerImp(NodeAgentHandler nodeAgentHandler) {
    this.nodeAgentHandler = nodeAgentHandler;
  }

  @Override
  public YBATask upgradeNodeAgent(
      Http.Request request, UUID cUUID, UUID uniUUID, NodeAgentUpgradeSpec nodeAgentUpgradeSpec)
      throws Exception {
    return nodeAgentHandler.upgradeNodeAgent(cUUID, uniUUID, nodeAgentUpgradeSpec);
  }
}
