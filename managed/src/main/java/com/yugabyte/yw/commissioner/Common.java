// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import java.util.Optional;

public class Common {
  // The various cloud types supported.
  public enum CloudType {
    unknown("unknown"),
    aws("aws", true, true, true, ConfigHelper.ConfigType.AWSRegionMetadata),
    gcp("gcp", true, true, true, ConfigHelper.ConfigType.GCPRegionMetadata),
    azu("azu", true, true, true, ConfigHelper.ConfigType.AZURegionMetadata),
    docker("docker", false, false, false, ConfigHelper.ConfigType.DockerRegionMetadata),
    onprem("onprem", true, false),
    kubernetes("kubernetes", true, false),
    local("cloud-1"),
    other("other");

    private final String value;
    private final Optional<ConfigHelper.ConfigType> regionMetadataConfigType;
    private final boolean requiresDeviceInfo;
    private final boolean requiresStorageType;
    private final boolean requiresBootstrap;

    CloudType(
        String value,
        boolean requiresDeviceInfo,
        boolean requiresStorageType,
        boolean requiresBootstrap,
        ConfigType regionMetadataConfigType) {
      this.value = value;
      this.regionMetadataConfigType = Optional.ofNullable(regionMetadataConfigType);
      this.requiresDeviceInfo = requiresDeviceInfo;
      this.requiresStorageType = requiresStorageType;
      this.requiresBootstrap = requiresBootstrap;
    }

    CloudType(String value, boolean requiresDeviceInfo, boolean requiresStorageType) {
      this(value, requiresDeviceInfo, requiresStorageType, false, null);
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

    public boolean isRequiresBootstrap() {
      return requiresBootstrap;
    }
  }
}
