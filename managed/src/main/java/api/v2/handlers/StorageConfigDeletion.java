// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import api.v2.models.StorageConfigUsage;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import javax.annotation.Nullable;
import play.libs.Json;

/**
 * What keeps the v2 delete from deleting a storage config: a running backup, a backup schedule or a
 * DR config using it, and, unless its backups are deleted with it, a completed backup. Checked by
 * the request (409) and again by the task, under the config's row lock. All the lookups are
 * customer-scoped and use the storage_config_uuid indexes.
 */
public final class StorageConfigDeletion {

  public static final String IN_USE = "STORAGE_CONFIG_IN_USE";

  private StorageConfigDeletion() {}

  /** The STORAGE_CONFIG_IN_USE error for the config, or null when it can be deleted. */
  @Nullable
  public static ObjectNode inUseError(UUID customerUUID, UUID configUUID, boolean deleteBackups) {
    int running =
        Backup.countWithStorageConfig(customerUUID, configUUID, List.of(BackupState.InProgress));
    int completed =
        Backup.countWithStorageConfig(customerUUID, configUUID, List.of(BackupState.Completed));
    StorageConfigUsage usage =
        StorageConfigUsages.of(customerUUID, List.of(configUUID)).get(configUUID);
    List<String> reasons = new ArrayList<>();
    if (running > 0) {
      reasons.add(running + " running backup(s)");
    }
    if (!usage.getScheduleUuids().isEmpty()) {
      reasons.add("backup schedule(s) " + usage.getScheduleUuids());
    }
    if (!usage.getDrConfigUuids().isEmpty()) {
      reasons.add("DR config(s) " + usage.getDrConfigUuids());
    }
    if (completed > 0 && !deleteBackups) {
      reasons.add(completed + " completed backup(s); delete them with it with delete_backups");
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
    return error;
  }

  /** Whether backups stored with the config are queued for, or in the middle of, deletion. */
  public static boolean backupsBeingDeleted(UUID customerUUID, UUID configUUID) {
    return Backup.countWithStorageConfig(customerUUID, configUUID, Backup.DELETION_STATES) > 0;
  }
}
