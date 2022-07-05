// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonGetter;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import io.ebean.Finder;
import io.ebean.Model;
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
import lombok.NoArgsConstructor;
import lombok.ToString;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@IdClass(XClusterTableConfig.XClusterTableConfigPK.class)
@Entity
@NoArgsConstructor
@ToString(onlyExplicitlyIncluded = true)
public class XClusterTableConfig extends Model {

  private static final Finder<UUID, XClusterTableConfig> find =
      new Finder<UUID, XClusterTableConfig>(XClusterTableConfig.class) {};

  @Id
  @ManyToOne
  @JoinColumn(name = "config_uuid", referencedColumnName = "uuid")
  @ApiModelProperty(value = "Time of the bootstrap of the table")
  @JsonIgnore
  public XClusterConfig config;

  @Id
  @Column(length = 64)
  @ApiModelProperty(value = "Table ID", example = "000033df000030008000000000004005")
  @ToString.Include
  public String tableId;

  @Column(length = 64)
  @ApiModelProperty(
      value = "Stream ID if replication is setup; bootstrap ID if the table is bootstrapped",
      example = "a9d2470786694dc4b34e0e58e592da9e")
  public String streamId;

  @ApiModelProperty(value = "Whether replication is set up for this table")
  public boolean replicationSetupDone;

  @ApiModelProperty(value = "Whether this table needs bootstrap process for replication setup")
  public boolean needBootstrap;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  @ApiModelProperty(
      value = "Time of the bootstrap of the table",
      example = "2022-04-26 15:37:32.610000")
  public Date bootstrapCreateTime;

  @ManyToOne
  @JoinColumn(name = "backup_uuid", referencedColumnName = "backup_uuid")
  @JsonProperty("backupUuid")
  public Backup backup;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  @ApiModelProperty(
      value = "Time of the last try to restore data to the target universe",
      example = "2022-04-26 15:37:32.610000")
  public Date restoreTime;

  public XClusterTableConfig(XClusterConfig config, String tableId) {
    this.config = config;
    this.tableId = tableId;
    replicationSetupDone = false;
    needBootstrap = false;
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
    if (backup == null) {
      return null;
    }
    return backup.backupUUID;
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
