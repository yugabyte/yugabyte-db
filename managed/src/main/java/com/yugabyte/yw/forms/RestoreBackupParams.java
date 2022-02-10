package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.UUID;
import org.yb.CommonTypes.TableType;
import play.data.validation.Constraints;

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

  @ApiModelProperty(value = "Data of the backup to be restored")
  public BackupData backupData;

  @ApiModelProperty(value = "Backup's storage info to restore")
  public List<BackupStorageInfo> backupStorageInfoList;

  // Should backup script enable verbose logging.
  @ApiModelProperty(value = "Is verbose logging enabled")
  public boolean enableVerboseLogs = false;

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

    @ApiModelProperty(value = "Storage config uuid")
    public UUID storageConfigUUID;

    @ApiModelProperty(value = "Keyspace name")
    public String keyspace;

    @ApiModelProperty(value = "Tables")
    public List<String> tableNameList;

    @ApiModelProperty(value = "Table UUIDs")
    public List<UUID> tableUUIDList;

    @ApiModelProperty(value = "Is SSE")
    public boolean sse = false;
  }

  @ApiModel(description = "Backup Info for doing restore operation")
  public static class BackupData {

    @ApiModelProperty(value = "Backup UUID")
    public UUID backupUUID = null;

    @ApiModelProperty(value = "Storage config uuid")
    public UUID storageConfigUUID;

    @ApiModelProperty(value = "Is SSE")
    public boolean sse = false;
  }
}
