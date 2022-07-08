// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.YbcClient;
import org.yb.ybc.BackupServiceNfsDirDeleteRequest;
import org.yb.ybc.BackupServiceNfsDirDeleteResponse;
import org.yb.ybc.ControllerStatus;

@Singleton
public class YbcManager {

  private static final Logger LOG = LoggerFactory.getLogger(YbcManager.class);

  private final YbcClientService ybcClientService;

  private final CustomerConfigService customerConfigService;

  private final YbcBackupUtil ybcBackupUtil;

  private final BackupUtil backupUtil;

  @Inject
  public YbcManager(
      YbcClientService ybcClientService,
      CustomerConfigService customerConfigService,
      YbcBackupUtil ybcBackupUtil,
      BackupUtil backupUtil) {
    this.ybcClientService = ybcClientService;
    this.customerConfigService = customerConfigService;
    this.ybcBackupUtil = ybcBackupUtil;
    this.backupUtil = backupUtil;
  }

  public boolean deleteNfsDirectory(Backup backup) {
    YbcClient ybcClient = null;
    try {
      ybcClient = ybcBackupUtil.getYbcClient(backup.universeUUID);
      CustomerConfigStorageNFSData configData =
          (CustomerConfigStorageNFSData)
              customerConfigService
                  .getOrBadRequest(backup.customerUUID, backup.getBackupInfo().storageConfigUUID)
                  .getDataObject();
      String nfsDir = configData.backupLocation;
      for (String location : backupUtil.getBackupLocations(backup)) {
        String cloudDir = BackupUtil.getBackupIdentifier(backup.universeUUID, location);
        BackupServiceNfsDirDeleteRequest nfsDirDelRequest =
            BackupServiceNfsDirDeleteRequest.newBuilder()
                .setNfsDir(nfsDir)
                .setBucket(NFSUtil.DEFAULT_YUGABYTE_NFS_BUCKET)
                .setCloudDir(cloudDir)
                .build();
        BackupServiceNfsDirDeleteResponse nfsDirDeleteResponse =
            ybcClient.backupServiceNfsDirDelete(nfsDirDelRequest);
        if (!nfsDirDeleteResponse.getStatus().getCode().equals(ControllerStatus.OK)) {
          LOG.error(
              "Nfs Dir deletion for backup {} failed with error: {}.",
              backup.backupUUID,
              nfsDirDeleteResponse.getStatus().getErrorMessage());
          return false;
        }
      }
    } catch (Exception e) {
      LOG.error("Backup {} deletion failed with error: {}", backup.backupUUID, e.getMessage());
      return false;
    } finally {
      if (ybcClient != null) {
        ybcClientService.closeClient(ybcClient);
      }
    }
    LOG.debug("Nfs dir for backup {} is successfully deleted.", backup.backupUUID);
    return true;
  }
}
