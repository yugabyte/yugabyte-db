// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.base.Stopwatch;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.NodeUIApiHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import play.libs.Json;

@Slf4j
public class CheckUnderReplicatedTablets extends UniverseTaskBase {

  private static final int INITIAL_DELAY_MS = 1000;

  private static final int MAX_DELAY_MS = 130000;

  public static final String URL_SUFFIX = "/api/v1/tablet-under-replication";

  private static final String MINIMUM_VERSION_UNDERREPLICATED_SUPPORT_2_14 = "2.14.12.0-b1";

  private static final String MINIMUM_VERSION_UNDERREPLICATED_SUPPORT_2_16 = "2.16.7.0-b1";

  private static final String MINIMUM_VERSION_UNDERREPLICATED_SUPPORT_2_18 = "2.18.2.0-b65";

  private static final String MINIMUM_VERSION_UNDERREPLICATED_SUPPORT = "2.19.1.0-b291";

  private final ApiHelper apiHelper;

  public static class Params extends NodeTaskParams {
    public Duration maxWaitTime;

    @Nullable public String targetSoftwareVersion;
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
    String softwareVersion =
        taskParams().targetSoftwareVersion != null
            ? taskParams().targetSoftwareVersion
            : universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;

    log.debug("Current master db software version {}", softwareVersion);
    if (!supportsUnderReplicatedCheck(softwareVersion)) {
      log.debug(
          "Under-replicated tablets check skipped for universe {}. Universe version {} "
              + "does not support under-replicated tablets check.",
          universe.getName(),
          softwareVersion);
      return;
    }
    if (CheckNodesAreSafeToTakeDown.isApiSupported(
            universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion)
        && CheckNodesAreSafeToTakeDown.isApiSupported(softwareVersion)) {
      log.debug("Skipping check, CheckNodesAreSafeToTakeDown should have been applied");
      return;
    }

    NodeDetails currentNode = universe.getNode(taskParams().nodeName);

    if (currentNode == null) {
      String msg = "No node " + taskParams().nodeName + " found in universe " + universe.getName();
      log.error(msg);
      throw new RuntimeException(msg);
    }
    Cluster cluster = universe.getCluster(currentNode.placementUuid);
    int iterationNum = 0;
    Stopwatch stopwatch = Stopwatch.createStarted();
    Duration maxSubtaskTimeout = taskParams().maxWaitTime;
    Duration currentElapsedTime;
    int numUnderReplicatedTablets = 0;
    JsonNode underReplicatedTabletsJson;
    long sleepTimeMs;
    JsonNode errors;
    String url = "";

    // Skip check if node is not in any of the clusters.
    if (cluster == null) {
      log.info(
          "Skipping under-replicated tablets check due to node {} "
              + "not being in any cluster in universe {}",
          currentNode.nodeName,
          universe.getName());
      return;
    }

    // Skip check if node is from a non-primary/read-replica cluster as we do not
    //   require quorum for read-replica clusters.
    if (!cluster.clusterType.equals(ClusterType.PRIMARY)) {
      log.info(
          "Skipping under-replicated tablets check due to node {} not being in the primary cluster",
          currentNode.nodeName);
      return;
    }

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
      sleepTimeMs =
          Util.getExponentialBackoffDelayMs(
              INITIAL_DELAY_MS /* initialDelayMs */,
              MAX_DELAY_MS /* maxDelayMs */,
              iterationNum /* iterationNumber */);
      try {
        // Round robin to select master UI endpoint.
        HostAndPort currentHostPort = hp.get(hostIndex);
        hostIndex = (hostIndex + 1) % hp.size();
        url = String.format("http://%s%s", currentHostPort.toString(), URL_SUFFIX);
        log.debug("Making url request to endpoint: {}", url);
        underReplicatedTabletsJson = apiHelper.getRequest(url);
        errors = underReplicatedTabletsJson.get("error");
        if (errors != null) {
          log.warn("Url request: {} failed. Error: {}, iteration: {}", url, errors, iterationNum);
        } else {
          UnderReplicatedTabletsResp underReplicatedTabletsResp =
              Json.fromJson(underReplicatedTabletsJson, UnderReplicatedTabletsResp.class);
          numUnderReplicatedTablets =
              underReplicatedTabletsResp.numUnderReplicatedTabletsInCluster(cluster);
          if (numUnderReplicatedTablets == 0) {
            log.info("Under-replicated tablets is 0 after {} iterations", iterationNum);
            break;
          }
          log.warn(
              "Under-replicated tablet size not 0, under-replicated tablet size: {}, iteration: {}",
              numUnderReplicatedTablets,
              iterationNum);
          log.warn(
              "Under-replicated tablets: {} ...",
              underReplicatedTabletsResp.getTabletsUnderReplicatedInCluster(cluster).stream()
                  .limit(10)
                  .collect(Collectors.joining(",")));
        }
      } catch (Exception e) {
        log.error("{} hit error : '{}' after {} iters", getName(), e.getMessage(), iterationNum);

        // Skip check for new db versions that do not have this endpoint.
        if (e.getMessage().contains("Error 404")) {
          log.debug(
              "Skipping under-replicated tablets check as endpoint: '{}' "
                  + "is not found in db software version: '{}'.",
              url,
              softwareVersion);
          return;
        }
      }

