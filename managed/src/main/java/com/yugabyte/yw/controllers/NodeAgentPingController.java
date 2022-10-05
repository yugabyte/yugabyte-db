// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.yugabyte.yw.controllers.handlers.NodeAgentHandler;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.NodeAgent;
import io.swagger.annotations.Api;
import java.util.UUID;
import javax.inject.Inject;
import play.mvc.Controller;
import play.mvc.Result;

@Api(hidden = true)
public class NodeAgentPingController extends Controller {
  @Inject NodeAgentHandler nodeAgentHandler;

  /**
   * This is an unauthenticated method used by a node agent to determine the current state. It can
   * be used for recovery on error during certificate rotation.
   *
   * @param customerUuid customer UUID.
   * @param nodeAgentUuid node agent UUID.
   * @return the node agent state.
   */
  public Result getState(UUID customerUuid, UUID nodeAgentUuid) {
    NodeAgent nodeAgent = nodeAgentHandler.get(customerUuid, nodeAgentUuid);
    return PlatformResults.withData(nodeAgent.state);
  }
}
