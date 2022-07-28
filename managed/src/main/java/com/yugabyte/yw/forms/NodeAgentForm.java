// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.State;
import java.util.HashMap;
import java.util.UUID;

/** This class is used by NodeAgentController to send request payload. */
public class NodeAgentForm {
  public String name;
  public String ip;
  public String version;
  public State state;

  // Helper method to create NodeAgent.
  public NodeAgent toNodeAgent(UUID customerUuid) {
    NodeAgent nodeAgent = new NodeAgent();
    nodeAgent.customerUuid = customerUuid;
    nodeAgent.name = name;
    nodeAgent.ip = ip;
    nodeAgent.state = state;
    nodeAgent.version = version;
    nodeAgent.config = new HashMap<>();
    return nodeAgent;
  }
}
