// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import java.util.HashMap;
import java.util.UUID;
import play.data.validation.Constraints;

/** This class is used by NodeAgentController to send request payload. */
public class NodeAgentForm {
  @Constraints.Required public String name;
  @Constraints.Required public String ip;
  @Constraints.Required public String version;
  @Constraints.Required public String archType;
  @Constraints.Required public String osType;
  public String state;
  @Constraints.Required public int port;

  // Helper method to create NodeAgent.
  public NodeAgent toNodeAgent(UUID customerUuid) {
    NodeAgent nodeAgent = new NodeAgent();
    nodeAgent.customerUuid = customerUuid;
    nodeAgent.name = name;
    nodeAgent.ip = ip;
    nodeAgent.port = port;
    nodeAgent.version = version;
    nodeAgent.archType = ArchType.parse(archType);
    nodeAgent.osType = OSType.parse(osType);
    nodeAgent.config = new HashMap<>();
    return nodeAgent;
  }
}
