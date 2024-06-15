// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import java.util.Optional;

public class Common {
  // The various cloud types supported.
  public enum CloudType {
    unknown("unknown"),
    aws("aws", true, true, true, ConfigHelper.ConfigType.AWSRegionMetadata, "ec2-user"),
    gcp("gcp", true, true, true, ConfigHelper.ConfigType.GCPRegionMetadata, "centos"),
    azu("azu", true, true, true, ConfigHelper.ConfigType.AZURegionMetadata, "centos"),
    docker("docker", false, false, false, ConfigHelper.ConfigType.DockerRegionMetadata),
    onprem("onprem", true, false, true, null),
    kubernetes("kubernetes", true, false, true, null),
    local("local"),
    other("other");

    private final String value;
    private final Optional<ConfigHelper.ConfigType> regionMetadataConfigType;
    private final boolean requiresDeviceInfo;
    private final boolean requiresStorageType;
    private final boolean requiresBootstrap;
    private final String defaultSshUser;

    CloudType(
        String value,
        boolean requiresDeviceInfo,
        boolean requiresStorageType,
        boolean requiresBootstrap,
        ConfigType regionMetadataConfigType,
        String defaultSshUser) {
      this.value = value;
      this.regionMetadataConfigType = Optional.ofNullable(regionMetadataConfigType);
      this.requiresDeviceInfo = requiresDeviceInfo;
      this.requiresStorageType = requiresStorageType;
      this.requiresBootstrap = requiresBootstrap;
      this.defaultSshUser = defaultSshUser;
    }

    CloudType(
        String value,
        boolean requiresDeviceInfo,
        boolean requiresStorageType,
        boolean requiresBootstrap,
        ConfigType regionMetadataConfigType) {
      this(
          value,
          requiresDeviceInfo,
          requiresStorageType,
          requiresBootstrap,
          regionMetadataConfigType,
          null);
    }

    CloudType(String value, boolean requiresDeviceInfo, boolean requiresStorageType) {
      this(value, requiresDeviceInfo, requiresStorageType, false, null, null);
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

    public boolean canAddRegions() {
      return true;
    }

    public boolean isHostedZoneEnabled() {
      return this == aws || this == azu;
    }

    public String getSshUser() {
      return defaultSshUser;
    }

    public boolean enforceInstanceTags() {
      return this == aws || this == azu || this == gcp;
    }

    public boolean imageBundleSupported() {
      return this == aws || this == azu || this == gcp;
    }

    public boolean regionBootstrapSupported() {
      return this == aws || this == azu || this == gcp;
    }

    public boolean isPublicCloud() {
      return this == aws || this == azu || this == gcp;
    }
  }
}
