// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.PRECONDITION_FAILED;

import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestorePreflightParams;
import com.yugabyte.yw.forms.RestorePreflightResponse;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageData;
import java.util.Collection;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.ybc.CloudStoreSpec;

public interface StorageUtil {

  public CloudStoreSpec createCloudStoreSpec(
      String region,
      String commonDir,
      String previousBackupLocation,
      CustomerConfigData configData);

  /**
   * Generate CloudStoreSpec for success marker download with default location
   *
   * @param cloudDir The backup location string
   * @param configData The storage config data object
   * @return The CloudStoreSpec object
   */
  public default CloudStoreSpec createDsmCloudStoreSpec(
      String backupLocation, CustomerConfigData configData) {
    return createRestoreCloudStoreSpec(
        YbcBackupUtil.DEFAULT_REGION_STRING, backupLocation, configData, true);
  }

  /**
   * Generate CloudStoreSpec for restore/success marker download
   *
   * @param region The region to generate CloudStoreSpec of. This is either 'default_region' or
   *     actual regions.
   * @param cloudDir The cloudDir string. This is dependent on spec usage type
   * @param configData The storage config data object
   * @param isDsm Boolean identifier for restore/success marker download type task
   * @return The CloudStoreSpec object
   */
  public CloudStoreSpec createRestoreCloudStoreSpec(
      String region, String cloudDir, CustomerConfigData configData, boolean isDsm);

  public Map<String, String> getRegionLocationsMap(CustomerConfigData configData);

  /**
   * Plain config validation. Mainly used pre-backup.
   *
   * @param configData The storage config data object
   */
  public default void validateStorageConfig(CustomerConfigData configData) {
    Map<String, String> configLocationMap = getRegionLocationsMap(configData);
    // TODO: Check all permissions instead of listing here.
    if (!canCredentialListObjects(
        configData, configLocationMap.values().stream().collect(Collectors.toList()))) {
      throw new PlatformServiceException(
          PRECONDITION_FAILED, "Storage config credentials cannot list objects");
    }
  }

  /**
   * Validate storage config with default locations. This is used for validating storage config with
   * default backup locations provided during restore.
   *
   * @param configData The storage config data object
   * @param locations List of default locations to check
   */
  public default void validateStorageConfigOnDefaultLocationsList(
      CustomerConfigData configData, Collection<String> locations) {
    if (CollectionUtils.isEmpty(locations)) {
      throw new RuntimeException("Empty locations list provided to validate");
    }
    locations.stream()
        .forEach(
            l ->
                checkStoragePrefixValidity(
                    ((CustomerConfigStorageData) configData).backupLocation, l));
    if (!canCredentialListObjects(configData, locations)) {
      throw new PlatformServiceException(
          PRECONDITION_FAILED, "Storage config credentials cannot list objects");
    }
  }

  /**
   * Used for storage config validation against a given backup. Utility of this is for during
   * incremental backup/backup deletion/update backup.
   *
   * @param configData The storage config data object
   * @param params The collection of backup params containing default/regional locations.
   */
  public default void validateStorageConfigOnBackup(
      CustomerConfigData configData, Collection<BackupTableParams> params) {
    Map<String, String> configLocationMap = getRegionLocationsMap(configData);
    if (CollectionUtils.isNotEmpty(params)) {
      params.stream()
          .forEach(
              bP -> {
                Map<String, String> regionLocationsMap = BackupUtil.getLocationMap(bP);
                regionLocationsMap.forEach(
                    (r, l) -> {
                      if (!configLocationMap.containsKey(r)) {
                        throw new PlatformServiceException(
                            PRECONDITION_FAILED,
                            String.format("Storage config does not contain %s region", r));
                      }
                      checkStoragePrefixValidity(configLocationMap.get(r), l);
                    });
                if (!canCredentialListObjects(
                    configData,
                    regionLocationsMap.values().stream().collect(Collectors.toList()))) {
                  throw new PlatformServiceException(
                      PRECONDITION_FAILED, "Storage config credentials cannot list objects");
                }
              });
    }
  }

  /**
   * Check if storage config can list objects in the given locations. The locations should be of the
   * exact format as present in cloud storage. For eg: s3://test/foo/bar
   *
   * @param configData
   * @param locations
   * @return
   */
  public default boolean canCredentialListObjects(
      CustomerConfigData configData, Collection<String> locations) {
    return true;
  }

  /**
   * Check if storage config can list objects for the given regionPrefixes map. This is used in case
   * of preflight validation during restores, where the success marker downloaded from backup
   * contains prefixes in form of cloudDirs. The method uses regional buckets including default
   * region from the storage config.
   *
   * <p>Ignored for NFS.
   *
   * @param configData Storage config to use. The buckets are taken from this.
   * @param regionPrefixesMap Region<->Prefixes mapping. The cloudDirs to use as prefixes while
   *     listing are taken from here.
   */
  public default void checkListObjectsWithYbcSuccessMarkerCloudStore(
      CustomerConfigData configData, YbcBackupResponse.ResponseCloudStoreSpec csSpec) {}

  /**
   * Validate similarity of bucket for S3/GCS/NFS. For AZ also check the AZ URL is same for config
   * location and backup location.
   *
   * @param configLocation The storage config location
   * @param backupLocation The actual backup location
   */
  public default void checkStoragePrefixValidity(String configLocation, String backupLocation) {
    if (!StringUtils.startsWith(backupLocation, configLocation)) {
      throw new PlatformServiceException(
          PRECONDITION_FAILED,
          String.format(
              "Matching failed for config-location %s and backup-location %s",
              configLocation, backupLocation));
    }
  }

  // Only for NFS
  public default void validateStorageConfigOnUniverseNonRpc(
      CustomerConfig config, Universe universe) {
    // default empty fall-through stub
  }

  public boolean checkFileExists(
      CustomerConfigData configData,
      Set<String> locations,
      String fileName,
      UUID universeUUID,
      boolean checkExistsOnAll);

  // Generate RestorePreflightResponse for yb_backup.py backup locations.
  public RestorePreflightResponse generateYBBackupRestorePreflightResponseWithoutBackupObject(
      RestorePreflightParams preflightParams, CustomerConfigData configData);
}
