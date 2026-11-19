// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import api.v2.handlers.StorageConfigHandler;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.TaskExecutionException;
import com.yugabyte.yw.common.customer.config.StorageCheckRunner;
import com.yugabyte.yw.common.customer.config.UnsavedStorageConfigs;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.helpers.CustomerConfigValidator;
import com.yugabyte.yw.models.helpers.YBAError.Code;
import io.ebean.DB;
import io.ebean.DuplicateKeyException;
import io.ebean.Transaction;
import java.util.UUID;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;

/**
 * Applies an edit of a storage config made through the v2 API. The stored config stays Active, and
 * in use with its stored settings, while the task checks the edited one with the storage validators
 * (skipped when the data didn't change, i.e. only the name did); the task then applies the edit, or
 * fails and leaves the config as it is.
 *
 * <p>The edited config, with its secrets, is in {@link UnsavedStorageConfigs}, not in the params,
 * which task_info stores as plain JSON. The task applies the edit only if the config is still
 * Active and unchanged (update_time) since the request: another edit, v1 or v2, or a deletion
 * meanwhile fails it with STORAGE_CONFIG_CHANGED.
 */
@Slf4j
public class EditStorageConfig extends AbstractTaskBase {

  public static final String CONFIG_CHANGED = "STORAGE_CONFIG_CHANGED";

  private final CustomerConfigValidator configValidator;
  private final UnsavedStorageConfigs unsavedConfigs;
  private final StorageCheckRunner checkRunner;

  @Inject
  public EditStorageConfig(
      BaseTaskDependencies baseTaskDependencies,
      CustomerConfigValidator configValidator,
      UnsavedStorageConfigs unsavedConfigs,
      StorageCheckRunner checkRunner) {
    super(baseTaskDependencies);
    this.configValidator = configValidator;
    this.unsavedConfigs = unsavedConfigs;
    this.checkRunner = checkRunner;
  }

  public static class Params extends AbstractTaskParams {
    public UUID customerUUID;

    // The stored config to edit.
    public UUID configUUID;

    // The key of the edited config in UnsavedStorageConfigs.
    public UUID editKey;

    // update_time of the stored config when the edit was requested, in milliseconds.
    public long revision;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      CustomerConfig edited = unsavedConfigs.get(params().editKey);
      if (edited == null) {
        throw new TaskExecutionException(
            Code.INTERNAL_ERROR,
            "The edit of storage config "
                + params().configUUID
                + " is no longer available. Edit the configuration again.");
      }
      CustomerConfig stored = CustomerConfig.get(params().customerUUID, params().configUUID);
      checkUnchanged(stored);
      if (edited.getData().equals(stored.getData())) {
        log.info(
            "Storage config {}: only the name changed, skipping its storage check",
            params().configUUID);
      } else {
        ValidateStorageConfig.checkStorage(
            checkRunner, configValidator, confGetter, edited, stored);
      }
      apply(edited);
    } finally {
      unsavedConfigs.remove(params().editKey);
    }
  }

  private void apply(CustomerConfig edited) {
    try (Transaction transaction = DB.beginTransaction()) {
      // Locked, so that no other save gets between the check and the update.
      CustomerConfig row =
          CustomerConfig.find
              .query()
              .forUpdate()
              .where()
              .idEq(params().configUUID)
              .eq("customerUUID", params().customerUUID)
              .findOne();
      checkUnchanged(row);
      row.setConfigName(edited.getConfigName());
      row.setData(edited.getData());
      row.update();
      transaction.commit();
    } catch (DuplicateKeyException e) {
      // Another config took the name after the request checked it.
      throw new TaskExecutionException(
          Code.INTERNAL_ERROR,
          String.format(
              "%s: A configuration named %s already exists",
              StorageConfigHandler.NAME_CONFLICT, edited.getConfigName()));
    }
    log.info("Applied the edit of storage config {}", params().configUUID);
  }

  private void checkUnchanged(@Nullable CustomerConfig config) {
    if (config == null
        || config.getState() != ConfigState.Active
        || config.getUpdateTime().getTime() != params().revision) {
      throw new TaskExecutionException(
          Code.INTERNAL_ERROR,
          String.format(
              "%s: Storage config %s was changed or deleted after the edit was requested; get it"
                  + " and edit it again",
              CONFIG_CHANGED, params().configUUID));
    }
  }
}
