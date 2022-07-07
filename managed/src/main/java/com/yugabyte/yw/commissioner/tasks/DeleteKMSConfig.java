/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DeleteKMSConfig extends KMSConfigTaskBase {

  @Inject
  protected DeleteKMSConfig(
      BaseTaskDependencies baseTaskDependencies, EncryptionAtRestManager kmsManager) {
    super(baseTaskDependencies, kmsManager);
  }

  @Override
  public void run() {
    log.info("Deleting KMS Configuration for customer: " + taskParams().customerUUID.toString());
    kmsManager
        .getServiceInstance(taskParams().kmsProvider.name())
        .deleteKMSConfig(taskParams().configUUID);
  }
}
