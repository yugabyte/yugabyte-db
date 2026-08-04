// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonGetter;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonSetter;
import com.yugabyte.yw.forms.TableInfoForm.NamespaceInfoResp;
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
import java.util.Date;
import java.util.UUID;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Entity
@IdClass(XClusterNamespaceConfig.XClusterNamespaceConfigPK.class)
@NoArgsConstructor
@Getter
@Setter
public class XClusterNamespaceConfig extends Model {

  public static final Finder<String, XClusterNamespaceConfig> find =
      new Finder<String, XClusterNamespaceConfig>(XClusterNamespaceConfig.class) {};

  @Id
  @ManyToOne
  @JoinColumn(name = "config_uuid", referencedColumnName = "uuid")
  @JsonIgnore
  private XClusterConfig config;

  @Id
  @Column(length = 64)
  private String sourceNamespaceId;

  @ApiModelProperty(
      value = "Status",
      allowableValues = "Validated, Running, Updating, Warning, Error, Bootstrapping, Failed")
  private Status status;

  // Statuses are declared in reverse severity for showing tables in UI with specific order.
  public enum Status {
    Failed("Failed"),
    Error("Error"), // Not stored in YBA DB.
    Warning("Warning"), // Not stored in YBA DB.
    Updating("Updating"),
    Bootstrapping("Bootstrapping"),
    Validated("Validated"),
    Running("Running");

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

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value =
          "Time of replication setup, ie, table added to the replication group "
              + "on the target universe",
      example = "2022-12-12T13:07:18Z")
  private Date replicationSetupTime;

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

  @Transient
  @ApiModelProperty(value = "namespaceInfo from source universe", required = false)
  private NamespaceInfoResp sourceNamespaceInfo;

  @Transient
  @ApiModelProperty(value = "namespaceInfo from target universe", required = false)
  private NamespaceInfoResp targetNamespaceInfo;

  public XClusterNamespaceConfig(XClusterConfig config, String sourceNamespaceId) {
    this.setConfig(config);
    this.setSourceNamespaceId(sourceNamespaceId);
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
    this.setStatus(Status.Validated);
    // We intentionally do not reset backup and restore objects in the xCluster config because
    // restart parent task sets these attributes and its subtasks use this method.
  }

  /** This class is the primary key for XClusterNamespaceConfig. */
  @Embeddable
  @EqualsAndHashCode
  public static class XClusterNamespaceConfigPK implements Serializable {
    @Column(name = "config_uuid")
    public UUID config;

    public String sourceNamespaceId;
  }
}
