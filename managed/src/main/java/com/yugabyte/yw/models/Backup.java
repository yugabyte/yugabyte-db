// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.forms.BackupTableParams;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.CreatedTimestamp;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.UpdatedTimestamp;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import java.text.SimpleDateFormat;
import java.util.*;
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
    Deleted,

    @EnumValue("Skipped")
    Skipped,

    // Complete or partial failure to delete
    @EnumValue("FailedToDelete")
    FailedToDelete,
  }

  @Id
  public UUID backupUUID;

  @Column(nullable = false)
  public UUID customerUUID;

  @Column(nullable = false)
  public BackupState state;

  @Column(columnDefinition = "TEXT", nullable = false)
  @DbJson
  public JsonNode backupInfo;

  @Column(unique = true)
  public UUID taskUUID;

  @Column
  private UUID scheduleUUID;
  public UUID getScheduleUUID() { return scheduleUUID; }

  @Column
  // Unix timestamp at which backup will get deleted.
  private Date expiry;
  public Date getExpiry() { return expiry; }

  public void setBackupInfo(BackupTableParams params) {
    this.backupInfo = Json.toJson(params);
  }

  public BackupTableParams getBackupInfo() {
    return Json.fromJson(this.backupInfo, BackupTableParams.class);
  }

  @CreatedTimestamp
  private Date createTime;
  public Date getCreateTime() { return createTime; }

  @UpdatedTimestamp
  private Date updateTime;
  public Date getUpdateTime() { return updateTime; }

  public static final Finder<UUID, Backup> find = new Finder<UUID, Backup>(Backup.class){};

  // For creating new backup we would set the storage location based on
  // universe UUID and backup UUID.
  // univ-<univ_uuid>/backup-<timestamp>-<something_to_disambiguate_from_yugaware>/table-keyspace
  // .table_name.table_uuid
  private void updateStorageLocation(BackupTableParams params) {
    CustomerConfig customerConfig = CustomerConfig.get(customerUUID, params.storageConfigUUID);
    if (params.tableUUIDList != null) {
      params.storageLocation = String.format("univ-%s/backup-%s-%d/multi-table-%s",
        params.universeUUID, tsFormat.format(new Date()), abs(backupUUID.hashCode()),
        params.getKeyspace());
    } else if (params.getTableName() == null && params.getKeyspace() != null) {
      params.storageLocation = String.format("univ-%s/backup-%s-%d/keyspace-%s",
        params.universeUUID, tsFormat.format(new Date()), abs(backupUUID.hashCode()),
        params.getKeyspace());
    } else {
      params.storageLocation = String.format("univ-%s/backup-%s-%d/table-%s.%s",
        params.universeUUID, tsFormat.format(new Date()), abs(backupUUID.hashCode()),
        params.getKeyspace(), params.getTableName());
      if (params.tableUUID != null) {
        params.storageLocation = String.format("%s-%s",
          params.storageLocation,
          params.tableUUID.toString().replace("-", "")
        );
      }
    }

    if (customerConfig != null) {
      // TODO: These values, S3 vs NFS / S3_BUCKET vs NFS_PATH come from UI right now...
      JsonNode storageNode = customerConfig.getData().get("BACKUP_LOCATION");
      if (storageNode != null) {
        String storagePath = storageNode.asText();
        if (storagePath != null && !storagePath.isEmpty()) {
          params.storageLocation = String.format("%s/%s", storagePath, params.storageLocation);
        }
      }
    }
  }

  public static Backup create(UUID customerUUID, BackupTableParams params) {
    Backup backup = new Backup();
    backup.backupUUID = UUID.randomUUID();
    backup.customerUUID = customerUUID;
    backup.state = BackupState.InProgress;
    if (params.scheduleUUID != null) {
      backup.scheduleUUID = params.scheduleUUID;
    }
    if (params.timeBeforeDelete != 0L) {
      backup.expiry = new Date(System.currentTimeMillis() + params.timeBeforeDelete);
    }
    if (params.backupList != null) {
      // In event of universe backup
      for (BackupTableParams childBackup : params.backupList) {
        if (childBackup.storageLocation == null) {
          backup.updateStorageLocation(childBackup);
        }
      }
    } else if (params.storageLocation == null) {
      // We would derive the storage location based on the parameters
      backup.updateStorageLocation(params);
    }
    backup.setBackupInfo(params);
    backup.save();
    return backup;
  }

  // We need to set the taskUUID right after commissioner task is submitted.

  /**
   * @param taskUUID to set if none set previously
   * @return true if the call ends up setting task uuid.
   */
  public synchronized boolean setTaskUUID(UUID taskUUID) {
    if (this.taskUUID == null) {
      this.taskUUID = taskUUID;
      save();
      return true;
    }
    return false;
  }

  public static List<Backup> fetchByUniverseUUID(UUID customerUUID, UUID universeUUID) {
      List<Backup> backupList = find.query().where()
        .eq("customer_uuid", customerUUID)
        .orderBy("create_time desc")
        .findList();
      return backupList.stream()
          .filter(backup -> backup.getBackupInfo().universeUUID.equals(universeUUID))
          .collect(Collectors.toList());
  }

  public static Backup get(UUID customerUUID, UUID backupUUID) {
    return find.query().where()
      .idEq(backupUUID)
      .eq("customer_uuid", customerUUID)
      .findOne();
  }

  public static Backup fetchByTaskUUID(UUID taskUUID) {
    return Backup.find.query().where()
      .eq("task_uuid", taskUUID)
      .findOne();
  }

  public static Map<Customer, List<Backup>> getExpiredBackups() {
    // Get current timestamp.
    Date now = new Date();
    List<Backup> expiredBackups = Backup.find.query().where()
      .lt("expiry", now)
      .eq("state", BackupState.Completed)
      .findList();

    Map<UUID, List<Backup>> expiredBackupsByCustomerUUID = new HashMap<>();
    for (Backup backup : expiredBackups) {
      expiredBackupsByCustomerUUID.putIfAbsent(backup.customerUUID, new ArrayList<>());
      expiredBackupsByCustomerUUID.get(backup.customerUUID).add(backup);
    }

    Map<Customer, List<Backup>> ret = new HashMap<>();
    expiredBackupsByCustomerUUID.forEach((customerUUID, backups) -> {
      Customer customer = Customer.get(customerUUID);
      Set<UUID> allUniverseUUIDs = Universe.getAllUUIDs(customer);
      List<Backup> backupsWithValidUniv = backups.stream()
        .filter(backup -> allUniverseUUIDs.contains(backup.getBackupInfo().universeUUID))
        .collect(Collectors.toList());
      ret.put(customer, backupsWithValidUniv);
    });
    return ret;
  }

  public void transitionState(BackupState newState) {
    // We only allow state transition from InProgress to a valid state
    // Or completed to deleted state.
    if ((this.state == BackupState.InProgress && this.state != newState) ||
        (this.state == BackupState.Completed && newState == BackupState.Deleted) ||
        (this.state == BackupState.Completed && newState == BackupState.FailedToDelete)) {
      this.state = newState;
      save();
    } else {
      LOG.error("Ignored INVALID STATE TRANSITION  {} -> {}", state, newState);
    }
  }

  public static boolean existsStorageConfig(UUID customerConfigUUID) {
    List<Backup> backupList = find.query().where()
        .or()
          .eq("state", BackupState.Completed)
          .eq("state", BackupState.InProgress)
        .endOr()
        .findList();
    backupList = backupList.stream()
        .filter(b -> b.getBackupInfo().storageConfigUUID.equals(customerConfigUUID))
        .collect(Collectors.toList());
    return backupList.size() != 0;
  }
}
