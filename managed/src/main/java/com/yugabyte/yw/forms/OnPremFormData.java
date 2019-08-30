// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import java.util.ArrayList;
import java.util.UUID;
import java.util.List;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.InstanceType;
import play.data.validation.Constraints;

/**
 * This class describes the fields we need to setup an on-prem deployment.
 */
public class OnPremFormData {
  @Constraints.Required()
  public IdData identification;

  public CloudData description;

  public AccessData access;

  // Storage classes
  static public class IdData {
    public UUID uuid;
    public String name;
    public Common.CloudType type;
  }

  static public class CloudData {
    public List<RegionData> regions;
    public List<InstanceTypeData> instanceTypes;
  }

  static public class RegionData {
    public String code;
    public List<ZoneData> zones;
  }

  static public class ZoneData {
    public String code;
    public List<NodeData> nodes;
  }

  static public class NodeData {
    public String ip;
    public int sshPort = 22;
    public String instanceTypeCode;
  }

  static public class InstanceTypeData {
    public String code;
    public List<InstanceType.VolumeDetails> volumeDetailsList = new ArrayList<>();
  }

  // TODO: placeholder
  static public class AccessData {
  }
}
