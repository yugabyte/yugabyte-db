// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.Iterables;
import com.google.common.net.HostAndPort;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckClusterConsistency;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckFollowerLag;
import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.nodeui.MetricGroup;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.schedule.JobConfig.RuntimeParams;
import java.time.Duration;
import java.time.temporal.ChronoUnit;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import lombok.Builder;
import lombok.Getter;
import lombok.ToString;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.tuple.Pair;
import org.yb.client.GetMasterHeartbeatDelaysResponse;
import org.yb.client.YBClient;
import org.yb.util.ServerInfo;
import play.libs.Json;

@Singleton
@Slf4j
// Extend UniverseDefinitionTaskBase to access the methods.
public class AutoMasterFailover extends UniverseDefinitionTaskBase {

  private final CustomerTaskManager customerTaskManager;

  private static final String FOLLOWER_LAG_URL_FORMAT =
      "http://%s:%d/metrics?metrics=follower_lag_ms";

  @Builder
  @Getter
  @ToString
  // The fail-over action to be performed as a result of the detection.
  static class Action {
    @Builder.Default ActionType actionType = ActionType.NONE;
    private TaskType taskType;
    private String nodeName;
    private UUID retryTaskUuid;
    private Duration delay;
  }

  static enum ActionType {
    NONE,
    SUBMIT,
    RETRY
  }

  @Inject
  protected AutoMasterFailover(
      BaseTaskDependencies baseTaskDependencies, CustomerTaskManager customerTaskManager) {
    super(baseTaskDependencies);
    this.customerTaskManager = customerTaskManager;
  }

  @Override
  public void run() {
    throw new UnsupportedOperationException();
  }

  /**
   * It checks if a fail-over task can be performed on the universe. The universe may not be in the
   * state to accept fail-over.
   *
   * @param customer the customer to which the universe belongs.
   * @param universe the universe.
   * @param runtimeParams the runtime params passed by the scheduler.
   * @return empty optional of TaskInfo if there is no fail-over task created, else non-empty.
   */
  public Optional<TaskInfo> maybeFailoverMaster(
      Customer customer, Universe universe, RuntimeParams runtimeParams) {
    try {
      boolean isFailoverEnabled =
          confGetter.getConfForScope(universe, UniverseConfKeys.enableAutoMasterFailover);
      if (!isFailoverEnabled) {
        log.debug(
            "Skipping automated master failover for universe {} because it is disabled",
            universe.getUniverseUUID(),
            isFailoverEnabled);
        // Let the creator of this schedule handle the life-cycle.
        return Optional.empty();
      }
      if (universe.getUniverseDetails().universePaused) {
        log.debug(
            "Skipping automated master failover for universe {} because it is paused",
            universe.getUniverseUUID());
        // Let the creator of this schedule handle the life-cycle.
        return Optional.empty();
      }
      if (universe.universeIsLocked()) {
        log.info(
            "Skipping master failover for universe {} because it is already being updated",
            universe.getUniverseUUID());
        // Let the creator of this schedule handle the life-cycle.
        return Optional.empty();
      }
      Action action = getAllowedMasterFailoverAction(customer, universe);
      if (action.getActionType() == ActionType.NONE) {
        // Task cannot be run.
        return Optional.empty();
      }
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      CustomerTask customerTask = null;
      if (action.getActionType() == ActionType.SUBMIT) {
        log.debug(
            "Submitting task {} for universe {}", action.getTaskType(), universe.getUniverseUUID());
        if (action.getTaskType() == TaskType.MasterFailover) {
          customerTask = submitMasterFailoverTask(customer, universe, action);
        } else if (action.getTaskType() == TaskType.SyncMasterAddresses) {
          customerTask = submitSyncMasterAddressesTask(customer, universe);
        }
      } else if (action.getActionType() == ActionType.RETRY) {
        if (action.getTaskType() == TaskType.MasterFailover) {
          long retryLimit =
              (long)
                  confGetter.getConfForScope(
                      universe, UniverseConfKeys.autoMasterFailoverMaxTaskRetries);
          if (runtimeParams.getJobSchedule().getFailedCount() > retryLimit) {
            String errMsg =
                String.format(
                    "Retry limit of %d reached for task %s on universe %s",
                    retryLimit, action.getTaskType(), universe.getUniverseUUID());
            log.error(errMsg);
            throw new RuntimeException(errMsg);
          }
        }
        log.debug(
            "Retrying task {} for universe {}", action.getTaskType(), universe.getUniverseUUID());
        customerTask =
            customerTaskManager.retryCustomerTask(
                customer.getUuid(), universeDetails.placementModificationTaskUuid);
      }
      if (customerTask == null) {
        return Optional.empty();
      }
      log.info(
          "Waiting for master failover task to complete for universe {}",
          universe.getUniverseUUID());
      getCommissioner().waitForTask(customerTask.getTaskUUID());
      return TaskInfo.maybeGet(customerTask.getTaskUUID());
    } finally {
      log.info("Master failover check completed for universe {}", universe.getUniverseUUID());
    }
  }

