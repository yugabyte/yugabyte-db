// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.NodeUniverseContext;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.RetryTaskUntilCondition;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
public class CheckLocale extends UniverseTaskBase {

  private final int TIMEOUT = 30;

  @Inject
  protected CheckLocale(
      BaseTaskDependencies baseTaskDependencies, NodeUniverseManager nodeUniverseManager) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public Set<String> nodeNames;
    // Map by clusterUUID
    public Map<UUID, UniverseDefinitionTaskParams.UserIntent> userIntentMap;
  }

  protected CheckLocale.Params params() {
    return (CheckLocale.Params) taskParams;
  }

  protected Map<UUID, UniverseDefinitionTaskParams.UserIntent> getUserIntentMap() {
    if (params().userIntentMap != null) {
      return params().userIntentMap;
    }
    Map<UUID, UniverseDefinitionTaskParams.UserIntent> result = new HashMap<>();
    return getUniverse().getUniverseDetails().clusters.stream()
        .collect(Collectors.toMap(c -> c.uuid, c -> c.userIntent));
  }

  @Override
  public void run() {
    try {
      Universe universe = getUniverse();
      Map<UUID, UniverseDefinitionTaskParams.UserIntent> userIntentMap = getUserIntentMap();

      if (userIntentMap.values().stream()
          .anyMatch(u -> u.getAllCloudTypes().contains(Common.CloudType.local))) {
        log.info("Skipping check for local provider");
        return;
      }
      if (CollectionUtils.isEmpty(params().nodeNames)) {
        log.info("No nodes were provided for locale validation.");
        return;
      }
      long timeout = TIMEOUT;
      List<String> command = new ArrayList<>();
      command.add("locale");
      command.add("-a");
      command.add("|");
      command.add("grep");
      command.add("-E");
      command.add("-q");
      command.add("\"en_US.utf8|en_US.UTF-8\"");
      command.add("&&");
      command.add("echo");
      command.add("\"Locale is present\"");
      command.add("||");
      command.add("echo");
      command.add("\"Locale is not present\"");
      List<NodeDetails> filteredNodes =
          universe.getNodes().stream()
              .filter(node -> params().nodeNames.contains(node.nodeName))
              .collect(Collectors.toList());
      for (NodeDetails node : filteredNodes) {
        RetryTaskUntilCondition<String> waitForCheck =
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
                            .runCommand(
                                NodeUniverseContext.of(
                                    universe, node, context, userIntentMap.get(node.placementUuid)),
                                command)
                            .processErrors();
                    return response.extractRunCommandOutput();
                  } catch (Exception e) {
                    log.error("Error fetching locale on node " + node.cloudInfo.private_ip, e);
                    return null;
                  }
                },
                // until condition
                locale -> {
                  if (locale == null) {
                    return false;
                  }
                  log.info("Found: " + locale + " on node: " + node.cloudInfo.private_ip);
                  if (!locale.toString().contains("Locale is present")) {
                    throw new PlatformServiceException(
                        INTERNAL_SERVER_ERROR,
                        "Could not find en_US.utf8 or en_US.UTF-8 on node: "
                            + node.cloudInfo.private_ip);
                  }
                  return true;
                });
        if (!waitForCheck.retryUntilCond(
            2 /* delayBetweenRetrySecs */, timeout /* timeoutSecs */)) {
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR, "Failed to fetch locale on node " + node.cloudInfo.private_ip);
        }
      }
      log.info("Validated locale en_US.utf8 or en_US.UTF-8 is present on nodes.");
    } catch (Exception e) {
      log.error("Errored out with: " + e);
      Throwables.propagate(e);
    }
  }
}
