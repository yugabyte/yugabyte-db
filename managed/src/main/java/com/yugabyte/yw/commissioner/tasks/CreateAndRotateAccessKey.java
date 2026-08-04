package com.yugabyte.yw.commissioner.tasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.ScheduledAccessKeyRotateParams;
import com.yugabyte.yw.common.AccessKeyRotationUtil;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.models.AccessKey;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CreateAndRotateAccessKey extends AbstractTaskBase {

  @Inject AccessKeyRotationUtil accessKeyRotationUtil;
  @Inject AccessManager accessManager;

  @Inject
  protected CreateAndRotateAccessKey(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  protected ScheduledAccessKeyRotateParams taskParams() {
    return (ScheduledAccessKeyRotateParams) taskParams;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    UUID customerUUID = taskParams().getCustomerUUID();
    UUID providerUUID = taskParams().getProviderUUID();
    List<UUID> universeUUIDs = taskParams().getUniverseUUIDs();
    // create a new access key for the scheduled task
    AccessKey newAccessKey =
        accessKeyRotationUtil.createAccessKeyForProvider(customerUUID, providerUUID);
    // create an access key rotation task per universe in the schedule params
    accessManager.rotateAccessKey(
        customerUUID, providerUUID, universeUUIDs, newAccessKey.getKeyCode());
  }
}