  // Potentially failed master name with the follower lag which has crossed the soft threshold.
  private Map.Entry<String, Long> validateAndGetMaybeFailedNodeName(
      Customer customer, Universe universe) {
    if (universe.getUniverseDetails().universePaused) {
      log.debug(
          "Skipping automated master failover for universe {} because it is paused",
          universe.getUniverseUUID());
      return null;
    }
    // Before performing any advanced checks, ensure that the YBA view of masters is the same as the
    // of the db to be conservative.
    try (YBClient ybClient =
        ybService.getClient(universe.getMasterAddresses(), universe.getCertificateNodetoNode())) {
      checkClusterConsistency(universe, ybClient);
      Map<String, Long> maybeFailedMasters = getMaybeFailedMastersForUniverse(universe, ybClient);
      log.info(
          "Potentially failed masters for universe {}: {}",
          universe.getUniverseUUID(),
          maybeFailedMasters);
      if (maybeFailedMasters.size() > 1) {
        // Currently, we want to be conservative and only perform automated master failover if
        // there is only one failed master. In case there are more than one failed masters, we
        // rely on manual intervention to solve the issue.
        String errMsg =
            String.format(
                "Universe %s has more than one potentially failed master %s",
                universe.getUniverseUUID(), maybeFailedMasters);
        log.info(errMsg);
        throw new IllegalStateException(errMsg);
      }
      int replicationFactor =
          universe.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor;
      if (maybeFailedMasters.size() > replicationFactor / 2) {
        String errMsg =
            String.format(
                "Universe %s has majority pontential master failure %s",
                universe.getUniverseUUID(), maybeFailedMasters);
        log.info(errMsg);
        throw new IllegalStateException(errMsg);
      }
      return Iterables.getFirst(maybeFailedMasters.entrySet(), null);
    } catch (Exception e) {
      log.error(
          "Error in validating failed master nodes for universe {} - {}",
          universe.getUniverseUUID(),
          e.getMessage());
      throw new RuntimeException(e);
    }
  }

  private void checkClusterConsistency(Universe universe, YBClient ybClient) {
    try {
      List<String> errors =
          CheckClusterConsistency.checkCurrentServers(
              ybClient,
              universe,
              null /* skip nodes */,
              false /* strict */,
              false /* cloud enabled */);
      if (!errors.isEmpty()) {
        String errMsg =
            String.format(
                "DB view of the universe %s is different - %s", universe.getUniverseUUID(), errors);
        log.error(errMsg);
        throw new RuntimeException(errMsg);
      }
    } catch (Exception e) {
      String errMsg =
          String.format(
              "Error in checking cluster consistency for universe %s - %s",
              universe.getUniverseUUID(), e.getMessage());
      log.error(errMsg);
      throw new RuntimeException(errMsg);
    }
  }

