// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.InstanceType;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import play.data.validation.Constraints;

/** This class describes the fields we need to setup an on-prem deployment. */
public class OnPremFormData {
  @Constraints.Required() public IdData identification;

  public CloudData description;

  public AccessData access;

  // Storage classes
  public static class IdData {
    public UUID uuid;
    public String name;
    public Common.CloudType type;
  }

  public static class CloudData {
    public List<RegionData> regions;
    public List<InstanceTypeData> instanceTypes;
  }

  public static class RegionData {
    public String code;
    public List<ZoneData> zones;
  }

  public static class ZoneData {
    public String code;
    public List<NodeData> nodes;
  }

  public static class NodeData {
    public String ip;
    public int sshPort = 22;
    public String instanceTypeCode;
  }

  public static class InstanceTypeData {
    public String code;
    public List<InstanceType.VolumeDetails> volumeDetailsList = new ArrayList<>();
  }

  // TODO: placeholder
  public static class AccessData {}
}
