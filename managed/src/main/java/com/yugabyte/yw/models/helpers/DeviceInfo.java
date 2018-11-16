// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.cloud.PublicCloudConstants;

public class DeviceInfo {

  // The size of each volume in each instance (if specified).
  public Integer volumeSize;

  // Number of volumes to be mounted on this instance at the default path (if specified).
  public Integer numVolumes;

  // Desired Iops for the volumes mounted on this instance (if specified).
  public Integer diskIops;

  // Name of storage class (if specified)
  public String storageClass = "standard";

  // Comma separated list of mount points for the devices in each instance (if specified).
  public String mountPoints;

  // The type of EBS volumes attached to this instance (null if instance volume type is not EBS).
  public PublicCloudConstants.EBSType ebsType;
  
  public String toString() {
    StringBuilder sb = new StringBuilder("DeviceInfo: ");
    sb.append("volSize=").append(volumeSize);
    sb.append(", numVols=").append(numVolumes);
    sb.append(", mountPoints=").append(mountPoints);
    if (ebsType != null) {
      sb.append(", ebsType=").append(ebsType);
      if (ebsType.equals(PublicCloudConstants.EBSType.IO1) && diskIops != null) {
        sb.append(", iops=").append(diskIops);
      }
    }
    return sb.toString();
  }
}
