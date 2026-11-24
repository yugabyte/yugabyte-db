// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import api.v2.handlers.StorageConfigDeletion;
import api.v2.handlers.StorageConfigHandler;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.TaskExecutionException;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.helpers.YBAError.Code;
import io.ebean.DB;
import io.ebean.Transaction;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

/**
 * Deletes a storage config for the v2 API. Under the config's row lock, it checks again that
 * nothing uses the config (STORAGE_CONFIG_IN_USE). Without deleteBackups, it removes the config
 * right away, unless backups stored with it are still being deleted: the garbage collector needs
 * the config to delete their files, so the config is queued instead and the collector removes it
 * once they're done. With deleteBackups, it queues the config's backups and the config for the
 * collector, as the v1 DeleteCustomerStorageConfig does.
 */
@Slf4j
public class DeleteStorageConfig extends AbstractTaskBase {

  private static final String IN_USE_PREFIX = StorageConfigDeletion.IN_USE + ": ";

  // Backups that delete_backups leaves alone: running or being stopped, already deleted, or already
  // being deleted.
  private static final Set<BackupState> NOT_TO_QUEUE =
      Sets.union(
              Backup.IN_PROGRESS_STATES,
              Sets.immutableEnumSet(
                  BackupState.Stopping, BackupState.Deleted, BackupState.QueuedForForcedDeletion))
          .immutableCopy();

  @Inject
  public DeleteStorageConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends AbstractTaskParams {
    public UUID customerUUID;
    public UUID configUUID;
    public boolean deleteBackups;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    UUID customerUUID = params().customerUUID;
    UUID configUUID = params().configUUID;
    try (Transaction transaction = DB.beginTransaction()) {
      // Locked, so that an edit can't be applied between the checks and the delete.
      CustomerConfig config =
          CustomerConfig.find
              .query()
              .forUpdate()
              .where()
              .idEq(configUUID)
              .eq("customerUUID", customerUUID)
              .findOne();
      if (config == null) {
        log.info("Storage config {} is already deleted", configUUID);
        return;
      }
      if (config.getState() == ConfigState.QueuedForDeletion) {
        log.info("Storage config {} is already queued for deletion", configUUID);
        return;
      }
      if (config.getState() != ConfigState.Active) {
        throw new TaskExecutionException(
            Code.INTERNAL_ERROR,
            String.format(
                "%s: Storage config %s is %s and can't be deleted",
                StorageConfigHandler.NOT_ACTIVE, configUUID, config.getState()));
      }
      ObjectNode inUse =
          StorageConfigDeletion.inUseError(customerUUID, configUUID, params().deleteBackups);
      if (inUse != null) {
        throw new TaskExecutionException(
            Code.INTERNAL_ERROR, IN_USE_PREFIX + inUse.get("message").asText());
      }
      if (params().deleteBackups) {
        List<Backup> backups =
            Backup.findWithStorageConfigNotIn(customerUUID, configUUID, NOT_TO_QUEUE);
        backups.forEach(backup -> backup.transitionState(BackupState.QueuedForDeletion));
        config.updateState(ConfigState.QueuedForDeletion);
        log.info(
            "Queued storage config {} and {} of its backups for deletion",
            configUUID,
            backups.size());
      } else if (StorageConfigDeletion.backupsBeingDeleted(customerUUID, configUUID)) {
        config.updateState(ConfigState.QueuedForDeletion);
        log.info(
            "Queued storage config {} for deletion: backups stored with it are being deleted",
            configUUID);
      } else {
        config.delete();
        log.info("Deleted storage config {}", configUUID);
      }
      transaction.commit();
    }
  }
}