      if (currentElapsedTime.compareTo(maxSubtaskTimeout) > 0) {
        log.info("Timing out after iters={}.", iterationNum);
        throw new RuntimeException(
            String.format(
                "There are existing under-replicated tablets on the universe. "
                    + "Aborting because this operation can potentially take down a "
                    + "majority of copies of some tablets (CheckUnderReplicatedTablets)."
                    + " If temporary unavailability is acceptable, you can "
                    + " adjust the runtime configs "
                    + UniverseConfKeys.underReplicatedTabletsCheckEnabled.getKey()
                    + " , "
                    + UniverseConfKeys.underReplicatedTabletsTimeout.getKey()
                    + " and retry this operation."
                    + " Details: Under-replicated tablet size: %s, timed out after retrying for "
                    + "a duration of %sms, greater than max time out of %sms. ",
                numUnderReplicatedTablets,
                currentElapsedTime.toMillis(),
                maxSubtaskTimeout.toMillis()));
      }

      waitFor(Duration.ofMillis(sleepTimeMs));
      iterationNum++;
    }

    log.debug("{} pre-check passed successfully.", getName());
  }

  private boolean supportsUnderReplicatedCheck(String ybSoftwareVersion) {
    return CommonUtils.isReleaseBetween(
            MINIMUM_VERSION_UNDERREPLICATED_SUPPORT_2_14, "2.15.0.0-b0", ybSoftwareVersion)
        || CommonUtils.isReleaseBetween(
            MINIMUM_VERSION_UNDERREPLICATED_SUPPORT_2_16, "2.17.0.0-b0", ybSoftwareVersion)
        || CommonUtils.isReleaseBetween(
            MINIMUM_VERSION_UNDERREPLICATED_SUPPORT_2_18, "2.19.0.0-b0", ybSoftwareVersion)
        || CommonUtils.isReleaseEqualOrAfter(
            MINIMUM_VERSION_UNDERREPLICATED_SUPPORT, ybSoftwareVersion);
  }

  public static class UnderReplicatedTabletsResp {

    @JsonProperty("underreplicated_tablets")
    public List<TabletInfo> underReplicatedTablets;

    public static class TabletInfo {

      @JsonProperty("table_uuid")
      public String tableUUID;

      @JsonProperty("tablet_uuid")
      public String tabletUUID;

      @JsonProperty("underreplicated_placements")
      public List<String> underReplicatedClusters;
    }

    public int numUnderReplicatedTabletsInCluster(Cluster cluster) {

      // Older db versions will not have underReplicatedClusters set.
      if (underReplicatedTablets.size() == 0
          || underReplicatedTablets.get(0).underReplicatedClusters == null) {
        return underReplicatedTablets.size();
      }

      return (int)
          underReplicatedTablets.stream()
              .filter(tablet -> tablet.underReplicatedClusters.contains(cluster.uuid.toString()))
              .count();
    }

    public List<String> getTabletsUnderReplicatedInCluster(Cluster cluster) {
      if (CollectionUtils.isEmpty(underReplicatedTablets)) {
        return Collections.emptyList();
      }

      // Older db versions will not have underReplicatedClusters set.
      if (underReplicatedTablets.get(0).underReplicatedClusters == null) {
        return underReplicatedTablets.stream()
            .map(tablet -> tablet.tabletUUID)
            .collect(Collectors.toList());
      }

      return underReplicatedTablets.stream()
          .filter(tablet -> tablet.underReplicatedClusters.contains(cluster.uuid.toString()))
          .map(tablet -> tablet.tabletUUID)
          .collect(Collectors.toList());
    }
  }
}
