// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.common.utils.Pair;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Objects;

@ApiModel(description = "Device information")
public class DeviceInfo {

  // The size of each volume in each instance (if specified).
  @ApiModelProperty(
      value =
          "The size of each volume in each instance. "
              + "Could be modified in payload for /resize_node API call")
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

  @Override
  public String toString() {
    StringBuilder sb = new StringBuilder("DeviceInfo: ");
    sb.append("volSize=").append(volumeSize);
    sb.append(", numVols=").append(numVolumes);
    sb.append(", mountPoints=").append(mountPoints);
    if (storageType != null) {
      sb.append(", storageType=").append(storageType);
      if (diskIops != null && storageType.isIopsProvisioning()) {
        sb.append(", iops=").append(diskIops);
      }
      if (throughput != null && storageType.isThroughputProvisioning()) {
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

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    DeviceInfo that = (DeviceInfo) o;
    return Objects.equals(volumeSize, that.volumeSize)
        && Objects.equals(numVolumes, that.numVolumes)
        && Objects.equals(diskIops, that.diskIops)
        && Objects.equals(throughput, that.throughput)
        && Objects.equals(mountPoints, that.mountPoints)
        && storageType == that.storageType;
  }

  @Override
  public int hashCode() {
    return Objects.hash(volumeSize, numVolumes, diskIops, throughput, mountPoints, storageType);
  }

  private void checkVolumeBaseInfo() {
    RuntimeConfGetter runtimeConfGetter =
        StaticInjectorHolder.injector().instanceOf(RuntimeConfGetter.class);
    int maxVolumeCount = runtimeConfGetter.getGlobalConf(GlobalConfKeys.maxVolumeCount);
    if (volumeSize == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Volume size field is mandatory");
    } else if (volumeSize <= 0) {
      throw new PlatformServiceException(BAD_REQUEST, "Volume size should be positive");
    }

    if (numVolumes == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Number of volumes field is mandatory");
    } else if (numVolumes <= 0) {
      throw new PlatformServiceException(BAD_REQUEST, "Number of volumes should be positive");
    } else if (numVolumes > maxVolumeCount) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Volume number should not exceed maximum limit of " + maxVolumeCount);
    }
  }

  private void checkDiskIops() {
    if (storageType == null) {
      return;
    }
    if (storageType.isIopsProvisioning()) {
      if (diskIops == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Disk IOPS is mandatory for " + storageType.name() + " storage");
      }
      Pair<Integer, Integer> iopsRange = storageType.getIopsRange();
      if (diskIops < iopsRange.getFirst() || diskIops > iopsRange.getSecond()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Disk IOPS for storage type %s should be in range [%d, %d]",
                storageType.name(), iopsRange.getFirst(), iopsRange.getSecond()));
      }
    }
  }

  private void checkThroughput() {
    if (storageType == null) {
      return;
    }
    if (storageType.isThroughputProvisioning()) {
      if (throughput == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Disk throughput is mandatory for " + storageType.name() + " storage");
      }
      Pair<Integer, Integer> throughputRange = storageType.getThroughputRange();
      if (throughput < throughputRange.getFirst() || throughput > throughputRange.getSecond()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Disk throughput for storage type %s should be in range [%d, %d]",
                storageType.name(), throughputRange.getFirst(), throughputRange.getSecond()));
      }
    }
  }
}
