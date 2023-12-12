// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.forms;

import org.yb.util.ServerInfo;

public class MasterNodesInfo {

  // The UUID of the node.
  public String masterUUID;

  // The port on which master process of the node is running.
  public int port;

  // Indicates if the current master node is the leader.
  public Boolean isLeader;

  // The IP of the node.
  public String host;

  public MasterNodesInfo(ServerInfo serverInfo) {
    this.masterUUID = serverInfo.getUuid();
    this.port = serverInfo.getPort();
    this.isLeader = serverInfo.isLeader();
    this.host = serverInfo.getHost();
  }
}
