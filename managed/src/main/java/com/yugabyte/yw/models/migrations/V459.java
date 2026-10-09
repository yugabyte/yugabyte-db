// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.migrations;

import com.yugabyte.yw.commissioner.Common;
import java.util.ArrayList;
import java.util.List;
import lombok.AllArgsConstructor;
import lombok.NoArgsConstructor;

/** Snapshot view of universe details fields needed by migration V459. */
public class V459 {

  @NoArgsConstructor
  public static class UniverseDefinitionTaskParams {
    public List<Cluster> clusters = new ArrayList<>();
  }

  public enum ClusterType {
    PRIMARY,
    ASYNC
  }

  @NoArgsConstructor
  @AllArgsConstructor
  public static class Cluster {
    public ClusterType clusterType;
    public UserIntent userIntent;
  }

  @NoArgsConstructor
  public static class UserIntent {
    public boolean dedicatedNodes;
    public Common.CloudType providerType;
    public String instanceType;
    public DeviceInfo deviceInfo;
    public DeviceInfo masterDeviceInfo;
    public String masterInstanceType;
  }

  @NoArgsConstructor
  @AllArgsConstructor
  public static class DeviceInfo {
    public Integer numVolumes;
    public Integer volumeSize;
  }
}
