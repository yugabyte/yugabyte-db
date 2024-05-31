/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.EncryptionAtRestKeyParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DestroyEncryptionAtRest extends AbstractTaskBase {

  private final EncryptionAtRestManager keyManager;

  @Inject
  protected DestroyEncryptionAtRest(
      BaseTaskDependencies baseTaskDependencies, EncryptionAtRestManager keyManager) {
    super(baseTaskDependencies);
    this.keyManager = keyManager;
  }

  public static class Params extends EncryptionAtRestKeyParams {
    public UUID customerUUID = null;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      Universe u = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      Customer c = Customer.get(u.getCustomerId());
      if (EncryptionAtRestUtil.getNumUniverseKeys(taskParams().getUniverseUUID()) > 0) {
        keyManager.cleanupEncryptionAtRest(c.getUuid(), taskParams().getUniverseUUID());
      }
    } catch (Exception e) {
      final String errMsg =
          String.format(
              "Error caught cleaning up encryption at rest for universe %s",
              taskParams().getUniverseUUID());
      log.error(errMsg, e);
    }
  }
}
