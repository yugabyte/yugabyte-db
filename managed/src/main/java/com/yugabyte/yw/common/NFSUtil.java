// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.ybc.YbcBackupUtil;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashMap;
import java.util.Map;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.ybc.CloudStoreSpec;

@Singleton
public class NFSUtil implements StorageUtil {

  public static final String DEFAULT_YUGABYTE_NFS_BUCKET = "yugabyte_backup";
  private static final String YBC_NFS_DIR_FIELDNAME = "YBC_NFS_DIR";

  @Inject NodeUniverseManager nodeUniverseManager;

  @Override
  // storageLocation parameter is unused here.
  public CloudStoreSpec createCloudStoreSpec(
      String storageLocation,
      String commonDir,
      String previousBackupLocation,
      CustomerConfigData configData) {
    String cloudDir = BackupUtil.appendSlash(commonDir);
    String bucket = ((CustomerConfigStorageNFSData) configData).nfsBucket;
    String previousCloudDir = "";
    if (StringUtils.isNotBlank(previousBackupLocation)) {
      previousCloudDir =
          BackupUtil.appendSlash(BackupUtil.getBackupIdentifier(previousBackupLocation, true));
    }
    Map<String, String> credsMap = createCredsMapYbc(storageLocation);
    return YbcBackupUtil.buildCloudStoreSpec(
        bucket, cloudDir, previousCloudDir, credsMap, Util.NFS);
  }

  @Override
  public CloudStoreSpec createRestoreCloudStoreSpec(
      String storageLocation, String cloudDir, CustomerConfigData configData, boolean isDsm) {
    String bucket = ((CustomerConfigStorageNFSData) configData).nfsBucket;
    Map<String, String> credsMap = new HashMap<>();
    if (isDsm) {
      String location =
          getNfsLocationString(
              storageLocation, bucket, ((CustomerConfigStorageData) configData).backupLocation);
      location = BackupUtil.appendSlash(location);
      credsMap = createCredsMapYbc(((CustomerConfigStorageData) configData).backupLocation);
      return YbcBackupUtil.buildCloudStoreSpec(bucket, location, "", credsMap, Util.NFS);
    }
    credsMap = createCredsMapYbc(storageLocation);
    return YbcBackupUtil.buildCloudStoreSpec(bucket, cloudDir, "", credsMap, Util.NFS);
  }

  private Map<String, String> createCredsMapYbc(String storageLocation) {
    Map<String, String> nfsMap = new HashMap<>();
    nfsMap.put(YBC_NFS_DIR_FIELDNAME, storageLocation);
    return nfsMap;
  }

  public Map<String, String> getRegionLocationsMap(CustomerConfigData configData) {
    Map<String, String> regionLocationsMap = new HashMap<>();
    CustomerConfigStorageNFSData nfsData = (CustomerConfigStorageNFSData) configData;
    if (CollectionUtils.isNotEmpty(nfsData.regionLocations)) {
      nfsData.regionLocations.stream()
          .forEach(rL -> regionLocationsMap.put(rL.region, rL.location));
    }
    return regionLocationsMap;
  }

  public void validateDirectory(CustomerConfigData customerConfigData, Universe universe) {
    for (NodeDetails node : universe.getTServersInPrimaryCluster()) {
      String backupDirectory = ((CustomerConfigStorageNFSData) customerConfigData).backupLocation;
      if (!backupDirectory.endsWith("/")) {
        backupDirectory += "/";
      }
      if (!nodeUniverseManager.isDirectoryWritable(backupDirectory, node, universe)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "NFS Storage Config Location: "
                + backupDirectory
                + " is not writable in Node: "
                + node.getNodeName());
      }
    }
  }

  private String getNfsLocationString(String backupLocation, String bucket, String configLocation) {
    String location =
        StringUtils.removeStart(
            backupLocation, BackupUtil.getCloudpathWithConfigSuffix(configLocation, bucket));
    location = StringUtils.removeStart(location, "/");
    return location;
  }

  @Override
  public void validateStorageConfigOnUniverse(CustomerConfig config, Universe universe) {
    validateDirectory(config.getDataObject(), universe);
  }
}
