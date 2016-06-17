// Copyright (c) YugaByte, Inc.

package org.yb.util;

// Class to track common info provided by master or tablet server. 
public class ServerInfo {
  private String uuid;
  private String host;
  private int port;
  // Note: Need not be set when there is no leader (eg., when all tablet servers are listed). 
  private boolean isLeader;

  public ServerInfo(String uuid, String host, int port, boolean isLeader) {
    this.uuid = uuid;
    this.host = host;
    this.port = port;
    this.isLeader = isLeader;
  }

  public String getHost() {
    return host;
  }

  public int getPort() {
    return port;
  }

  public String getUuid() {
    return uuid;
  }

  public boolean isLeader() {
    return isLeader;
  }
}