  /**
   * Helper method to identify masters for a univers that may fail potentially.
   *
   * <p>Two different checks are performed: 1. Master heartbeat delays are checked to make sure that
   * the master is alive and heartbeating to the master leader. 2. Follower lag is checked to make
   * sure that the master can catch up with the leader using WAL logs.
   *
   * @param universe the given universe.
   * @param ybClient the yb client for the universe.
   * @return map of node name to current time lag for the soft check that has failed.
   */
  @VisibleForTesting
  Map<String, Long> getMaybeFailedMastersForUniverse(Universe universe, YBClient ybClient) {
    Long followerLagSoftThreshold =
        confGetter
            .getConfForScope(universe, UniverseConfKeys.autoMasterFailoverFollowerLagSoftThreshold)
            .toMillis();
    Map<String, Long> maybeFailedMasters = new HashMap<>();
    List<ServerInfo> masters = getMasters(ybClient);
    boolean isMasterLeaderPresent = masters.stream().anyMatch(ServerInfo::isLeader);
    if (!isMasterLeaderPresent) {
      log.error("Cannot find a master leader in the universe {}", universe.getUniverseUUID());
      return maybeFailedMasters;
    }
    Map<String, Long> masterHeartbeatDelays = getMasterHeartbeatDelays(ybClient);
    masters.stream()
        .filter(masterInfo -> !masterInfo.isLeader())
        .forEach(
            masterInfo -> {
              String masterUuid = masterInfo.getUuid();
              String ipAddress = masterInfo.getHost();
              NodeDetails node = universe.getNodeByAnyIP(ipAddress);
              if (!masterHeartbeatDelays.containsKey(masterUuid)) {
                // The master heartbeat map does not contain the master, this is a discrepancy as
                // the master list was also fetched from the db. So we want to be conservative and
                // not take any action.
                log.error(
                    "Cannot find heartbeat delay for master {} in the universe {}",
                    node.getNodeName(),
                    universe.getUniverseUUID());
                return;
              }
              long heartbeatDelay = masterHeartbeatDelays.get(masterUuid);
              if (heartbeatDelay > followerLagSoftThreshold) {
                log.error(
                    "Adding master {} in universe {} as hearbeat delay {} exceeds soft threshold"
                        + " {}ms",
                    node.getNodeName(),
                    universe.getUniverseUUID(),
                    heartbeatDelay,
                    followerLagSoftThreshold);
                maybeFailedMasters.put(node.getNodeName(), heartbeatDelay);
                return;
              }
              HostAndPort hp = HostAndPort.fromParts(node.cloudInfo.private_ip, node.masterRpcPort);
              if (ybClient.waitForServer(hp, 5000)) {
                Pair<String, Long> maxFollowerLag =
                    CheckFollowerLag.maxFollowerLag(
                        getFollowerLagMs(ipAddress, node.masterHttpPort));
                if (maxFollowerLag.getRight() > followerLagSoftThreshold) {
                  log.error(
                      "Adding master {} in universe {} as max follower lag {} exceeds soft"
                          + " threshold {}ms",
                      node.getNodeName(),
                      universe.getUniverseUUID(),
                      maxFollowerLag.getRight(),
                      followerLagSoftThreshold);
                  maybeFailedMasters.put(node.getNodeName(), maxFollowerLag.getRight());
                }
              } else {
                // Cannot decide at this time, wait for heartbeat delay to catch it.
                String errMsg =
                    String.format(
                        "Follower lag for master %s in universe %s cannot be fetched",
                        ipAddress, universe.getUniverseUUID());
                log.error(errMsg);
                throw new RuntimeException(errMsg);
              }
            });
    return maybeFailedMasters;
  }

  private Duration getMasterFailoverScheduleDelay(Universe universe, long followerLagMs) {
    Duration hardThreshold =
        confGetter.getConfForScope(
            universe, UniverseConfKeys.autoMasterFailoverFollowerLagHardThreshold);
    Duration diff = hardThreshold.minus(followerLagMs, ChronoUnit.MILLIS);
    return diff.isNegative() ? Duration.ofSeconds(10) : diff;
  }

