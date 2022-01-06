// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static java.lang.Math.abs;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.CreatedTimestamp;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.UpdatedTimestamp;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@ApiModel(
    description =
        "A single backup. Includes the backup's status, expiration time, and configuration.")
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

    @EnumValue("Stopped")
    Stopped,
  }

  @ApiModelProperty(value = "Backup UUID", accessMode = READ_ONLY)
  @Id
  public UUID backupUUID;

  @ApiModelProperty(value = "Customer UUID that owns this backup", accessMode = READ_WRITE)
  @Column(nullable = false)
  public UUID customerUUID;

  @ApiModelProperty(value = "State of the backup", example = "DELETED", accessMode = READ_ONLY)
  @Column(nullable = false)
  public BackupState state;

  @ApiModelProperty(value = "Details of the backup", accessMode = READ_WRITE)
  @Column(columnDefinition = "TEXT", nullable = false)
  @DbJson
  private BackupTableParams backupInfo;

  @ApiModelProperty(value = "Backup UUID", accessMode = READ_ONLY)
  @Column(unique = true)
  public UUID taskUUID;

  @ApiModelProperty(
      value = "Schedule UUID, if this backup is part of a schedule",
      accessMode = READ_WRITE)
  @Column
  private UUID scheduleUUID;

  public UUID getScheduleUUID() {
    return scheduleUUID;
  }

  @ApiModelProperty(value = "Expiry time (unix timestamp) of the backup", accessMode = READ_WRITE)
  @Column
  // Unix timestamp at which backup will get deleted.
  private Date expiry;

  public Date getExpiry() {
    return expiry;
  }

  public void setBackupInfo(BackupTableParams params) {
    this.backupInfo = params;
  }

  public BackupTableParams getBackupInfo() {
    return this.backupInfo;
  }

  @CreatedTimestamp private Date createTime;

  public Date getCreateTime() {
    return createTime;
  }

  @UpdatedTimestamp private Date updateTime;

  public Date getUpdateTime() {
    return updateTime;
  }

  public static final Finder<UUID, Backup> find = new Finder<UUID, Backup>(Backup.class) {};

  // For creating new backup we would set the storage location based on
  // universe UUID and backup UUID.
  // univ-<univ_uuid>/backup-<timestamp>-<something_to_disambiguate_from_yugaware>/table-keyspace
  // .table_name.table_uuid
  private void updateStorageLocation(BackupTableParams params) {
    CustomerConfig customerConfig = CustomerConfig.get(customerUUID, params.storageConfigUUID);
    if (params.tableUUIDList != null) {
      params.storageLocation =
          String.format(
              "univ-%s/backup-%s-%d/multi-table-%s",
              params.universeUUID,
              tsFormat.format(new Date()),
              abs(backupUUID.hashCode()),
              params.getKeyspace());
    } else if (params.getTableName() == null && params.getKeyspace() != null) {
      params.storageLocation =
          String.format(
              "univ-%s/backup-%s-%d/keyspace-%s",
              params.universeUUID,
              tsFormat.format(new Date()),
              abs(backupUUID.hashCode()),
              params.getKeyspace());
    } else {
      params.storageLocation =
          String.format(
              "univ-%s/backup-%s-%d/table-%s.%s",
              params.universeUUID,
              tsFormat.format(new Date()),
              abs(backupUUID.hashCode()),
              params.getKeyspace(),
              params.getTableName());
      if (params.tableUUID != null) {
        params.storageLocation =
            String.format(
                "%s-%s", params.storageLocation, params.tableUUID.toString().replace("-", ""));
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
    List<Backup> backupList =
        find.query()
            .where()
            .eq("customer_uuid", customerUUID)
            .orderBy("create_time desc")
            .findList();
    return backupList
        .stream()
        .filter(backup -> backup.getBackupInfo().universeUUID.equals(universeUUID))
        .collect(Collectors.toList());
  }

  public static List<Backup> fetchBackupToDeleteByUniverseUUID(
      UUID customerUUID, UUID universeUUID) {
    return fetchByUniverseUUID(customerUUID, universeUUID)
        .stream()
        .filter(b -> b.backupInfo.actionType == BackupTableParams.ActionType.CREATE)
        .collect(Collectors.toList());
  }

  @Deprecated
  public static Backup get(UUID customerUUID, UUID backupUUID) {
    return find.query().where().idEq(backupUUID).eq("customer_uuid", customerUUID).findOne();
  }

  public static Backup getOrBadRequest(UUID customerUUID, UUID backupUUID) {
    Backup backup = get(customerUUID, backupUUID);
    if (backup == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid customer or backup UUID");
    }
    return backup;
  }

  public static List<Backup> fetchAllBackupsByTaskUUID(UUID taskUUID) {
    return Backup.find.query().where().eq("task_uuid", taskUUID).findList();
  }

  public static Map<Customer, List<Backup>> getExpiredBackups() {
    // Get current timestamp.
    Date now = new Date();
    List<Backup> expiredBackups =
        Backup.find.query().where().lt("expiry", now).eq("state", BackupState.Completed).findList();

    Map<UUID, List<Backup>> expiredBackupsByCustomerUUID = new HashMap<>();
    for (Backup backup : expiredBackups) {
      expiredBackupsByCustomerUUID.putIfAbsent(backup.customerUUID, new ArrayList<>());
      expiredBackupsByCustomerUUID.get(backup.customerUUID).add(backup);
    }

    Map<Customer, List<Backup>> ret = new HashMap<>();
    expiredBackupsByCustomerUUID.forEach(
        (customerUUID, backups) -> {
          Customer customer = Customer.get(customerUUID);
          List<Backup> backupList =
              backups
                  .stream()
                  .filter(backup -> !Universe.isUniversePaused(backup.getBackupInfo().universeUUID))
                  .collect(Collectors.toList());
          ret.put(customer, backupList);
        });
    return ret;
  }

  public void transitionState(BackupState newState) {
    // We only allow state transition from InProgress to a valid state
    // Or completed to deleted state.
    if ((this.state == BackupState.InProgress && this.state != newState)
        || (this.state == BackupState.Completed && newState == BackupState.Deleted)
        || (this.state == BackupState.Completed && newState == BackupState.FailedToDelete)
        || (this.state == BackupState.Failed && newState == BackupState.Deleted)
        || (this.state == BackupState.Failed && newState == BackupState.FailedToDelete)) {
      this.state = newState;
      save();
    } else {
      LOG.error("Ignored INVALID STATE TRANSITION  {} -> {}", state, newState);
    }
  }

  public static List<Backup> getInProgressAndCompleted(UUID customerUUID) {
    return find.query()
        .where()
        .eq("customer_uuid", customerUUID)
        .in("state", BackupState.InProgress, BackupState.Completed)
        .or()
        .eq("state", BackupState.Completed)
        .eq("state", BackupState.InProgress)
        .endOr()
        .findList();
  }

  public static List<Backup> findAllFinishedBackupsWithCustomerConfig(UUID customerConfigUUID) {
    List<Backup> backupList =
        find.query()
            .where()
            .or()
            .eq("state", BackupState.Failed)
            .eq("state", BackupState.Completed)
            .endOr()
            .findList();
    backupList =
        backupList
            .stream()
            .filter(b -> b.backupInfo.actionType == BackupTableParams.ActionType.CREATE)
            .filter(b -> b.getBackupInfo().storageConfigUUID.equals(customerConfigUUID))
            .collect(Collectors.toList());
    return backupList;
  }

  public static boolean findIfBackupsRunningWithCustomerConfig(UUID customerConfigUUID) {
    List<Backup> backupList = find.query().where().eq("state", BackupState.InProgress).findList();
    backupList =
        backupList
            .stream()
            .filter(b -> b.getBackupInfo().storageConfigUUID.equals(customerConfigUUID))
            .collect(Collectors.toList());
    return backupList.size() != 0;
  }

  public static Set<Universe> getAssociatedUniverses(UUID customerUUID, UUID configUUID) {
    Set<UUID> universeUUIDs = new HashSet<>();
    List<Backup> backupList = getInProgressAndCompleted(customerUUID);
    backupList =
        backupList
            .stream()
            .filter(
                b ->
                    b.getBackupInfo().storageConfigUUID.equals(configUUID)
                        && universeUUIDs.add(b.getBackupInfo().universeUUID))
            .collect(Collectors.toList());

    List<Schedule> scheduleList =
        Schedule.find
            .query()
            .where()
            .in("task_type", TaskType.BackupUniverse, TaskType.MultiTableBackup)
            .eq("status", "Active")
            .findList();
    scheduleList =
        scheduleList
            .stream()
            .filter(
                s ->
                    s.getTaskParams()
                            .path("storageConfigUUID")
                            .asText()
                            .equals(configUUID.toString())
                        && universeUUIDs.add(
                            UUID.fromString(s.getTaskParams().get("universeUUID").asText())))
            .collect(Collectors.toList());
    Set<Universe> universes = new HashSet<>();
    for (UUID universeUUID : universeUUIDs) {
      try {
        universes.add(Universe.getOrBadRequest(universeUUID));
      }
      // Backup is present but universe does not. We are ignoring such backups.
      catch (Exception e) {
      }
    }
    return universes;
  }

  public static List<Backup> fetchAllBackupsByScheduleUUID(UUID customerUUID, UUID scheduleUUID) {
    return find.query()
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("schedule_uuid", scheduleUUID)
        .eq("state", BackupState.Completed)
        .findList();
  }
}
