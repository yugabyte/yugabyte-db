// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static org.yb.CommonTypes.ReplicationErrorPb.REPLICATION_AUTO_FLAG_CONFIG_VERSION_MISMATCH;
import static org.yb.CommonTypes.ReplicationErrorPb.REPLICATION_MISSING_OP_ID;
import static org.yb.CommonTypes.ReplicationErrorPb.REPLICATION_MISSING_TABLE;
import static org.yb.CommonTypes.ReplicationErrorPb.REPLICATION_SCHEMA_MISMATCH;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonGetter;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonSetter;
import com.fasterxml.jackson.annotation.JsonValue;
import com.yugabyte.yw.forms.TableInfoForm.TableInfoResp;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbEnumValue;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Embeddable;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.IdClass;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.Transient;
import java.io.Serializable;
import java.util.Arrays;
import java.util.Date;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.ToString;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@IdClass(XClusterTableConfig.XClusterTableConfigPK.class)
@Entity
@NoArgsConstructor
@ToString(onlyExplicitlyIncluded = true)
@Getter
@Setter
public class XClusterTableConfig extends Model {

  public static final Finder<String, XClusterTableConfig> find =
      new Finder<String, XClusterTableConfig>(XClusterTableConfig.class) {};

  @Id
  @ManyToOne
  @JoinColumn(name = "config_uuid", referencedColumnName = "uuid")
  @ApiModelProperty(value = "The XCluster config that this table is a participant of")
  @JsonIgnore
  private XClusterConfig config;

  @Id
  @Column(length = 64)
  @ApiModelProperty(value = "Table ID", example = "000033df000030008000000000004005")
  @ToString.Include
  private String tableId;

  @Column(length = 64)
  @ApiModelProperty(
      value = "Stream ID if replication is setup; bootstrap ID if the table is bootstrapped",
      example = "a9d2470786694dc4b34e0e58e592da9e")
  @ToString.Include
  private String streamId;

  @ApiModelProperty(value = "YbaApi Internal. Whether replication is set up for this table")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.16.0.0")
  @ToString.Include
  private boolean replicationSetupDone;

  @ApiModelProperty(
      value = "YbaApi Internal. Whether this table needs bootstrap process for replication setup")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.16.0.0")
  private boolean needBootstrap;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(value = "Time of the bootstrap of the table", example = "2022-12-12T13:07:18Z")
  private Date bootstrapCreateTime;

  @ApiModelProperty(value = "The backup config used to do bootstrapping for this table")
  @ManyToOne
  @JoinColumn(name = "backup_uuid", referencedColumnName = "backup_uuid")
  @JsonIgnore
  private Backup backup;

