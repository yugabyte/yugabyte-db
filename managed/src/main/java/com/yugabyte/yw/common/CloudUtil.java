// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.BackupUtil.PerLocationBackupInfo;
import com.yugabyte.yw.forms.RestorePreflightResponse;
import com.yugabyte.yw.forms.backuprestore.AdvancedRestorePreflightParams;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.ProxyConfig;
import java.io.InputStream;
import java.nio.file.Path;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.AllArgsConstructor;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.ybc.ProxySpec;

public interface CloudUtil extends StorageUtil {

  @AllArgsConstructor
  public static class CloudLocationInfo {
    public String bucket;
    public String cloudPath;
  }

  public static enum ExtraPermissionToValidate {
    READ,
    LIST,
    NULL
  }

  public static enum Protocol {
    TCP,
    HTTP
  }

  public static final String KEY_LOCATION_SUFFIX = Util.KEY_LOCATION_SUFFIX;
  public static final String SUCCESS = "success";
  int FILE_DOWNLOAD_BUFFER_SIZE = 8 * 1024;

  public static final String DUMMY_DATA = "dummy-text";

  public CloudLocationInfo getCloudLocationInfo(
      String region, CustomerConfigData configData, String backupLocation);

  public boolean deleteKeyIfExists(CustomerConfigData configData, String defaultBackupLocation);

  public boolean deleteStorage(
      CustomerConfigData configData, Map<String, List<String>> backupRegionLocationsMap);

  public <T> T listBuckets(CustomerConfigData configData);

  public InputStream getCloudFileInputStream(CustomerConfigData configData, String cloudPath)
      throws Exception;

  default void downloadCloudFile(CustomerConfigData configData, String cloudPath, Path destination)
      throws Exception {
    FileUtils.copyInputStreamToFile(
        getCloudFileInputStream(configData, cloudPath), destination.toFile());
  }

  public default boolean checkFileExists(
      CustomerConfigData configData,
      Set<String> locations,
      String fileName,
      UUID universeUUID,
      boolean checkExistsOnAll) {
    return checkFileExists(configData, locations, fileName, checkExistsOnAll);
  }

  /**
   * Check if file exists on given locations set.
   *
   * @param configData The CustomerConfigData object
   * @param locations The set of locations to test file presence
   * @param fileName The file name to check on all locations
   * @param checkExistsOnAll Check whether this function should behave like AND or OR
   * @return A boolean of the check result
   */
  public boolean checkFileExists(
      CustomerConfigData configData,
      Set<String> locations,
      String fileName,
      boolean checkExistsOnAll);

  default void validate(CustomerConfigData configData, List<ExtraPermissionToValidate> permissions)
      throws Exception {
    // default fall through stub
  }

  public default boolean shouldUseHttpsProxy(CustomerConfigData configData) {
    return true;
  }

  public default ProxySpec getOldProxySpec(CustomerConfigData configData) {
    return null;
  }

  default UUID getRandomUUID() {
    return UUID.randomUUID();
  }

  public default RestorePreflightResponse
      generateYBBackupRestorePreflightResponseWithoutBackupObject(
          AdvancedRestorePreflightParams preflightParams, CustomerConfigData configData) {
    RestorePreflightResponse.RestorePreflightResponseBuilder preflightResponseBuilder =
        RestorePreflightResponse.builder();
    boolean isSelectiveRestoreSupported = false;

    if (!checkFileExists(
        configData, preflightParams.getBackupLocations(), BackupUtil.SNAPSHOT_PB, true)) {
      throw new RuntimeException("No snapshot export file 'SnapshotInfoPB' found: Bad backup.");
    }
    preflightResponseBuilder.backupCategory(BackupCategory.YB_BACKUP_SCRIPT);

    // For backup_keys.json, check one-level up.
    if (checkFileExists(
        configData,
        /* Go to parent directory for backup_keys.json file. */
        preflightParams.getBackupLocations().parallelStream()
            .map(bL -> bL.substring(0, bL.lastIndexOf('/')))
            .collect(Collectors.toSet()),
        BackupUtil.BACKUP_KEYS_JSON,
        false)) {
      preflightResponseBuilder.hasKMSHistory(true);
    }
    Map<String, PerLocationBackupInfo> perLocationBackupInfoMap = new HashMap<>();
    preflightParams.getBackupLocations().stream()
        .forEach(
            bL -> {
              PerLocationBackupInfo.PerLocationBackupInfoBuilder perLocationBackupInfoBuilder =
                  PerLocationBackupInfo.builder();
              perLocationBackupInfoBuilder.isSelectiveRestoreSupported(isSelectiveRestoreSupported);
              if (checkFileExists(
                  configData, Collections.singleton(bL), BackupUtil.YSQL_DUMP, true)) {
                perLocationBackupInfoBuilder.isYSQLBackup(true);
              }
              perLocationBackupInfoMap.put(bL, perLocationBackupInfoBuilder.build());
            });
    preflightResponseBuilder.perLocationBackupInfoMap(perLocationBackupInfoMap);
    return preflightResponseBuilder.build();
  }

  @Override
  public default org.yb.ybc.ProxyConfig createYbcProxyConfig(
      Universe universe, CustomerConfigData configData) {
    boolean useHttpsProxy = shouldUseHttpsProxy(configData);
    Map<NodeDetails, ProxyConfig> nodeProxyMap = universe.getNodeProxyConfigMap();
    Map<String, ProxySpec> pSpecMap =
        nodeProxyMap.entrySet().parallelStream()
            .filter(e -> (e.getKey().cloudInfo.private_ip != null))
            .filter(
                e ->
                    (e.getValue() != null)
                        && (useHttpsProxy
                            ? StringUtils.isNotBlank(e.getValue().getHttpsProxy())
                            : StringUtils.isNotBlank(e.getValue().getHttpProxy())))
            .collect(
                Collectors.toMap(
                    e -> e.getKey().cloudInfo.private_ip,
                    e -> e.getValue().getYbcProxySpec(useHttpsProxy)));
    if (MapUtils.isNotEmpty(pSpecMap)) {
      return org.yb.ybc.ProxyConfig.newBuilder().putAllNodeProxyMap(pSpecMap).build();
    }
    // Old flow
    ProxySpec pSpec = getOldProxySpec(configData);
    if (pSpec != null) {
      return org.yb.ybc.ProxyConfig.newBuilder().setDefaultProxy(pSpec).build();
    }
    return null;
  }
}
