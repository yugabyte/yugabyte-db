package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class BackupStorageConfigValidate extends AbstractTaskBase {

  private final CustomerConfigService configService;

  public static class Params extends AbstractTaskParams {
    public UUID storageConfigUUID;
    public UUID customerUUID;
    public UUID universeUUID;
    public boolean ybcBackup;
  }

  @Override
  public Params taskParams() {
    return (Params) taskParams;
  }

  @Inject
  public BackupStorageConfigValidate(
      BaseTaskDependencies baseTaskDependencies, CustomerConfigService configService) {
    super(baseTaskDependencies);
    this.configService = configService;
  }

  @Override
  public void run() {
    CustomerConfig customerConfig =
        configService.getOrBadRequest(taskParams().customerUUID, taskParams().storageConfigUUID);
    if (!customerConfig.getState().equals(ConfigState.Active)) {
      throw new RuntimeException("Storage config cannot be used as it is not in Active state");
    }
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    if (backupHelper.isSkipConfigBasedPreflightValidation(universe)) {
      return;
    }
    // YBA-side SDK check (e.g. S3 listObjects) then YBC RPC when applicable. Must run as a
    // subtask before Backup.create() so an unusable config does not leave an orphaned Backup
    // row (PLAT-20585).
    backupHelper.validateStorageConfig(customerConfig);
    if (taskParams().ybcBackup) {
      backupHelper.validateStorageConfigForBackupOnUniverse(customerConfig, universe);
    }
  }
}
