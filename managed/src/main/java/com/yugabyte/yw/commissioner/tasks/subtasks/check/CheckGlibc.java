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
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.springframework.util.CollectionUtils;

@Slf4j
public class CheckGlibc extends UniverseTaskBase {

  private final NodeUniverseManager nodeUniverseManager;
  private final GFlagsValidation gFlagsValidation;

  private final int TIMEOUT = 30;

  @Inject
  protected CheckGlibc(
      BaseTaskDependencies baseTaskDependencies,
      NodeUniverseManager nodeUniverseManager,
      GFlagsValidation gFlagsValidation) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
    this.gFlagsValidation = gFlagsValidation;
  }

  public static class Params extends UniverseTaskParams {
    public String ybSoftwareVersion;
    public Set<String> nodeNames;
  }

  protected CheckGlibc.Params params() {
    return (CheckGlibc.Params) taskParams;
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
      if (CollectionUtils.isEmpty(params().nodeNames)) {
        log.info("No nodes were provided for glibc validation.");
        return;
      }
      Optional<Double> dbGlibcVersion;
      try {
        dbGlibcVersion = gFlagsValidation.getGlibcVersion(params().ybSoftwareVersion);
      } catch (IOException e) {
        log.error("Error: ", e);
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "Error extracting glibc version for db: " + params().ybSoftwareVersion);
      }
      if (!dbGlibcVersion.isPresent()) {
        log.info(params().ybSoftwareVersion + " does not contain glibc version info");
        return;
      }
      long timeout = TIMEOUT;
      List<String> command = new ArrayList<>();
      command.add("getconf");
      command.add("GNU_LIBC_VERSION");
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
                            .runCommand(node, universe, command, context)
                            .processErrors();
                    return response.extractRunCommandOutput();
                  } catch (Exception e) {
                    log.error("Error fetching Glibc on node " + node.cloudInfo.private_ip, e);
                    return null;
                  }
                },
                // until condition
                output -> {
                  if (output == null) {
                    return false;
                  }
                  log.info("Found: " + output + " on node: " + node.cloudInfo.private_ip);
                  String glibc = output.split(" ")[1];
                  if (Double.parseDouble(glibc) < dbGlibcVersion.get()) {
                    throw new PlatformServiceException(
                        INTERNAL_SERVER_ERROR,
                        "Found lower glibc version "
                            + glibc
                            + " than expected "
                            + dbGlibcVersion.get());
                  }
                  return true;
                });
        if (!waitForCheck.retryUntilCond(
            2 /* delayBetweenRetrySecs */, timeout /* timeoutSecs */)) {
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR, "Failed to fetch glibc on node " + node.cloudInfo.private_ip);
        }
      }
      log.info("Validated glibc on nodes.");
    } catch (Exception e) {
      log.error("Errored out with: " + e);
      Throwables.propagate(e);
    }
  }
}
