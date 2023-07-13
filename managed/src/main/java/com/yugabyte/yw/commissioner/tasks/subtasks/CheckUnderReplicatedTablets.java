// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.base.Stopwatch;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.NodeUIApiHelper;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class CheckUnderReplicatedTablets extends UniverseTaskBase {

  private static final Duration RETRY_WAIT_TIME = Duration.ofSeconds(10);

  private static final String URL_SUFFIX = "/api/v1/tablet-under-replication";

  private final ApiHelper apiHelper;

  public static class Params extends UniverseTaskParams {
    public Duration maxWaitTime;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Inject
  protected CheckUnderReplicatedTablets(
      BaseTaskDependencies baseTaskDependencies, NodeUIApiHelper apiHelper) {
    super(baseTaskDependencies);
    this.apiHelper = apiHelper;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    int iterationNum = 0;
    Stopwatch stopwatch = Stopwatch.createStarted();
    Duration maxSubtaskTimeout = taskParams().maxWaitTime;
    Duration currentElapsedTime;
    int numUnderReplicatedTablets = 0;
    JsonNode underReplicatedTabletsJson;
    JsonNode errors;
    Map<String, Integer> ipHttpPortMap =
        universe.getNodes().stream()
            .collect(
                Collectors.toMap(node -> node.cloudInfo.private_ip, node -> node.masterHttpPort));

    // Find universe's master UI endpoints (may have custom https ports).
    List<HostAndPort> hp =
        Arrays.stream(universe.getMasterAddresses().split(","))
            .map(HostAndPort::fromString)
            .map(HostAndPort::getHost)
            .filter(host -> ipHttpPortMap.containsKey(host))
            .map(host -> HostAndPort.fromParts(host, ipHttpPortMap.get(host)))
            .collect(Collectors.toList());

    if (hp.size() == 0) {
      throw new RuntimeException(
          String.format(
              "%s failed. No masters found for universe %s",
              getName(), universe.getUniverseUUID()));
    }
    log.debug("Master UI addresses to use: {}", hp);

    int hostIndex = new Random().nextInt(hp.size());
    while (true) {
      currentElapsedTime = stopwatch.elapsed();
      try {
        // Round robin to select master UI endpoint.
        HostAndPort currentHostPort = hp.get(hostIndex);
        hostIndex = (hostIndex + 1) % hp.size();
        String url = String.format("http://%s%s", currentHostPort.toString(), URL_SUFFIX);
        log.debug("Making url request to endpoint: {}", url);
        underReplicatedTabletsJson = apiHelper.getRequest(url);
        errors = underReplicatedTabletsJson.get("error");
        if (errors != null) {
          log.warn("Url request: {} failed. Error: {}, iteration: {}", url, errors, iterationNum);
        } else {
          UnderReplicatedTabletsResp underReplicatedTabletsResp =
              Json.fromJson(underReplicatedTabletsJson, UnderReplicatedTabletsResp.class);
          numUnderReplicatedTablets = underReplicatedTabletsResp.underReplicatedTablets.size();
          if (numUnderReplicatedTablets == 0) {
            log.info("Under-replicated tablets is 0 after {} iterations", iterationNum);
            break;
          }
          log.warn(
              "Under-replicated tablet size not 0, under-replicated tablet size: {}, iteration: {}",
              numUnderReplicatedTablets,
              iterationNum);
        }
      } catch (Exception e) {
        log.error("{} hit error : '{}' after {} iters", getName(), e.getMessage(), iterationNum);
      }

      if (currentElapsedTime.compareTo(maxSubtaskTimeout) > 0) {
        log.info("Timing out after iters={}.", iterationNum);
        throw new RuntimeException(
            String.format(
                "CheckUnderReplicatedTablets, timing out after retrying %s times for "
                    + "a duration of %sms, greater than max time out of %sms. Failing...",
                iterationNum, currentElapsedTime.toMillis(), maxSubtaskTimeout.toMillis()));
      }

      waitFor(RETRY_WAIT_TIME);
      iterationNum++;
    }

    log.debug("{} pre-check passed successfully.", getName());
  }

  public static class UnderReplicatedTabletsResp {

    @JsonProperty("underreplicated_tablets")
    public List<TabletInfo> underReplicatedTablets;

    public static class TabletInfo {

      @JsonProperty("table_uuid")
      public String tableUUID;

      @JsonProperty("tablet_uuid")
      public String tabletUUID;
    }
  }
}
