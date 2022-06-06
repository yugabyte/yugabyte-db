package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import lombok.NoArgsConstructor;
import org.yb.CommonTypes.TableType;
import play.data.validation.Constraints;

@NoArgsConstructor
public class RestoreBackupParams extends UniverseTaskParams {

  public enum ActionType {
    RESTORE,
    RESTORE_KEYS
  }

  @Constraints.Required
  @ApiModelProperty(value = "Customer UUID")
  public UUID customerUUID;

  @Constraints.Required
  @ApiModelProperty(value = "Universe UUID", required = true)
  public UUID universeUUID;

  @ApiModelProperty(value = "KMS configuration UUID")
  public UUID kmsConfigUUID = null;

  @ApiModelProperty(value = "Action type")
  public ActionType actionType;

  @ApiModelProperty(value = "Backup's storage info to restore")
  public List<BackupStorageInfo> backupStorageInfoList;

  // Should backup script enable verbose logging.
  @ApiModelProperty(value = "Is verbose logging enabled")
  public boolean enableVerboseLogs = false;

  @ApiModelProperty(value = "Storage config uuid")
  public UUID storageConfigUUID;

  @ApiModelProperty(value = "Alter load balancer state")
  public boolean alterLoadBalancer = true;

  @ApiModelProperty(value = "Disable checksum")
  public Boolean disableChecksum = false;

  @ApiModelProperty(value = "Is tablespaces information included")
  public Boolean useTablespaces = false;

  // The number of concurrent commands to run on nodes over SSH
  @ApiModelProperty(value = "Number of concurrent commands to run on nodes over SSH")
  public int parallelism = 8;

  @ApiModelProperty(value = "Restore TimeStamp")
  public String restoreTimeStamp = null;

  @ApiModelProperty(value = "User name of the current tables owner")
  public String oldOwner = "yugabyte";

  @ApiModelProperty(value = "User name of the new tables owner")
  public String newOwner = null;

  @ApiModel(description = "Backup Storage Info for doing restore operation")
  public static class BackupStorageInfo {

    @ApiModelProperty(value = "Backup type")
    public TableType backupType;

    // Specifies the backup storage location. In case of S3 it would have
    // the S3 url based on universeUUID and timestamp.
    @ApiModelProperty(value = "Storage location")
    public String storageLocation;

    @ApiModelProperty(value = "Keyspace name")
    public String keyspace;

    @ApiModelProperty(value = "Tables")
    public List<String> tableNameList;

    @ApiModelProperty(value = "Is SSE")
    public boolean sse = false;
  }

  public RestoreBackupParams(
      RestoreBackupParams otherParams, BackupStorageInfo backupStorageInfo, ActionType actionType) {
    this.customerUUID = otherParams.customerUUID;
    this.universeUUID = otherParams.universeUUID;
    this.storageConfigUUID = otherParams.storageConfigUUID;
    this.restoreTimeStamp = otherParams.restoreTimeStamp;
    this.kmsConfigUUID = otherParams.kmsConfigUUID;
    this.actionType = actionType;
    this.backupStorageInfoList = new ArrayList<>();
    this.backupStorageInfoList.add(backupStorageInfo);
  }
}
