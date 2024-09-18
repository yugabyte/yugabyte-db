/*
 * Copyright 2024 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.forms.AbstractTaskParams;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RestoreContinuousBackup extends AbstractTaskBase {

  @Inject
  protected RestoreContinuousBackup(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends AbstractTaskParams {
    public UUID storageConfigUUID;
    public String backupDir;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Dummy exeuction of RestoreContinuousBackup");
    return;
  }
}
