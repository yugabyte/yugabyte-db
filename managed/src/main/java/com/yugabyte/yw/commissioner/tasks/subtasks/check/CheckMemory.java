// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.RetryTaskUntilCondition;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CheckMemory extends UniverseTaskBase {

  private final NodeUniverseManager nodeUniverseManager;

  @Inject
  protected CheckMemory(
      BaseTaskDependencies baseTaskDependencies, NodeUniverseManager nodeUniverseManager) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
  }

  public static class Params extends UniverseTaskParams {
    public Long memoryLimitKB;
    public String memoryType;
    public List<String> nodeIpList;
  }

  protected CheckMemory.Params params() {
    return (CheckMemory.Params) taskParams;
  }

  @Override
  public void run() {
    try {
      Universe universe = getUniverse();
      UniverseDefinitionTaskParams.Cluster cluster =
          universe.getUniverseDetails().getPrimaryCluster();
      if (cluster.userIntent.providerType == Common.CloudType.local) {
        log.info("Skipping check for local provider");
        return;
      }
      long timeout = confGetter.getConfForScope(universe, UniverseConfKeys.checkMemoryTimeoutSecs);
      List<String> command = new ArrayList<>();
      command.add("awk");
      command.add(String.format("/%s/ {print$2}", params().memoryType));
      command.add("/proc/meminfo");
      for (String nodeIp : params().nodeIpList) {
        NodeDetails node = universe.getNodeByPrivateIP(nodeIp);
        RetryTaskUntilCondition<Long> waitForCheck =
            new RetryTaskUntilCondition<>(
                // task
                () -> {
                  try {
                    ShellProcessContext context =
                        ShellProcessContext.builder()
                            .logCmdOutput(true)
                            .timeoutSecs(timeout / 2)
                            .build();
                    ShellResponse response =
                        nodeUniverseManager
                            .runCommand(node, universe, command, context)
                            .processErrors();
                    return Long.parseLong(response.extractRunCommandOutput());
                  } catch (Exception e) {
                    log.error("Error fetching available memory on node " + nodeIp, e);
                    return -1L;
                  }
                },
                // until condition
                availMemory -> {
                  if (availMemory <= 0) {
                    return false;
                  }
                  log.info(
                      "Found available memory: " + availMemory + "kB available on node: " + nodeIp);
                  if (availMemory < params().memoryLimitKB) {
                    throw new PlatformServiceException(
                        INTERNAL_SERVER_ERROR,
                        "Insufficient memory " + availMemory + "kB available on node " + nodeIp);
                  }
                  return true;
                });
        if (!waitForCheck.retryUntilCond(
            2 /* delayBetweenRetrySecs */, timeout /* timeoutSecs */)) {
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR,
              "Failed to fetch " + params().memoryType + " on node " + nodeIp);
        }
      }
      log.info("Validated Enough memory is available for Upgrade.");
    } catch (Exception e) {
      log.error("Errored out with: " + e);
      Throwables.propagate(e);
    }
  }
}
