// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import api.v2.handlers.StorageValidationErrors;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.TaskExecutionException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.customer.config.StorageCheckRunner;
import com.yugabyte.yw.common.customer.config.UnsavedStorageConfigs;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.helpers.CustomerConfigValidator;
import com.yugabyte.yw.models.helpers.YBAError.Code;
import java.time.Duration;
import java.util.UUID;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeoutException;
import lombok.extern.slf4j.Slf4j;

/**
 * Checks a storage config created or validated through the v2 API: runs the storage validators,
 * which check the locations and reach out to the storage with the config's credentials (unless
 * yb.storage_config.skip_validation is set). They block on cloud SDK calls, which is why they run
 * in a task rather than on the request, and on {@link StorageCheckRunner} with a deadline
 * (yb.storage_config.validation_timeout).
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
  private final StorageCheckRunner checkRunner;

  @Inject
  public ValidateStorageConfig(
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
            "The storage config to validate is no longer available. Validate it again.");
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
      deleteIfValidating(config);
      throw e;
    }
    // Not through the bean loaded above: its state is from before the check, so saving it would
    // undo a deletion requested meanwhile.
    if (CustomerConfig.updateStateIfValidating(config.getConfigUUID(), ConfigState.Active)) {
      log.info("Storage config {} passed its storage check", config.getConfigUUID());
    } else {
      log.info(
          "Storage config {} passed its storage check, but left the Validating state meanwhile;"
              + " leaving it as it is",
          config.getConfigUUID());
    }
  }

  // Kept if it left the Validating state meanwhile, e.g. queued for deletion through v1. A failure
  // to delete it is logged, so that the task still fails with the check's error; the config is then
  // deleted at the next startup.
  private void deleteIfValidating(CustomerConfig config) {
    try {
      if (CustomerConfig.deleteIfValidating(config.getConfigUUID())) {
        log.info(
            "Deleted storage config {} ({}), which failed its storage check",
            config.getConfigUUID(),
            config.getConfigName());
      }
    } catch (RuntimeException e) {
      log.error(
          "Failed to delete storage config {} ({}), which failed its storage check",
          config.getConfigUUID(),
          config.getConfigName(),
          e);
    }
  }

  private void check(CustomerConfig config) {
    Duration timeout = confGetter.getGlobalConf(GlobalConfKeys.storageConfigValidationTimeout);
    try {
      checkRunner.run(() -> configValidator.validateConfigDataWithValidator(config), timeout);
    } catch (RejectedExecutionException e) {
      throw new TaskExecutionException(
          Code.INTERNAL_ERROR,
          String.format(
              "%s: All storage checks are busy (yb.%s.max_threads), try again later",
              StorageValidationErrors.VALIDATION_BUSY, StorageCheckRunner.POOL_NAME));
    } catch (TimeoutException e) {
      throw new TaskExecutionException(
          Code.TIMED_OUT,
          String.format(
              "%s: YBA could not check the storage within %s (%s)",
              StorageValidationErrors.PROVIDER_UNAVAILABLE,
              timeout,
              GlobalConfKeys.storageConfigValidationTimeout.getKey()));
    } catch (InterruptedException e) {
      Thread.currentThread().interrupt();
      throw new TaskExecutionException(Code.INTERNAL_ERROR, "Interrupted checking the storage");
    } catch (RuntimeException e) {
      throw StorageValidationErrors.toTaskError(e);
    }
  }
}