  @ApiModelProperty(value = "The restore config used to do bootstrapping for this table")
  @ManyToOne
  @JoinColumn(name = "restore_uuid", referencedColumnName = "restore_uuid")
  @JsonIgnore
  private Restore restore;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "Time of the last try to restore data to the target universe",
      example = "2022-12-12T13:07:18Z")
  private Date restoreTime;

  // If its main table is not part the config, it will be false; otherwise, it indicates whether the
  // table is an index table.
  @ApiModelProperty(
      value =
          "YbaApi Internal. Whether this table is an index table and its main table is in"
              + " replication")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.16.0.0")
  private boolean indexTable;

  @ApiModelProperty(
      value = "Status",
      allowableValues =
          "Validated, Running, Updating, Warning, Error, Bootstrapping, Failed, UnableToFetch")
  @ToString.Include
  private Status status;

  @Transient
  @ApiModelProperty(value = "tableInfo from source universe", required = false)
  private TableInfoResp sourceTableInfo;

  @Transient
  @ApiModelProperty(value = "tableInfo from target universe", required = false)
  private TableInfoResp targetTableInfo;

  // Statuses are declared in reverse severity for showing tables in UI with specific order based on
  // code.
  public enum Status {
    UnableToFetch("UnableToFetch", 0), // Not stored in YBA DB.
    Updating("Updating", 1),
    Bootstrapping("Bootstrapping", 2),
    Validated("Validated", 3),
    Running("Running", 4),
    // The following statuses will leads to alert creation.
    Failed("Failed", -1),
    Error("Error", -2), // Not stored in YBA DB.
    Warning("Warning", -3), // Not stored in YBA DB.
    DroppedFromSource("DroppedFromSource", -5), // Not stored in YBA DB.
    DroppedFromTarget("DroppedFromTarget", -6), // Not stored in YBA DB.
    ExtraTableOnSource("ExtraTableOnSource", -7), // Not stored in YBA DB.
    ExtraTableOnTarget("ExtraTableOnTarget", -8); // Not stored in YBA DB.

    private final String status;
    @Getter private final int code;

    Status(String status, int code) {
      this.status = status;
      this.code = code;
    }

    @Override
    @DbEnumValue
    public String toString() {
      return this.status;
    }
  }

  // TODO move API response attributes out of the DB model
  public enum ReplicationStatusError {
    MISSING_OP(REPLICATION_MISSING_OP_ID, "Missing op ID"),
    SCHEMA_MISMATCH(REPLICATION_SCHEMA_MISMATCH, "Schema mismatch"),
    MISSING_TABLE(REPLICATION_MISSING_TABLE, "Missing table"),
    AUTO_FLAG_CONFIG_MISMATCH(
        REPLICATION_AUTO_FLAG_CONFIG_VERSION_MISMATCH, "Auto flag config mismatch");

    private final org.yb.CommonTypes.ReplicationErrorPb errorCode;
    private final String message;

    ReplicationStatusError(org.yb.CommonTypes.ReplicationErrorPb errorCode, String message) {
      this.errorCode = errorCode;
      this.message = message;
    }

    @JsonValue
    @Override
    public String toString() {
      return message;
    }

    public static ReplicationStatusError fromErrorCode(
        org.yb.CommonTypes.ReplicationErrorPb errorCode) {
      return Arrays.stream(values()).filter(e -> e.errorCode == errorCode).findFirst().orElse(null);
    }
  }

  @Transient
  @ApiModelProperty(value = "Short human readable replication status error messages")
  private Set<ReplicationStatusError> replicationStatusErrors = new HashSet<>();

  public XClusterTableConfig(XClusterConfig config, String tableId) {
    this.setConfig(config);
    this.setTableId(tableId);
    this.setReplicationSetupDone(false);
    this.setNeedBootstrap(false);
    this.setIndexTable(false);
    this.setStatus(Status.Validated);
  }

  @JsonSetter("backupUuid")
  private void setBackupFromUuid(UUID backupUuid) {
    if (backupUuid == null) {
      setBackup(null);
      return;
    }
    setBackup(Backup.maybeGet(backupUuid).orElse(null));
  }

  @JsonGetter("backupUuid")
  UUID getBackupUuid() {
    if (getBackup() == null) {
      return null;
    }
    return getBackup().getBackupUUID();
  }

  @JsonSetter("restoreUuid")
  private void setRestoreFromUuid(UUID restoreUuid) {
    if (restoreUuid == null) {
      restore = null;
      return;
    }
    setRestore(Restore.maybeGet(restoreUuid).orElse(null));
  }

  @JsonGetter("restoreUuid")
  UUID getRestoreUuid() {
    if (getRestore() == null) {
      return null;
    }
    return getRestore().getRestoreUUID();
  }

  public void reset() {
    this.setStatus(XClusterTableConfig.Status.Validated);
    this.setReplicationSetupDone(false);
    this.setStreamId(null);
    this.setBootstrapCreateTime(null);
    this.setRestoreTime(null);
    // We intentionally do not reset backup and restore objects in the xCluster config because
    // restart parent task sets these attributes and its subtasks use this method.
  }

  /** This class is the primary key for XClusterTableConfig. */
  @Embeddable
  @EqualsAndHashCode
  public static class XClusterTableConfigPK implements Serializable {
    @Column(name = "config_uuid")
    public UUID config;

    public String tableId;
  }
}
