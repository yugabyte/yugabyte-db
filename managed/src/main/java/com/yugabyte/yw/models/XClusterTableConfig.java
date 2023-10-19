// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonGetter;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbEnumValue;
import io.swagger.annotations.ApiModelProperty;
import java.io.Serializable;
import java.util.Date;
import java.util.Optional;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Embeddable;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.IdClass;
import javax.persistence.JoinColumn;
import javax.persistence.ManyToOne;
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

  private static final Finder<String, XClusterTableConfig> find =
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
  private String streamId;

  @ApiModelProperty(value = "YbaApi Internal. Whether replication is set up for this table")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.16.0.0")
  private boolean replicationSetupDone;

  @ApiModelProperty(
      value =
          "YbaApi Internal. Whether this table needs bootstrap process " + "for replication setup")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.16.0.0")
  private boolean needBootstrap;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(value = "Time of the bootstrap of the table", example = "2022-12-12T13:07:18Z")
  private Date bootstrapCreateTime;

  @ApiModelProperty(value = "The backup config used to do bootstrapping for this table")
  @ManyToOne
  @JoinColumn(name = "backup_uuid", referencedColumnName = "backup_uuid")
  @JsonProperty("backupUuid")
  private Backup backup;

  @ApiModelProperty(value = "The restore config used to do bootstrapping for this table")
  @ManyToOne
  @JoinColumn(name = "restore_uuid", referencedColumnName = "restore_uuid")
  @JsonProperty("restoreUuid")
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
  private Status status;

  public enum Status {
    Validated("Validated"),
    Running("Running"),
    Updating("Updating"),
    Bootstrapping("Bootstrapping"),
    Failed("Failed"),

    // The following statuses will not be stored in the YBA DB.
    Error("Error"),
    Warning("Warning"),
    UnableToFetch("UnableToFetch");

    private final String status;

    Status(String status) {
      this.status = status;
    }

    @Override
    @DbEnumValue
    public String toString() {
      return this.status;
    }
  }

  public XClusterTableConfig(XClusterConfig config, String tableId) {
    this.setConfig(config);
    this.setTableId(tableId);
    this.setReplicationSetupDone(false);
    this.setNeedBootstrap(false);
    this.setIndexTable(false);
    this.setStatus(Status.Validated);
  }

  public static Optional<XClusterTableConfig> maybeGetByStreamId(String streamId) {
    XClusterTableConfig xClusterTableConfig =
        find.query().fetch("tables").where().eq("stream_id", streamId).findOne();
    if (xClusterTableConfig == null) {
      log.info("Cannot find an xClusterTableConfig with streamId {}", streamId);
      return Optional.empty();
    }
    return Optional.of(xClusterTableConfig);
  }

  @JsonGetter("backupUuid")
  UUID getBackupUuid() {
    if (getBackup() == null) {
      return null;
    }
    return getBackup().getBackupUUID();
  }

  @JsonGetter("restoreUuid")
  UUID getRestoreUuid() {
    if (getRestore() == null) {
      return null;
    }
    return getRestore().getRestoreUUID();
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
