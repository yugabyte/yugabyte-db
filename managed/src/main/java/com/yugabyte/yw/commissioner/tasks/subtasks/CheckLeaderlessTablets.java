// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.NodeUIApiHelper;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

@Slf4j
public class CheckLeaderlessTablets extends ServerSubTaskBase {

  private static final int INITIAL_DELAY_MS = 1000;
  private static final int MAX_DELAY_MS = 30000;
  private static final int MAX_ERRORS_TO_IGNORE = 10;

  public static final String URL_SUFFIX = "/api/v1/tablet-replication";
  public static final String KEY = "leaderless_tablets";

  private final ApiHelper apiHelper;

  @Inject
  protected CheckLeaderlessTablets(
      BaseTaskDependencies baseTaskDependencies, NodeUIApiHelper apiHelper) {
    super(baseTaskDependencies);
    this.apiHelper = apiHelper;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    if (universe.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor == 1) {
      log.warn("Skipping check for RF1 cluster");
      return;
    }
    String masterAddresses = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    Duration timeout =
        confGetter.getConfForScope(universe, UniverseConfKeys.leaderlessTabletsTimeout);
    int httpPort = universe.getUniverseDetails().communicationPorts.masterHttpPort;
    int initialDelay = INITIAL_DELAY_MS;
    int maxDelay = MAX_DELAY_MS;
    if (taskParams().isRunOnlyPrechecks()) {
      // We need to get only single try.
      initialDelay = 0;
      maxDelay = 1;
      timeout = Duration.ofMillis(1);
    }

    try (YBClient client = ybService.getClient(masterAddresses, certificate)) {
      AtomicInteger errorCnt = new AtomicInteger();
      AtomicReference<List<String>> tablets = new AtomicReference<>();
      boolean result =
          doWithExponentialTimeout(
              initialDelay,
              maxDelay,
              timeout.toMillis(),
              () -> {
                try {
                  tablets.set(doGetLeaderlessTablets(client, httpPort));
                  return tablets.get().isEmpty();
                } catch (Exception e) {
                  if (errorCnt.incrementAndGet() > MAX_ERRORS_TO_IGNORE) {
                    throw new RuntimeException(
                        "Exceeded max errors (" + MAX_ERRORS_TO_IGNORE + ")", e);
                  }
                  log.debug("Error count {}", errorCnt.get());
                  return false;
                }
              });
      if (!result) {
        String runtimeConfigInfo =
            "To proceed with the operation anyway (not recommended),"
                + " disable the runtime config "
                + UniverseConfKeys.leaderlessTabletsCheckEnabled.getKey()
                + " and retry this operation.";
        if (tablets.get() != null) {
          throw new RuntimeException(
              "Aborting operation because the db seems to be unhealthy."
                  + " There are leaderless tablets. "
                  + runtimeConfigInfo
                  + " List of leaderless tablets: "
                  + tablets.get());
        } else {
          throw new RuntimeException(
              "Failed to perform safety check for leaderless tablets. " + runtimeConfigInfo);
        }
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  // TODO Remove this to use the similar method in UniverseTaskBase.
  private List<String> doGetLeaderlessTablets(YBClient client, int httpPort) {
    return doGetLeaderlessTablets(taskParams().getUniverseUUID(), client, apiHelper, httpPort);
  }

  public static List<String> doGetLeaderlessTablets(
      UUID universeUUID, YBClient client, ApiHelper apiHelper, int httpPort) {
    HostAndPort leaderMasterHostAndPort = client.getLeaderMasterHostAndPort();

    if (leaderMasterHostAndPort == null) {
      throw new RuntimeException(
          "Could not find the master leader address in universe " + universeUUID);
    }
    HostAndPort hostAndPort = HostAndPort.fromParts(leaderMasterHostAndPort.getHost(), httpPort);
    String url = String.format("http://%s%s", hostAndPort.toString(), URL_SUFFIX);
    log.debug("Making url request to endpoint: {}", url);
    JsonNode response = apiHelper.getRequest(url);
    log.debug("Received {}", response);
    JsonNode errors = response.get("error");
    if (errors != null) {
      throw new RuntimeException("Received error: " + errors.asText());
    }
    ArrayNode leaderlessTablets = (ArrayNode) response.get(KEY);
    if (leaderlessTablets == null) {
      throw new RuntimeException("Not expected response, no " + KEY + " in it: " + response);
    }
    List<String> result = new ArrayList<>();
    for (JsonNode leaderlessTabletInfo : leaderlessTablets) {
      result.add(leaderlessTabletInfo.get("tablet_uuid").asText());
    }
    return result;
  }
}
