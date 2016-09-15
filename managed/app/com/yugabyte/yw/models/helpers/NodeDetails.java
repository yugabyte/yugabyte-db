// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import java.util.UUID;

/**
 * Represents all the details of a cloud node that are of interest.
 */
public class NodeDetails {
  // The id of the node. This is usually present in the node name.
  public int nodeIdx = -1;
  // Name of the node.
  public String nodeName;

  // Information about the node that is returned by the cloud provider.
  public CloudSpecificInfo cloudInfo;

  // The AZ UUID (the YB UUID for the AZ) into which the node is deployed.
  public UUID azUuid;

  // True if this node is a master, along with port info.
  public boolean isMaster;
  public int masterHttpPort = 7000;
  public int masterRpcPort = 7100;

  // True if this node is a tserver, along with port info.
  public boolean isTserver;
  public int tserverHttpPort = 9000;
  public int tserverRpcPort = 9100;

  @Override
  public String toString() {
    StringBuilder sb = new StringBuilder();
    sb.append("name: ").append(nodeName)
      .append(cloudInfo.toString())
      .append(", isMaster: ").append(isMaster)
      .append(", isTserver: ").append(isTserver);
    return sb.toString();
  }
}
