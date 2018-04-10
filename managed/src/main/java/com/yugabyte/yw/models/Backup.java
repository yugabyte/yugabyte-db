// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.avaje.ebean.Model;
import com.avaje.ebean.annotation.DbJson;
import com.avaje.ebean.annotation.EnumValue;
import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.forms.BackupTableParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;

import static java.lang.Math.abs;

@Entity
public class Backup extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(Backup.class);
  SimpleDateFormat tsFormat = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss");

  public enum BackupState {
    @EnumValue("In Progress")
    InProgress,

    @EnumValue("Completed")
    Completed,

    @EnumValue("Failed")
    Failed,

    @EnumValue("Deleted")
    Deleted
  }

  @Id
  public UUID backupUUID;

  @Column(nullable = false)
  public UUID customerUUID;

  @Column(nullable = false)
  public BackupState state;

  @Column(nullable = false)
  @DbJson
  public JsonNode backupInfo;

  public void setBackupInfo(BackupTableParams params) {
    this.backupInfo = Json.toJson(params);
  }

  public BackupTableParams getBackupInfo() {
    return Json.fromJson(this.backupInfo, BackupTableParams.class);
  }

  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd hh:mm:ss")
  private Date createTime;
  public Date getCreateTime() { return createTime; }

  @Column
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd hh:mm:ss")
  private Date updateTime;
  public Date getUpdateTime() { return updateTime; }

  public static final Find<Long, Backup> find = new Find<Long, Backup>(){};

  // For creating new backup we would set the storage location based on
  // universe UUID and backup UUID.
  // univ-<univ_uuid>/backup-<timestamp>-<something_to_disambiguate_from_yugaware>
  private void updateStorageLocation(BackupTableParams params) {
    CustomerConfig customerConfig = CustomerConfig.get(customerUUID, params.storageConfigUUID);
    params.storageLocation = String.format("univ-%s/backup-%s-%d",
        params.universeUUID, tsFormat.format(new Date()), abs(backupUUID.hashCode()));
    if (customerConfig != null && customerConfig.name.equals("S3")) {
      params.storageLocation = String.format("%s/%s",
          customerConfig.getData().get("S3_BUCKET").asText(),
          params.storageLocation
      );
    }
  }

  public static Backup create(UUID customerUUID, BackupTableParams params) {
    Backup backup = new Backup();
    backup.backupUUID = UUID.randomUUID();
    backup.customerUUID = customerUUID;
    backup.state = BackupState.InProgress;
    backup.createTime = new Date();
    // We would derive the storage location based on the parameters
    backup.updateStorageLocation(params);
    backup.setBackupInfo(params);
    backup.save();
    return backup;
  }

  public static List<Backup> fetchByUniverseUUID(UUID customerUUID, UUID universeUUID) {
      List<Backup> backupList = find.where().eq("customer_uuid", customerUUID).orderBy("create_time desc").findList();
      return backupList.stream()
          .filter(backup -> backup.getBackupInfo().universeUUID.equals(universeUUID))
          .collect(Collectors.toList());
  }

  public static Backup get(UUID customerUUID, UUID backupUUID) {
    return find.where().idEq(backupUUID).eq("customer_uuid", customerUUID).findUnique();
  }

  public static Backup fetchByTaskUUID(UUID taskUUID) {
    CustomerTask customerTask = CustomerTask.find.where()
        .eq("task_uuid", taskUUID)
        .eq("target_type", CustomerTask.TargetType.Backup.name()).findUnique();
    if (customerTask == null) {
      return null;
    }
    return Backup.find.where().eq("backup_uuid", customerTask.getTargetUUID()).findUnique();
  }

  public void transitionState(BackupState newState) {
    // We only allow state transition from InProgress to a valid state
    // Or completed to deleted state.
    if ((this.state == BackupState.InProgress && this.state != newState) ||
        (this.state == BackupState.Completed && newState == BackupState.Deleted)) {
      this.state = newState;
      this.updateTime = new Date();
      save();
    }
  }
}
