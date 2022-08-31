// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.YbcClient;
import org.yb.ybc.BackupServiceNfsDirDeleteRequest;
import org.yb.ybc.BackupServiceNfsDirDeleteResponse;
import java.util.regex.Matcher;
import java.util.UUID;
import org.yb.ybc.BackupServiceTaskAbortRequest;
import org.yb.ybc.BackupServiceTaskAbortResponse;
import org.yb.ybc.BackupServiceTaskCreateRequest;
import org.yb.ybc.BackupServiceTaskCreateResponse;
import org.yb.ybc.BackupServiceTaskDeleteRequest;
import org.yb.ybc.BackupServiceTaskDeleteResponse;
import org.yb.ybc.BackupServiceTaskResultRequest;
import org.yb.ybc.BackupServiceTaskResultResponse;
import org.yb.ybc.ControllerStatus;

@Singleton
public class YbcManager {

  private static final Logger LOG = LoggerFactory.getLogger(YbcManager.class);

  private final YbcClientService ybcClientService;
  private final CustomerConfigService customerConfigService;
  private final YbcBackupUtil ybcBackupUtil;
  private final BackupUtil backupUtil;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private final ReleaseManager releaseManager;
  private final NodeManager nodeManager;

  private static final int WAIT_EACH_ATTEMPT_MS = 5000;
  private static final int MAX_RETRIES = 10;

  private static final String YBC_STABLE_RELEASE_PATH = "ybc.releases.stable_version";

