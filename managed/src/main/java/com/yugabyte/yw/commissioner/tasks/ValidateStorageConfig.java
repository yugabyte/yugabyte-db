// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import api.v2.handlers.StorageValidationErrors;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.TaskExecutionException;
import com.yugabyte.yw.common.customer.config.UnsavedStorageConfigs;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.helpers.CustomerConfigValidator;
import com.yugabyte.yw.models.helpers.YBAError.Code;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

/**
 * Checks a storage config created or validated through the v2 API: runs the storage validators,
 * which check the locations and reach out to the storage with the config's credentials (unless
 * yb.storage_config.skip_validation is set). They block on cloud SDK calls, which is why they run
 * in a task rather than on the request.
 *
 * <p>For a create, the config is saved in state Validating: the task makes it Active when the check
 * passes, and deletes it, which frees its name, when the check fails. For a dry run, the unsaved
 * config is in {@link UnsavedStorageConfigs}, not in the params, which task_info stores as plain
 * JSON. A failed check fails the task with the v2 error code in its message, see {@link
 * StorageValidationErrors#toTaskError}.
 */
@Slf4j
public class ValidateStorageConfig extends AbstractTaskBase {

  private final CustomerConfigValidator configValidator;
  private final UnsavedStorageConfigs unsavedConfigs;

  @Inject
  public ValidateStorageConfig(
      BaseTaskDependencies baseTaskDependencies,
      CustomerConfigValidator configValidator,
      UnsavedStorageConfigs unsavedConfigs) {
    super(baseTaskDependencies);
    this.configValidator = configValidator;
    this.unsavedConfigs = unsavedConfigs;
  }

  public static class Params extends AbstractTaskParams {
    public UUID customerUUID;

    // The saved config to check or, for a dry run, the key of the unsaved one in
    // UnsavedStorageConfigs.
    public UUID configUUID;

    public boolean dryRun;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    if (params().dryRun) {
      checkUnsaved();
    } else {
      checkSaved();
    }
  }

  private void checkUnsaved() {
    try {
      CustomerConfig config = unsavedConfigs.get(params().configUUID);
      if (config == null) {
        throw new TaskExecutionException(
            Code.INTERNAL_ERROR,
            "The storage config to validate is no longer available, as YBA restarted after the"
                + " request. Validate it again.");
      }
      check(config);
    } finally {
      unsavedConfigs.remove(params().configUUID);
    }
  }

  private void checkSaved() {
    CustomerConfig config = CustomerConfig.get(params().customerUUID, params().configUUID);
    if (config == null) {
      throw new TaskExecutionException(
          Code.INTERNAL_ERROR, "Storage config " + params().configUUID + " no longer exists");
    }
    try {
      check(config);
    } catch (RuntimeException e) {
      deleteIfValidating();
      throw e;
    }
    // A no-op if the config was queued for deletion meanwhile.
    config.updateState(ConfigState.Active);
    log.info("Storage config {} passed its storage check", config.getConfigUUID());
  }

  private void check(CustomerConfig config) {
    try {
      configValidator.validateConfigDataWithValidator(config);
    } catch (Exception e) {
      throw StorageValidationErrors.toTaskError(e);
    }
  }

  // Re-read, so that a config that left the Validating state meanwhile, e.g. queued for deletion
  // through v1, is left alone.
  private void deleteIfValidating() {
    CustomerConfig config = CustomerConfig.get(params().customerUUID, params().configUUID);
    if (config != null && config.getState() == ConfigState.Validating) {
      log.info(
          "Deleting storage config {} ({}), which failed its storage check",
          config.getConfigUUID(),
          config.getConfigName());
      config.delete();
    }
  }
}
