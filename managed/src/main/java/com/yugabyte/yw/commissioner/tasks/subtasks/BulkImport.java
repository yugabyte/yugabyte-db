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
import com.yugabyte.yw.forms.BulkImportParams;
import javax.inject.Inject;

public class BulkImport extends AbstractTaskBase {
  @Inject
  protected BulkImport(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected BulkImportParams taskParams() {
    return (BulkImportParams) taskParams;
  }

  @Override
  public void run() {
    // Execute the ansible command and log its result.
    tableManager.bulkImport(taskParams()).processErrors();
  }
}
