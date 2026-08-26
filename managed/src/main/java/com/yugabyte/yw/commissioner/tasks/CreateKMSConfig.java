/*
 * Copyright 2019 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.models.KmsConfig;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class CreateKMSConfig extends KMSConfigTaskBase {

  @Inject
  protected CreateKMSConfig(
      BaseTaskDependencies baseTaskDependencies, EncryptionAtRestManager kmsManager) {
    super(baseTaskDependencies, kmsManager);
  }

  @Override
  public void run() {
    log.info("Creating KMS Configuration for customer: " + taskParams().customerUUID.toString());

    // A config with this name already exists, so a previous attempt succeeded: nothing to do.
    boolean alreadyExists =
        KmsConfig.listKMSConfigs(taskParams().customerUUID).stream()
            .anyMatch(config -> config.getName().equals(taskParams().kmsConfigName));
    if (alreadyExists) {
      log.info(
          "KMS Configuration '{}' already exists for customer {}, skipping creation",
          taskParams().kmsConfigName,
          taskParams().customerUUID);
      return;
    }

    KmsConfig createResult =
        kmsManager
            .getServiceInstance(taskParams().kmsProvider.name())
            .createAuthConfig(
                taskParams().customerUUID, taskParams().kmsConfigName, taskParams().providerConfig);

    if (createResult == null) {
      throw new RuntimeException(
          String.format(
              "Error creating KMS Configuration for customer %s and kms provider %s",
              taskParams().customerUUID.toString(), taskParams().kmsProvider));
    }
  }
}
