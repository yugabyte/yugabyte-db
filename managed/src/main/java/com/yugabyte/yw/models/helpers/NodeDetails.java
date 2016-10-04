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

  // Possible states in which this node can exist.
  public enum NodeState {
    // Set when a new node needs to be added into a Universe and has not yet been created.
    ToBeAdded,
    // Set after the node (without any configuration) is created using the IaaS provider at the
    // end of the provision step.
    Provisioned,
    // Set after the YB software installed and some basic configuration done on a provisioned node.
    SoftwareInstalled,
    // Set after all the services (master, tserver, etc) on a node are started and successfully
    // running.
    Running,
    // Set when a node is marked for removal. Note that we will wait to get all its data out during
    // edit universe.
    ToBeDecommissioned,
    // Set just before sending the request to the IaaS provider to terminate this node.
    BeingDecommissioned,
    // Set after the node has been decomissioned.
    Destroyed
  }

  // The current state of the node. 
  public NodeState state;

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
    sb.append("name: ").append(nodeName).append(".")
      .append(cloudInfo.toString())
      .append(", isMaster: ").append(isMaster)
      .append(", isTserver: ").append(isTserver);
    return sb.toString();
  }
}
