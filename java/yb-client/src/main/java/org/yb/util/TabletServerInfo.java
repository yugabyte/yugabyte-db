// Copyright (c) YugaByte, Inc.

package org.yb.util;

import com.google.common.net.HostAndPort;
import java.util.UUID;

public class TabletServerInfo {

  private UUID uuid;

  private boolean inPrimaryCluster;

  private HostAndPort privateRpcAddress;

  private HostAndPort broadcastAddress;

  private HostAndPort httpAddress;

  private UUID placementUuid;

  private CloudInfo cloudInfo;

  public HostAndPort getPrivateAddress() {
    return broadcastAddress == null ? privateRpcAddress : broadcastAddress;
  }

  public static class CloudInfo {
    private String cloud;

    private String region;

    private String zone;

    public String getCloud() {
      return this.cloud;
    }

    public void setCloud(String cloud) {
      this.cloud = cloud;
    }

    public String getRegion() {
      return this.region;
    }

    public void setRegion(String region) {
      this.region = region;
    }

    public String getZone() {
      return this.zone;
    }

    public void setZone(String zone) {
      this.zone = zone;
    }
  }

  public UUID getUuid() {
    return this.uuid;
  }

  public void setUuid(UUID uuid) {
    this.uuid = uuid;
  }

  public boolean isInPrimaryCluster() {
    return this.inPrimaryCluster;
  }

  public boolean getInPrimaryCluster() {
    return this.inPrimaryCluster;
  }

  public void setInPrimaryCluster(boolean inPrimaryCluster) {
    this.inPrimaryCluster = inPrimaryCluster;
  }

  public HostAndPort getPrivateRpcAddress() {
    return this.privateRpcAddress;
  }

  public void setPrivateRpcAddress(HostAndPort privateRpcAddress) {
    this.privateRpcAddress = privateRpcAddress;
  }

  public HostAndPort getBroadcastAddress() {
    return this.broadcastAddress;
  }

  public void setBroadcastAddress(HostAndPort broadcastAddress) {
    this.broadcastAddress = broadcastAddress;
  }

  public HostAndPort getHttpAddress() {
    return this.httpAddress;
  }

  public void setHttpAddress(HostAndPort httpAddress) {
    this.httpAddress = httpAddress;
  }

  public UUID getPlacementUuid() {
    return this.placementUuid;
  }

  public void setPlacementUuid(UUID placementUuid) {
    this.placementUuid = placementUuid;
  }

  public CloudInfo getCloudInfo() {
    return this.cloudInfo;
  }

  public void setCloudInfo(CloudInfo cloudInfo) {
    this.cloudInfo = cloudInfo;
  }
}
