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

  // Desired throughput for the volumes mounted on this instance (if specified).
  public Integer throughput;

  // Name of storage class (if specified)
  public String storageClass = "standard";

  // Comma separated list of mount points for the devices in each instance (if specified).
  public String mountPoints;

  // The type of storage used for this instance (null if instance volume type is not EBS).
  public PublicCloudConstants.StorageType storageType;

  public String toString() {
    StringBuilder sb = new StringBuilder("DeviceInfo: ");
    sb.append("volSize=").append(volumeSize);
    sb.append(", numVols=").append(numVolumes);
    sb.append(", mountPoints=").append(mountPoints);
    if (storageType != null) {
      sb.append(", storageType=").append(storageType);
      if (storageType.isIopsProvisioning() && diskIops != null) {
        sb.append(", iops=").append(diskIops);
      }
      if (storageType.isThroughputProvisioning() && throughput != null) {
        sb.append(", throughput=").append(throughput);
      }
    }
    return sb.toString();
  }
}
