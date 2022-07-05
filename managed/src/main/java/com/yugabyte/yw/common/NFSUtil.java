// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Singleton;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import java.util.HashMap;
import java.util.Map;
import org.yb.ybc.CloudStoreSpec;

@Singleton
public class NFSUtil implements StorageUtil {

  public static final String DEFAULT_YUGABYTE_NFS_BUCKET = "yugabyte_backup";
  private static final String YBC_NFS_DIR_FIELDNAME = "YBC_NFS_DIR";

  @Override
  // backupLocation parameter is unused here.
  public CloudStoreSpec createCloudStoreSpec(
      String backupLocation, String commonDir, CustomerConfigData configData) {
    String cloudDir = commonDir + "/";
    String bucket = DEFAULT_YUGABYTE_NFS_BUCKET;
    Map<String, String> credsMap = createCredsMapYbc(configData);
    return YbcBackupUtil.buildCloudStoreSpec(bucket, cloudDir, credsMap, Util.NFS);
  }

  private Map<String, String> createCredsMapYbc(CustomerConfigData configData) {
    CustomerConfigStorageNFSData nfsData = (CustomerConfigStorageNFSData) configData;
    Map<String, String> nfsMap = new HashMap<>();
    nfsMap.put(YBC_NFS_DIR_FIELDNAME, nfsData.backupLocation);
    return nfsMap;
  }
}
