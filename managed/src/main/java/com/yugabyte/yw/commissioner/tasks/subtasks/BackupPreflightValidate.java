package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.UUID;

public class BackupPreflightValidate extends AbstractTaskBase {

  private final CustomerConfigService configService;

  public static class Params extends AbstractTaskParams {
    public Params(UUID storageConfigUUID, UUID customerUUID, UUID universeUUID, boolean ybcBackup) {
      this.storageConfigUUID = storageConfigUUID;
      this.customerUUID = customerUUID;
      this.universeUUID = universeUUID;
      this.ybcBackup = ybcBackup;
    }

    public UUID storageConfigUUID;
    public UUID customerUUID;
    public UUID universeUUID;
    public boolean ybcBackup;
  }

  @Override
  public BackupPreflightValidate.Params taskParams() {
    return (BackupPreflightValidate.Params) taskParams;
  }

  @Inject
  public BackupPreflightValidate(
      BaseTaskDependencies baseTaskDependencies, CustomerConfigService configService) {
    super(baseTaskDependencies);
    this.configService = configService;
  }

  @Override
  public void run() {
    if (taskParams().ybcBackup) {
      CustomerConfig storageConfig =
          configService.getOrBadRequest(taskParams().customerUUID, taskParams().storageConfigUUID);
      Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
      backupHelper.validateStorageConfigForBackupOnUniverse(storageConfig, universe);
    }
  }
}
