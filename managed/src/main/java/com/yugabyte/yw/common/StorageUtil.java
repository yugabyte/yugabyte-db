// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.PRECONDITION_FAILED;

import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.common.ybc.YbcBackupUtil;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageData;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import org.apache.commons.collections.MapUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.ybc.CloudStoreSpec;

public interface StorageUtil {

  public CloudStoreSpec createCloudStoreSpec(
      String backupLocation,
      String commonDir,
      String previousBackupLocation,
      CustomerConfigData configData);

  public CloudStoreSpec createRestoreCloudStoreSpec(
      String storageLocation, String cloudDir, CustomerConfigData configData, boolean isDsm);

  public Map<String, String> getRegionLocationsMap(CustomerConfigData configData);

  public default void validateStorageConfigOnLocations(CustomerConfigData configData) {
    validateStorageConfigOnLocations(configData, null);
  }

  public default void validateStorageConfigOnLocations(
      CustomerConfigData configData, Map<String, String> keyspaceLocationMap) {
    Map<String, String> configLocationMap = getRegionLocationsMap(configData);
    configLocationMap.put(
        YbcBackupUtil.DEFAULT_REGION_STRING,
        ((CustomerConfigStorageData) configData).backupLocation);
    if (MapUtils.isNotEmpty(keyspaceLocationMap)) {
      keyspaceLocationMap.forEach(
          (r, l) -> {
            if (!configLocationMap.containsKey(r)) {
              throw new PlatformServiceException(
                  PRECONDITION_FAILED,
                  String.format("Storage config does not contain %s region", r));
            }
            checkStoragePrefixValidity(configLocationMap.get(r), l);
            if (!canCredentialListObjects(
                configData, keyspaceLocationMap.values().stream().collect(Collectors.toList()))) {
              throw new PlatformServiceException(
                  PRECONDITION_FAILED, "Storage config credentials cannot list objects");
            }
          });
    } else if (!canCredentialListObjects(
        configData, configLocationMap.values().stream().collect(Collectors.toList()))) {
      throw new PlatformServiceException(
          PRECONDITION_FAILED, "Storage config credentials cannot list objects");
    }
  }

  public default boolean canCredentialListObjects(
      CustomerConfigData configData, List<String> locations) {
    return true;
  }

  // Suffice for Azure and NFS, because backup location should have exact match with backup prefix.
  public default void checkStoragePrefixValidity(String configLocation, String backupLocation) {
    if (!StringUtils.startsWith(backupLocation, configLocation)) {
      throw new PlatformServiceException(
          PRECONDITION_FAILED,
          String.format(
              "Matching failed for config-location %s and backup-location %s",
              configLocation, backupLocation));
    }
  }

  public static <T extends StorageUtil> T getStorageUtil(String configType) {
    switch (configType) {
      case Util.S3:
        return (T) StaticInjectorHolder.injector().instanceOf(AWSUtil.class);
      case Util.GCS:
        return (T) StaticInjectorHolder.injector().instanceOf(GCPUtil.class);
      case Util.AZ:
        return (T) StaticInjectorHolder.injector().instanceOf(AZUtil.class);
      case Util.NFS:
        return (T) StaticInjectorHolder.injector().instanceOf(NFSUtil.class);
      default:
        throw new PlatformServiceException(BAD_REQUEST, "Unsupported storage type");
    }
  }

  public default void validateStorageConfigOnUniverse(CustomerConfig config, Universe universe) {
    // default empty fall-through stub
  }
}
