package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.common.YBADeprecated;
import com.yugabyte.yw.models.common.YbaApi;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
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
  @Getter
  @Setter
  private UUID universeUUID;

  @ApiModelProperty(value = "KMS configuration UUID")
  public UUID kmsConfigUUID = null;

  @ApiModelProperty(value = "Action type")
  public ActionType actionType;

  @ApiModelProperty(value = "Category of the backup")
  public BackupCategory category = BackupCategory.YB_BACKUP_SCRIPT;

  @ApiModelProperty(value = "Backup's storage info to restore")
  public List<BackupStorageInfo> backupStorageInfoList;

  @ApiModelProperty(hidden = true)
  @JsonIgnore
  @Getter
  @Setter
  private Map<String, YbcBackupResponse> successMarkerMap = new HashMap<>();

  // Intermediate states to resume ybc backups
  @ApiModelProperty(value = "Prefix UUID")
  public UUID prefixUUID;

  @ApiModelProperty(value = "Current index", hidden = true)
  public int currentIdx;

  @ApiModelProperty(value = "Current ybc task id", hidden = true)
  public String currentYbcTaskId;

  @ApiModelProperty(value = "Node IP")
  public String nodeIp;

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
  @YBADeprecated(sinceDate = "2023-08-28", sinceYBAVersion = "2.20.0")
  public Boolean useTablespaces = false;

  @ApiModelProperty(value = "Disable multipart upload")
  public boolean disableMultipart = false;

  // The number of concurrent commands to run on nodes over SSH
  @ApiModelProperty(value = "Number of concurrent commands to run on nodes over SSH")
  public int parallelism = 8;

  @ApiModelProperty(value = "Restore TimeStamp")
  @YBADeprecated(sinceDate = "2024-08-15", sinceYBAVersion = "2024.2.0.0")
  public String restoreTimeStamp = null;

  @ApiModelProperty(value = "Restore timestamp in millis")
  public long restoreToPointInTimeMillis;

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

    @ApiModelProperty(value = "User name of the current tables owner")
    public String oldOwner = "postgres";

    @ApiModelProperty(value = "User name of the new tables owner")
    public String newOwner = null;

    @ApiModelProperty(
        value = "Only restore selected tables instead of restoring all tables in backup")
    public boolean selectiveTableRestore = false;

    @ApiModelProperty(value = "Use tablespaces during restore")
    @Getter
    @Setter
    private boolean useTablespaces = false;

    // During restore, alter tables/schemas to be owned by the restored roles.
    @ApiModelProperty(value = "Restore global ysql roles")
    @Getter
    @Setter
    private Boolean useRoles = false;

    @ApiModelProperty(
        value = "Restore privileges for roles. Passes --use-roles during restore",
        hidden = true)
    @Getter
    @Setter
    private Boolean usePrivileges = true;

    // When set, ybc backups will ignore all new flags that came with roles backup. Useful for
    // taking
    // backups on older universes.
    @ApiModelProperty(hidden = true)
    @Getter
    @Setter
    private Boolean revertToPreRolesBehaviour = false;

    /* Error handling flags */

    // Only applicable with new roles behavior.
    @ApiModelProperty(
        value =
            "WARNING: This is a preview API that could change. Fail if tablespaces exist with same"
                + " names. Only applicable with new roles behavior.")
    @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2025.2.0.0")
    @Getter
    @Setter
    private Boolean errorIfTablespacesExists = false;

    @ApiModelProperty(
        value =
            "WARNING: This is a preview API that could change. Fail if roles exist with same names."
                + "Only applicable with new roles behavior.")
    @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2025.2.0.0")
    @Getter
    @Setter
    private Boolean errorIfRolesExists = false;

    // This will be be harcoded to true if success marker does not have dump_role_checks set to
    // true.
    // Default true until testing is complete.
    @ApiModelProperty(
        value = "WARNING: This is a preview API that could change. Ignore all restore errors")
    @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2025.1.0.0")
    @Getter
    @Setter
    private Boolean ignoreErrors = false;
  }

  public RestoreBackupParams(
      RestoreBackupParams otherParams, BackupStorageInfo backupStorageInfo, ActionType actionType) {
    this.customerUUID = otherParams.customerUUID;
    this.setUniverseUUID(otherParams.getUniverseUUID());
    this.storageConfigUUID = otherParams.storageConfigUUID;
    this.restoreTimeStamp = otherParams.restoreTimeStamp;
    this.kmsConfigUUID = otherParams.kmsConfigUUID;
    this.parallelism = otherParams.parallelism;
    this.actionType = actionType;
    this.backupStorageInfoList = new ArrayList<>();
    // Deprecating parent level useTablespaces, so need to set backupStorageInfo
    // level useTablespaces here.
    backupStorageInfo.useTablespaces =
        backupStorageInfo.useTablespaces || otherParams.useTablespaces;
    this.backupStorageInfoList.add(backupStorageInfo);
    this.disableChecksum = otherParams.disableChecksum;
    this.useTablespaces = otherParams.useTablespaces;
    this.disableMultipart = otherParams.disableMultipart;
    this.enableVerboseLogs = otherParams.enableVerboseLogs;
    this.prefixUUID = otherParams.prefixUUID;
    this.restoreToPointInTimeMillis = otherParams.restoreToPointInTimeMillis;
  }

  @JsonIgnore
  public RestoreBackupParams(RestoreBackupParams params) {
    // Don't need vebose, multipart, parallelism.
    // Since only using this for YBC restores.
    this.customerUUID = params.customerUUID;
    this.setUniverseUUID(params.getUniverseUUID());
    this.storageConfigUUID = params.storageConfigUUID;
    this.restoreTimeStamp = params.restoreTimeStamp;
    this.kmsConfigUUID = params.kmsConfigUUID;
    this.actionType = params.actionType;
    this.backupStorageInfoList = new ArrayList<>(params.backupStorageInfoList);
    this.disableChecksum = params.disableChecksum;
    this.useTablespaces = params.useTablespaces;
    this.prefixUUID = params.prefixUUID;
    this.restoreToPointInTimeMillis = params.restoreToPointInTimeMillis;
  }
}
