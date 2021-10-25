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

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.NodeInstance;
import java.util.Map;
import javax.inject.Inject;

/** Actually now this is fake task, that just must fail */
public class PrecheckNode extends UniverseTaskBase {
  @Inject
  protected PrecheckNode(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for failed precheck task.
  public static class Params extends UniverseTaskParams {
    // Map of node names to error messages.
    public Map<String, String> failedNodeNamesToError;
    // Whether nodes should remain reserved or not.
    public boolean reserveNodes = false;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    String errMsg = "";
    for (Map.Entry<String, String> entry : taskParams().failedNodeNamesToError.entrySet()) {
      NodeInstance node = NodeInstance.getByName(entry.getKey());
      if (!taskParams().reserveNodes) {
        try {
          node.clearNodeDetails();
        } catch (RuntimeException e) {
          continue;
        }
      }

      errMsg +=
          String.format(
              "\n-----\nNode %s (%s) failed preflight checks:\n%s",
              node.getInstanceName(), node.getDetails().ip, entry.getValue());
    }

    throw new RuntimeException(errMsg);
  }
}
