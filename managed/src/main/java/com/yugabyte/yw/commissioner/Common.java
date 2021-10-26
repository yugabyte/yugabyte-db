// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.common.ConfigHelper;
import java.util.Optional;

public class Common {
  // The various cloud types supported.
  public enum CloudType {
    unknown("unknown"),
    aws("aws", true, true, ConfigHelper.ConfigType.AWSRegionMetadata),
    gcp("gcp", true, true, ConfigHelper.ConfigType.GCPRegionMetadata),
    azu("azu", true, true, ConfigHelper.ConfigType.AZURegionMetadata),
    docker("docker", false, false, ConfigHelper.ConfigType.DockerRegionMetadata),
    onprem("onprem", true, false),
    kubernetes("kubernetes", true, false),
    local("cloud-1"),
    other("other");

    private final String value;
    private final Optional<ConfigHelper.ConfigType> regionMetadataConfigType;
    private final boolean requiresDeviceInfo;
    private final boolean requiresStorageType;

    CloudType(
        String value,
        boolean requiresDeviceInfo,
        boolean requiresStorageType,
        ConfigHelper.ConfigType regionMetadataConfigType) {
      this.value = value;
      this.regionMetadataConfigType = Optional.ofNullable(regionMetadataConfigType);
      this.requiresDeviceInfo = requiresDeviceInfo;
      this.requiresStorageType = requiresStorageType;
    }

    CloudType(String value, boolean requiresDeviceInfo, boolean requiresStorageType) {
      this(value, requiresDeviceInfo, requiresStorageType, null);
    }

    CloudType(String value) {
      this(value, false, false);
    }

    public Optional<ConfigHelper.ConfigType> getRegionMetadataConfigType() {
      return this.regionMetadataConfigType;
    }

    public boolean isVM() {
      return this != CloudType.kubernetes;
    }

    public boolean isRequiresDeviceInfo() {
      return requiresDeviceInfo;
    }

    public boolean isRequiresStorageType() {
      return requiresStorageType;
    }

    public String toString() {
      return this.value;
    }
  }
}
