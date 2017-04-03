// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

public class DeviceInfo {

  // The size of each volume in each instance (if specified).
  public Integer volumeSize;

  // Number of volumes to be mounted on this instance at the default path (if specified).
  public Integer numVolumes;

  // Desired Iops for the volumes mounted on this instance (if specified).
  public Integer diskIops;

  // Comma separated list of mount points for the devices in each instance (if specified).
  public String mountPoints;
  
  public String toString() {
    return "DeviceInfo:" + " volSize=" + volumeSize + ", numVols=" + numVolumes + ", iops=" +
           diskIops + ", mountPoints=" + mountPoints;
      
  }
}
