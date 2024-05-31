/*
* Copyright 2019 YugaByte, Inc. and Contributors
*
* Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
* may not use this file except in compliance with the License. You
* may obtain a copy of the License at
*
https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
*/

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class WaitForYbcServer extends UniverseTaskBase {

  private final YbcManager ybcManager;

  @Inject
  protected WaitForYbcServer(BaseTaskDependencies baseTaskDependencies, YbcManager ybcManager) {
    super(baseTaskDependencies);
    this.ybcManager = ybcManager;
  }

  public static class Params extends UniverseDefinitionTaskParams {
    // The universe UUID must be stored in universeUUID field.
    // The xCluster info object to persist.
    public Set<String> nodeNameList = null;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    Set<NodeDetails> nodeDetailsSet =
        taskParams().nodeDetailsSet == null
            ? universe.getUniverseDetails().nodeDetailsSet
            : taskParams().nodeNameList.stream()
                .map(nodeName -> universe.getNode(nodeName))
                .collect(Collectors.toSet());

    ybcManager.waitForYbc(universe, nodeDetailsSet);
  }
}
