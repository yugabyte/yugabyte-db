// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.base.Stopwatch;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.NodeUIApiHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.nodeui.MetricGroup;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.List;
import java.util.Map;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;
import play.libs.Json;

@Slf4j
public class CheckFollowerLag extends ServerSubTaskBase {

  private static final int INITIAL_DELAY_MS = 1000;

  private static final int MAX_DELAY_MS = 130000;

  // Filters endpoint to only show follower lag metrics.
  public static final String URL_SUFFIX = "/metrics?metrics=follower_lag_ms";

  private final ApiHelper apiHelper;

  @Inject
  protected CheckFollowerLag(BaseTaskDependencies baseTaskDependencies, NodeUIApiHelper apiHelper) {
    super(baseTaskDependencies);
    this.apiHelper = apiHelper;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails currentNode = universe.getNode(taskParams().nodeName);
    ServerType serverType = taskParams().serverType;

    if (currentNode == null) {
      String msg = "No node " + taskParams().nodeName + " found in universe " + universe.getName();
      log.error(msg);
      throw new RuntimeException(msg);
    }
    Cluster cluster = universe.getCluster(currentNode.placementUuid);

    // Skip check if process is not master or tserver.
    if (!(serverType == ServerType.MASTER || serverType == ServerType.TSERVER)) {
      log.warn(
          "Skipping follower lag check due to server type not a master or tserver process. Got {}",
          serverType.toString());
    }

    // Skip check if node is not in any of the clusters.
    if (cluster == null) {
      log.warn(
          "Skipping follower lag check due to node {} not being in any cluster in universe {}",
          currentNode.nodeName,
          universe.getName());
      return;
    }

    // Max timeout to wait for check to complete.
    Duration maxSubtaskTimeout =
        confGetter.getConfForScope(universe, UniverseConfKeys.followerLagTimeout);

    // Max threshold for follower lag.
    long maxAcceptableFollowerLagMs =
        confGetter.getConfForScope(universe, UniverseConfKeys.followerLagMaxThreshold).toMillis();

    HostAndPort currentNodeHostPort =
        HostAndPort.fromParts(
            currentNode.cloudInfo.private_ip,
            serverType == ServerType.MASTER
                ? currentNode.masterHttpPort
                : currentNode.tserverHttpPort);

    String currentNodeUrl =
        String.format("http://%s%s", currentNodeHostPort.toString(), URL_SUFFIX);
    Stopwatch stopwatch = Stopwatch.createStarted();
    int iterationNum = 0;

    while (true) {
      Duration currentElapsedTime = stopwatch.elapsed();
      if (currentElapsedTime.compareTo(maxSubtaskTimeout) > 0) {
        log.info("Timing out after iters={}.", iterationNum);
        throw new RuntimeException(
            String.format(
                "CheckFollowerLag, timing out after retrying %s times for a duration of %sms,"
                    + " greater than max time out of %sms for node: %s. Failing...",
                iterationNum,
                currentElapsedTime.toMillis(),
                maxSubtaskTimeout.toMillis(),
                currentNode.nodeName));
      }

      long sleepTimeMs =
          Util.getExponentialBackoffDelayMs(
              INITIAL_DELAY_MS /* initialDelayMs */,
              MAX_DELAY_MS /* maxDelayMs */,
              iterationNum /* iterationNumber */);

      try {
        log.debug("Making url request to current node's endpoint: {}", currentNodeUrl);
        JsonNode currentNodeMetricsJson = apiHelper.getRequest(currentNodeUrl);
        JsonNode errors = currentNodeMetricsJson.get("error");
        if (errors != null) {
          log.warn(
              "Url request: {} failed. Error: {}, iteration: {}",
              currentNodeUrl,
              errors,
              iterationNum);
        } else {
          ObjectMapper objectMapper = Json.mapper();
          List<MetricGroup> metricGroups =
              objectMapper.readValue(
                  currentNodeMetricsJson.toString(), new TypeReference<List<MetricGroup>>() {});
          Map<String, Long> tabletFollowerLagMap =
              MetricGroup.getTabletFollowerLagMap(metricGroups);

          if (followerLagWithinThreshold(tabletFollowerLagMap, maxAcceptableFollowerLagMs)) {
            break;
          }
        }

      } catch (Exception e) {
        log.error(
            "{} hit error : '{}' after {} iters for node: {}",
            getName(),
            e.getMessage(),
            iterationNum,
            currentNode.nodeName);
      }

      waitFor(Duration.ofMillis(sleepTimeMs));
      iterationNum++;
    }

    log.debug("{} pre-check for node {} passed successfully.", getName(), currentNode.nodeName);
  }

  public static boolean followerLagWithinThreshold(
      Map<String, Long> tabletFollowerLagMap, long threshold) {

    Pair<String, Long> maxFollowerLagFound = maxFollowerLag(tabletFollowerLagMap);
    if (maxFollowerLagFound.getRight() >= threshold) {
      log.debug(
          "Tablet: {} has follower lag: {} ms, greater than expected threshold: {} ms",
          maxFollowerLagFound.getLeft(),
          maxFollowerLagFound.getRight(),
          threshold);
      return false;
    }
    return true;
  }

  /*
   * Returns the largest follower lag in ms along with the tablet id associated to it.
   */
  public static Pair<String, Long> maxFollowerLag(Map<String, Long> tabletFollowerLagMap) {
    Long maxFollowerLagMs = 0L;
    String maxFollowerLagTablet = "";
    for (Map.Entry<String, Long> entry : tabletFollowerLagMap.entrySet()) {
      if (entry.getValue() > maxFollowerLagMs) {
        maxFollowerLagMs = entry.getValue();
        maxFollowerLagTablet = entry.getKey();
      }
    }

    log.debug(
        "Max follower lag: {} ms found on tablet: {}", maxFollowerLagMs, maxFollowerLagTablet);
    return new ImmutablePair<>(maxFollowerLagTablet, maxFollowerLagMs);
  }
}
