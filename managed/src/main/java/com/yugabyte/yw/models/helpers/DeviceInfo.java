// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.common.PlatformServiceException;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;

@ApiModel(description = "Device information")
public class DeviceInfo {

  // The size of each volume in each instance (if specified).
  @ApiModelProperty(value = "The size of each volume in each instance")
  public Integer volumeSize;

  // Number of volumes to be mounted on this instance at the default path (if specified).
  @ApiModelProperty(value = "Number of volumes to be mounted on this instance at the default path")
  public Integer numVolumes;

  // Desired Iops for the volumes mounted on this instance (if specified).
  @ApiModelProperty(value = "Desired IOPS for the volumes mounted on this instance")
  public Integer diskIops;

  // Desired throughput for the volumes mounted on this instance (if specified).
  @ApiModelProperty(value = "Desired throughput for the volumes mounted on this instance")
  public Integer throughput;

  // Name of storage class (if specified)
  @ApiModelProperty(value = "Name of the storage class")
  public String storageClass = "";

  // Comma separated list of mount points for the devices in each instance (if specified).
  @ApiModelProperty(value = "Comma-separated list of mount points for the devices in each instance")
  public String mountPoints;

  // The type of storage used for this instance (null if instance volume type is not EBS).
  @ApiModelProperty(value = "Storage type used for this instance")
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

  public void validate() {
    checkVolumeBaseInfo();
    checkDiskIops();
    checkThroughput();
  }

  public DeviceInfo clone() {
    DeviceInfo result = new DeviceInfo();
    result.storageType = storageType;
    result.numVolumes = numVolumes;
    result.mountPoints = mountPoints;
    result.volumeSize = volumeSize;
    result.diskIops = diskIops;
    result.storageClass = storageClass;
    result.throughput = throughput;
    return result;
  }

  private void checkVolumeBaseInfo() {
    if (volumeSize == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Volume size field is mandatory");
    } else if (volumeSize <= 0) {
      throw new PlatformServiceException(BAD_REQUEST, "Volume size should be positive");
    }
    if (numVolumes == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Number of volumes field is mandatory");
    } else if (numVolumes <= 0) {
      throw new PlatformServiceException(BAD_REQUEST, "Number of volumes should be positive");
    }
  }

  private void checkDiskIops() {
    if (storageType == null) {
      return;
    }
    if (diskIops == null) {
      if (storageType.isIopsProvisioning()) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Disk IOPS is mandatory for " + storageType.name() + " storage");
      }
    } else if (diskIops <= 0) {
      throw new PlatformServiceException(BAD_REQUEST, "Disk IOPS should be positive");
    }
  }

  private void checkThroughput() {
    if (storageType == null) {
      return;
    }
    if (throughput == null) {
      if (storageType.isThroughputProvisioning()) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Disk throughput is mandatory for " + storageType.name() + " storage");
      }
    } else if (throughput <= 0) {
      throw new PlatformServiceException(BAD_REQUEST, "Disk throughput should be positive");
    }
  }
}