  @Inject
  public YbcManager(
      YbcClientService ybcClientService,
      CustomerConfigService customerConfigService,
      YbcBackupUtil ybcBackupUtil,
      BackupUtil backupUtil,
      RuntimeConfigFactory runtimeConfigFactory,
      ReleaseManager releaseManager,
      NodeManager nodeManager) {
    this.ybcClientService = ybcClientService;
    this.customerConfigService = customerConfigService;
    this.ybcBackupUtil = ybcBackupUtil;
    this.backupUtil = backupUtil;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.releaseManager = releaseManager;
    this.nodeManager = nodeManager;
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
        String cloudDir = BackupUtil.getBackupIdentifier(configData.backupLocation, location, true);
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
      ybcClientService.closeClient(ybcClient);
    }
    LOG.debug("Nfs dir for backup {} is successfully deleted.", backup.backupUUID);
    return true;
  }

  public void abortBackupTask(UUID customerUUID, UUID backupUUID, String taskID) {
    Backup backup = Backup.getOrBadRequest(customerUUID, backupUUID);
    YbcClient ybcClient = null;
    try {
      ybcClient = ybcBackupUtil.getYbcClient(backup.universeUUID);
      BackupServiceTaskAbortRequest abortTaskRequest =
          BackupServiceTaskAbortRequest.newBuilder().setTaskId(taskID).build();
      BackupServiceTaskAbortResponse abortTaskResponse =
          ybcClient.backupServiceTaskAbort(abortTaskRequest);
      if (!abortTaskResponse.getStatus().getCode().equals(ControllerStatus.OK)) {
        LOG.error(
            "Aborting backup {} task errored out with {}.",
            backup.backupUUID,
            abortTaskResponse.getStatus().getErrorMessage());
        return;
      }
      BackupServiceTaskResultRequest taskResultRequest =
          BackupServiceTaskResultRequest.newBuilder().setTaskId(taskID).build();
      BackupServiceTaskResultResponse taskResultResponse =
          ybcClient.backupServiceTaskResult(taskResultRequest);
      if (!taskResultResponse.getTaskStatus().equals(ControllerStatus.ABORT)) {
        LOG.error(
            "Aborting backup {} task errored out and is in {} state.",
            backup.backupUUID,
            taskResultResponse.getTaskStatus());
        return;
      } else {
        LOG.info("Backup {} task is successfully aborted on Yb-controller.", backup.backupUUID);
        deleteYbcBackupTask(backup.universeUUID, taskID);
      }
    } catch (Exception e) {
      LOG.error("Backup {} task abort failed with error: {}.", backup.backupUUID, e.getMessage());
    } finally {
      ybcClientService.closeClient(ybcClient);
    }
  }

  public void deleteYbcBackupTask(UUID universeUUID, String taskID) {
    YbcClient ybcClient = null;
    try {
      ybcClient = ybcBackupUtil.getYbcClient(universeUUID);
      BackupServiceTaskResultRequest taskResultRequest =
          BackupServiceTaskResultRequest.newBuilder().setTaskId(taskID).build();
      BackupServiceTaskResultResponse taskResultResponse =
          ybcClient.backupServiceTaskResult(taskResultRequest);
      if (taskResultResponse.getTaskStatus().equals(ControllerStatus.NOT_FOUND)) {
        return;
      }
      BackupServiceTaskDeleteRequest taskDeleteRequest =
          BackupServiceTaskDeleteRequest.newBuilder().setTaskId(taskID).build();
      BackupServiceTaskDeleteResponse taskDeleteResponse = null;
      int numRetries = 0;
      while (numRetries < MAX_RETRIES) {
        taskDeleteResponse = ybcClient.backupServiceTaskDelete(taskDeleteRequest);
        if (!taskDeleteResponse.getStatus().getCode().equals(ControllerStatus.IN_PROGRESS)) {
          break;
        }
        Thread.sleep(WAIT_EACH_ATTEMPT_MS);
        numRetries++;
      }
      if (!taskDeleteResponse.getStatus().getCode().equals(ControllerStatus.OK)) {
        LOG.error(
            "Deleting task {} errored out and is in {} state.",
            taskID,
            taskDeleteResponse.getStatus());
        return;
      }
      LOG.info("Task {} is successfully deleted on Yb-controller.", taskID);
    } catch (Exception e) {
      LOG.error("Task {} deletion failed with error: {}", taskID, e.getMessage());
    } finally {
      ybcClientService.closeClient(ybcClient);
    }
  }

  public String downloadSuccessMarker(
      BackupServiceTaskCreateRequest downloadSuccessMarkerRequest,
      UUID universeUUID,
      String taskID) {
    YbcClient ybcClient = null;
    String successMarker = null;
    try {
      ybcClient = ybcBackupUtil.getYbcClient(universeUUID);
      BackupServiceTaskCreateResponse downloadSuccessMarkerResponse =
          ybcClient.restoreNamespace(downloadSuccessMarkerRequest);
      if (!downloadSuccessMarkerResponse.getStatus().getCode().equals(ControllerStatus.OK)) {
        throw new Exception(
            String.format(
                "Failed to send download success marker request, failure status: {}",
                downloadSuccessMarkerResponse.getStatus().getCode().name()));
      }
      BackupServiceTaskResultRequest downloadSuccessMarkerResultRequest =
          BackupServiceTaskResultRequest.newBuilder().setTaskId(taskID).build();
      BackupServiceTaskResultResponse downloadSuccessMarkerResultResponse = null;
      int numRetries = 0;
      while (numRetries < MAX_RETRIES) {
        downloadSuccessMarkerResultResponse =
            ybcClient.backupServiceTaskResult(downloadSuccessMarkerResultRequest);
        if (!downloadSuccessMarkerResultResponse
            .getTaskStatus()
            .equals(ControllerStatus.IN_PROGRESS)) {
          break;
        }
        Thread.sleep(WAIT_EACH_ATTEMPT_MS);
        numRetries++;
      }
      if (!downloadSuccessMarkerResultResponse.getTaskStatus().equals(ControllerStatus.OK)) {
        throw new Exception(
            String.format(
                "Failed to download success marker, failure status: {}",
                downloadSuccessMarkerResultResponse.getTaskStatus().name()));
      }
      LOG.info("Task {} on YB-Controller to fetch success marker is successful", taskID);
      successMarker = downloadSuccessMarkerResultResponse.getMetadataJson();
      deleteYbcBackupTask(universeUUID, taskID);
      return successMarker;
    } catch (Exception e) {
      LOG.error(
          "Task {} on YB-Controller to fetch success marker for restore failed. Error: {}",
          taskID,
          e.getMessage());
      return successMarker;
    } finally {
      ybcClientService.closeClient(ybcClient);
    }
  }

  public String getStableYbcVersion() {
    return runtimeConfigFactory.globalRuntimeConf().getString(YBC_STABLE_RELEASE_PATH);
  }

  /**
   * @param universe
   * @param node
   * @return pair of string containing osType and archType of ybc-server-package
   */
  public Pair<String, String> getYbcPackageDetailsForNode(Universe universe, NodeDetails node) {
    Cluster nodeCluster = Universe.getCluster(universe, node.nodeName);
    String ybSoftwareVersion = nodeCluster.userIntent.ybSoftwareVersion;
    String ybServerPackage =
        nodeManager.getYbServerPackageName(ybSoftwareVersion, nodeCluster.getRegions().get(0));
    return Util.getYbcPackageDetailsFromYbServerPackage(ybServerPackage);
  }

  /**
   * @param universe
   * @param node
   * @param ybcVersion
   * @return Temp location of ybc server package on a DB node.
   */
  public String getYbcPackageTmpLocation(Universe universe, NodeDetails node, String ybcVersion) {
    Pair<String, String> ybcPackageDetails = getYbcPackageDetailsForNode(universe, node);
    return String.format(
        "/tmp/ybc-%s-%s-%s.tar.gz",
        ybcVersion, ybcPackageDetails.getFirst(), ybcPackageDetails.getSecond());
  }

  /**
   * @param universe
   * @param node
   * @param ybcVersion
   * @return complete file path of ybc server package present in YBA node.
   */
  public String getYbcServerPackageForNode(Universe universe, NodeDetails node, String ybcVersion) {
    Pair<String, String> ybcPackageDetails = getYbcPackageDetailsForNode(universe, node);
    ReleaseManager.ReleaseMetadata releaseMetadata =
        releaseManager.getYbcReleaseByVersion(
            ybcVersion, ybcPackageDetails.getFirst(), ybcPackageDetails.getSecond());
    String ybcServerPackage =
        releaseMetadata.getFilePath(
            Universe.getCluster(universe, node.nodeName).getRegions().get(0));
    if (StringUtils.isBlank(ybcServerPackage)) {
      throw new RuntimeException("Ybc package cannot be empty.");
    }
    Matcher matcher = NodeManager.YBC_PACKAGE_PATTERN.matcher(ybcServerPackage);
    boolean matches = matcher.matches();
    if (!matches) {
      throw new RuntimeException(
          String.format(
              "Ybc package: %s does not follow the format required: %s",
              ybcServerPackage, NodeManager.YBC_PACKAGE_REGEX));
    }
    return ybcServerPackage;
  }
}
