// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.annotation.JsonSerialize;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.backuprestore.ybc.ControllerFlagsSetRequestSerializer;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import org.yb.ybc.ControllerFlagsSetRequest;

@Slf4j
public class SetYbcThrottleParamsInMemory extends UniverseTaskBase {

  private final YbcManager ybcManager;

  @Inject
  protected SetYbcThrottleParamsInMemory(
      BaseTaskDependencies baseTaskDependencies, YbcManager ybcManager) {
    super(baseTaskDependencies);
    this.ybcManager = ybcManager;
  }

  @Setter
  @Getter
  public static class Params extends UniverseTaskParams {
    private UUID universeUUID;
    private List<NodeDetails> nodes;

    @JsonSerialize(using = ControllerFlagsSetRequestSerializer.class)
    private ControllerFlagsSetRequest controllerFlagsRequest;
  }

  @Override
  protected SetYbcThrottleParamsInMemory.Params taskParams() {
    return (SetYbcThrottleParamsInMemory.Params) taskParams;
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());
      ybcManager.setThrottleParamsOnYbcServers(
          getUniverse(), taskParams().nodes, taskParams().controllerFlagsRequest);
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.error(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
