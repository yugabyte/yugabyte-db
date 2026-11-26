// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import api.v2.models.StorageConfigUsage;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.ContinuousBackupConfig;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import javax.annotation.Nullable;
import play.libs.Json;

/**
 * What keeps the v2 delete from deleting a storage config: a running (or stopping) backup, a backup
 * schedule, a DR config or YBA's continuous backup using it, and, unless its backups are deleted
 * with it, a completed backup or one whose delete failed. Checked by the request (409) and again by
 * the task, under the config's row lock; the lock keeps out edits, but not a backup that a request
 * accepted just before the task ran and whose row its task hasn't inserted yet (that backup then
 * fails, as with the v1 delete). The backup lookups are customer-scoped and use the
 * storage_config_uuid index; DR configs are looked up by the config's UUID, and schedules by the
 * customer and the config UUID in their task params.
 */
public final class StorageConfigDeletion {

  public static final String IN_USE = "STORAGE_CONFIG_IN_USE";

  // Backups whose task still writes to the storage: a backup being stopped runs until it's Stopped.
  public static final List<BackupState> RUNNING =
      List.of(BackupState.InProgress, BackupState.Stopping);

  private StorageConfigDeletion() {}

  /** The STORAGE_CONFIG_IN_USE error for the config, or null when it can be deleted. */
  @Nullable
  public static ObjectNode inUseError(UUID customerUUID, UUID configUUID, boolean deleteBackups) {
    int running = Backup.countWithStorageConfig(customerUUID, configUUID, RUNNING);
    int completed =
        Backup.countWithStorageConfig(customerUUID, configUUID, List.of(BackupState.Completed));
    // Their files are still in the storage: without the config, nothing could delete them.
    int failedToDelete =
        Backup.countWithStorageConfig(
            customerUUID, configUUID, List.of(BackupState.FailedToDelete));
    // YBA's own backup; its row has a foreign key onto the config. YBA-wide, at most a few rows.
    boolean continuousBackup =
        ContinuousBackupConfig.getAll().stream()
            .anyMatch(c -> configUUID.equals(c.getStorageConfigUUID()));
    StorageConfigUsage usage =
        StorageConfigUsages.of(customerUUID, List.of(configUUID)).get(configUUID);
    List<String> reasons = new ArrayList<>();
    if (running > 0) {
      reasons.add(running + " running or stopping backup(s)");
    }
    if (!usage.getScheduleUuids().isEmpty()) {
      reasons.add("backup schedule(s) " + usage.getScheduleUuids());
    }
    if (!usage.getDrConfigUuids().isEmpty()) {
      reasons.add("DR config(s) " + usage.getDrConfigUuids());
    }
    if (continuousBackup) {
      reasons.add("YBA continuous backup");
    }
    if (completed > 0 && !deleteBackups) {
      reasons.add(completed + " completed backup(s); delete them with it with delete_backups");
    }
    if (failedToDelete > 0 && !deleteBackups) {
      reasons.add(
          failedToDelete
              + " backup(s) whose delete failed; delete them with it with delete_backups");
    }
    if (reasons.isEmpty()) {
      return null;
    }
    ObjectNode error = Json.newObject();
    error.put("code", IN_USE);
    error.put(
        "message",
        String.format(
            "Storage config %s is in use and can't be deleted: %s",
            configUUID, String.join(", ", reasons)));
    error.set("usage", Json.toJson(usage));
    error.put("running_backups", running);
    error.put("completed_backups", completed);
    error.put("failed_to_delete_backups", failedToDelete);
    error.put("continuous_backup", continuousBackup);
    return error;
  }

  /** Whether backups stored with the config are queued for, or in the middle of, deletion. */
  public static boolean backupsBeingDeleted(UUID customerUUID, UUID configUUID) {
    return Backup.countWithStorageConfig(customerUUID, configUUID, Backup.DELETION_STATES) > 0;
  }
}
