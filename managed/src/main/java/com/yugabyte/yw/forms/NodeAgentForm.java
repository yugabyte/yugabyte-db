// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import java.util.UUID;
import play.data.validation.Constraints;

/** This class is used by NodeAgentController to send request payload. */
public class NodeAgentForm {
  @Constraints.Required public String name;
  @Constraints.Required public String ip;
  @Constraints.Required public String version;
  @Constraints.Required public String archType;
  @Constraints.Required public String osType;
  @Constraints.Required public String home;
  public String state;
  @Constraints.Required public int port;

  // Helper method to create NodeAgent.
  public NodeAgent toNodeAgent(UUID customerUuid) {
    NodeAgent nodeAgent = new NodeAgent();
    nodeAgent.setCustomerUuid(customerUuid);
    nodeAgent.setName(name);
    nodeAgent.setIp(ip);
    nodeAgent.setPort(port);
    nodeAgent.setVersion(version);
    nodeAgent.setHome(home);
    nodeAgent.setArchType(ArchType.parse(archType));
    nodeAgent.setOsType(OSType.parse(osType));
    nodeAgent.setConfig(new NodeAgent.Config());
    return nodeAgent;
  }
}
