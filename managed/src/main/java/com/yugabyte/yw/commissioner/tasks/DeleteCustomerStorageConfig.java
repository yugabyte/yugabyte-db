// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.helpers.CustomerConfigValidator;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DeleteCustomerStorageConfig extends AbstractTaskBase {

  @Inject
  public DeleteCustomerStorageConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Inject private CustomerConfigService customerConfigService;

  @Inject private CustomerConfigValidator configValidator;

  public static class Params extends AbstractTaskParams {
    public UUID customerUUID;
    public UUID configUUID;
    public Boolean isDeleteBackups;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      CustomerConfig customerConfig =
          customerConfigService.getOrBadRequest(params().customerUUID, params().configUUID);

      Schedule.findAllScheduleWithCustomerConfig(params().configUUID)
          .forEach(Schedule::stopSchedule);

      if (params().isDeleteBackups) {
        List<Backup> backupList =
            Backup.findAllNonProgressBackupsWithCustomerConfig(
                params().configUUID, params().customerUUID);
        backupList.forEach(backup -> backup.transitionState(BackupState.QueuedForDeletion));
        configValidator.validateConfigRemoval(customerConfig);
      }
      customerConfig.updateState(ConfigState.QueuedForDeletion);
    } catch (Exception e) {
      log.error("Error while deleting Configuration {}: ", params().configUUID, e);
      throw new RuntimeException(e.getMessage());
    }
  }
}