  public Action getAllowedMasterFailoverAction(Customer customer, Universe universe) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    if (universe.getUniverseDetails().universePaused) {
      log.debug(
          "Skipping automated master failover for universe {} because it is paused",
          universe.getUniverseUUID());
      return Action.builder().actionType(ActionType.NONE).build();
    }
    if (universe.universeIsLocked()) {
      log.info(
          "Skipping master failover for universe {} because it is already being updated",
          universe.getUniverseUUID());
      return Action.builder().actionType(ActionType.NONE).build();
    }
    if (universeDetails.placementModificationTaskUuid == null) {
      // Skip this check for retries.
      boolean allNodesLive = universe.getNodes().stream().allMatch(n -> n.state == NodeState.Live);
      if (!allNodesLive) {
        log.info(
            "Skipping master failover for universe {} because not all nodes are live",
            universe.getUniverseUUID());
        return Action.builder().actionType(ActionType.NONE).build();
      }
    }
    AllowedTasks allowedTasks =
        UniverseTaskBase.getAllowedTasksOnFailure(universeDetails.placementModificationTaskUuid);
    if (!allowedTasks.isRestricted()) {
      boolean autoSyncMasterAddrs =
          universe.getNodes().stream().anyMatch(n -> n.autoSyncMasterAddrs);
      if (autoSyncMasterAddrs) {
        log.info("Sync master addresses is pending for universe {}", universe.getUniverseUUID());
        // Always sync even if another master may have failed.
        // TODO we may want to run this earlier if at least one is up.
        if (areAllTabletServersAlive(universe)) {
          return Action.builder()
              .actionType(ActionType.SUBMIT)
              .taskType(TaskType.SyncMasterAddresses)
              .delay(
                  confGetter.getConfForScope(
                      universe, UniverseConfKeys.autoSyncMasterAddrsTaskDelay))
              .build();
        }
        log.warn(
            "Sync master addresses is skipped as some tservers not alive for universe {}",
            universe.getUniverseUUID());
        return Action.builder().actionType(ActionType.NONE).build();
      }
      Map.Entry<String, Long> maybeFailedMaster =
          validateAndGetMaybeFailedNodeName(customer, universe);
      if (maybeFailedMaster == null) {
        return Action.builder().actionType(ActionType.NONE).build();
      }
      return Action.builder()
          .actionType(ActionType.SUBMIT)
          .taskType(TaskType.MasterFailover)
          .nodeName(maybeFailedMaster.getKey())
          .delay(getMasterFailoverScheduleDelay(universe, maybeFailedMaster.getValue()))
          .build();
    }
    // The universe is restricted.
    if (allowedTasks.getLockedTaskType() == TaskType.SyncMasterAddresses) {
      if (!areAllTabletServersAlive(universe)) {
        return Action.builder().actionType(ActionType.NONE).build();
      }
      return Action.builder()
          .actionType(ActionType.RETRY)
          .taskType(TaskType.SyncMasterAddresses)
          .retryTaskUuid(universeDetails.placementModificationTaskUuid)
          .delay(
              confGetter.getConfForScope(universe, UniverseConfKeys.autoSyncMasterAddrsTaskDelay))
          .build();
    }
    if (allowedTasks.getLockedTaskType() == TaskType.MasterFailover) {
      Map.Entry<String, Long> maybeFailedMaster =
          validateAndGetMaybeFailedNodeName(customer, universe);
      if (maybeFailedMaster != null) {
        TaskInfo taskInfo = TaskInfo.getOrBadRequest(universeDetails.placementModificationTaskUuid);
        JsonNode node = taskInfo.getTaskParams().get("nodeName");
        if (!node.asText().equals(maybeFailedMaster.getKey())) {
          String errMsg =
              String.format(
                  "Failed node names %s and %s do not match",
                  node.asText(), maybeFailedMaster.getKey());
          log.error(errMsg);
          return Action.builder().actionType(ActionType.NONE).build();
        }
      }
      return Action.builder()
          .actionType(ActionType.RETRY)
          .taskType(TaskType.MasterFailover)
          .retryTaskUuid(universeDetails.placementModificationTaskUuid)
          .delay(getMasterFailoverScheduleDelay(universe, maybeFailedMaster.getValue()))
          .build();
    }
    return Action.builder().actionType(ActionType.NONE).build();
  }

  /**
   * Function to get the follower lag metrics.
   *
   * <p>Make a http call to each master to get the follower lag metrics. The follower lag is
   * considered to be the maximum lag among all tablets that the master is responsible for.
   *
   * @param ip The ip address of the master
   * @param port The port of the master
   * @return the maximum follower lag among all tablets that the master is responsible for
   */
  @VisibleForTesting
  Map<String, Long> getFollowerLagMs(String ip, int port) {
    String endpoint = String.format(FOLLOWER_LAG_URL_FORMAT, ip, port);
    log.info("Getting follower lag for endpoint {}", endpoint);
    try {
      JsonNode currentNodeMetricsJson = nodeUIApiHelper.getRequest(endpoint);
      JsonNode errors = currentNodeMetricsJson.get("error");
      if (errors != null) {
        String errMsg =
            String.format("Error getting follower lag for endpoint %s. Error %s", endpoint, errors);
        log.error(errMsg);
        throw new RuntimeException(errMsg);
      }
      List<MetricGroup> metricGroups =
          Json.mapper()
              .readValue(
                  currentNodeMetricsJson.toString(), new TypeReference<List<MetricGroup>>() {});
      return MetricGroup.getTabletFollowerLagMap(metricGroups);
    } catch (Exception e) {
      String errMsg =
          String.format(
              "Error getting follower lag for endpoint %s - %s", endpoint, e.getMessage());
      log.error(errMsg);
      throw new RuntimeException(errMsg);
    }
  }

  private CustomerTask submitMasterFailoverTask(
      Customer customer, Universe universe, Action action) {
    NodeDetails node = universe.getNode(action.getNodeName());
    NodeDetails possibleReplacementCandidate = findReplacementMaster(universe, node);
    if (possibleReplacementCandidate == null) {
      log.error(
          "No replacement master found for node {} in universe {}",
          action.getNodeName(),
          universe.getUniverseUUID());
      return null;
    }
    log.debug(
        "Found a possible replacement master candidate {} for universe {}",
        possibleReplacementCandidate.getNodeName(),
        universe.getUniverseUUID());
    Set<String> leaderlessTablets = getLeaderlessTablets(universe.getUniverseUUID());
    if (CollectionUtils.isNotEmpty(leaderlessTablets)) {
      log.error(
          "Leaderless tablets {} found for universe {}",
          Iterables.limit(leaderlessTablets, 10),
          universe.getUniverseUUID());
      return null;
    }
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.nodeName = action.getNodeName();
    taskParams.expectedUniverseVersion = universe.getVersion();
    taskParams.azUuid = node.azUuid;
    taskParams.placementUuid = node.placementUuid;
    taskParams.clusters = universe.getUniverseDetails().clusters;
    taskParams.rootCA = universe.getUniverseDetails().rootCA;
    // Submit the task to initiate master failover.
    UUID taskUUID = getCommissioner().submit(TaskType.MasterFailover, taskParams);
    log.info(
        "Submitted master failover for universe {} node {}, task uuid = {}.",
        universe.getUniverseUUID(),
        action.getNodeName(),
        taskUUID);
    return CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.MasterFailover,
        universe.getName());
  }

  private CustomerTask submitSyncMasterAddressesTask(Customer customer, Universe universe) {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.expectedUniverseVersion = universe.getVersion();
    taskParams.clusters = universe.getUniverseDetails().clusters;
    taskParams.rootCA = universe.getUniverseDetails().rootCA;
    UUID taskUUID = getCommissioner().submit(TaskType.SyncMasterAddresses, taskParams);
    log.info(
        "Submitted sync master addresses task {} for universe {}",
        taskUUID,
        universe.getUniverseUUID());
    return CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.SyncMasterAddresses,
        universe.getName());
  }

  /**
   * Helper method to get the master heartbeat delays for non-leader masters to the master leader
   * from the DB cluster.
   *
   * @param ybClient the yb client to the DB.
   * @return map of master uuid to heartbeat delay, excluding the master leader leader.
   */
  private Map<String, Long> getMasterHeartbeatDelays(YBClient ybClient) {
    try {
      GetMasterHeartbeatDelaysResponse response = ybClient.getMasterHeartbeatDelays();
      if (response.hasError()) {
        String errMsg =
            String.format("Error in getting master heartbeat delays - %s", response.errorMessage());
        log.error(errMsg);
        throw new RuntimeException(errMsg);
      }
      return response.getMasterHeartbeatDelays();
    } catch (Exception e) {
      String errMsg =
          String.format("Error in getting master heartbeat delays - %s", e.getMessage());
      log.error(errMsg);
      throw new RuntimeException(errMsg);
    }
  }

  private List<ServerInfo> getMasters(YBClient ybClient) {
    try {
      return ybClient.listMasters().getMasters();
    } catch (Exception e) {
      String errMsg = String.format("Error in listing masters - %s", e.getMessage());
      log.error(errMsg);
      throw new RuntimeException(errMsg);
    }
  }

  private boolean areAllTabletServersAlive(Universe universe) {
    Set<NodeDetails> liveTserverNodes = getLiveTserverNodes(universe);
    return liveTserverNodes.containsAll(universe.getTServers());
  }
}
