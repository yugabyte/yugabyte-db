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
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.AllowedTasks;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckClusterConsistency;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckFollowerLag;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.NodeUIApiHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.nodeui.MetricGroup;
import com.yugabyte.yw.common.services.YBClientService;
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
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import lombok.Builder;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.client.GetMasterHeartbeatDelaysResponse;
import org.yb.client.YBClient;
import org.yb.util.ServerInfo;
import play.libs.Json;

@Singleton
@Slf4j
public class AutoMasterFailover {
  private final RuntimeConfGetter confGetter;
  private final YBClientService ybClientService;
  private final ApiHelper apiHelper;
  private final Commissioner commissioner;
  private final CustomerTaskManager customerTaskManager;

  private static final String FOLLOWER_LAG_URL_FORMAT =
      "http://%s:%d/metrics?metrics=follower_lag_ms";

  private static final String TABLET_SERVERS_URL_FORMAT = "http://%s:%d/api/v1/tablet-servers";

  @Builder
  @Getter
  // The fail-over action to be performed as a result of the detection.
  static class Action {
    @Builder.Default ActionType actionType = ActionType.NONE;
    private TaskType taskType;
    private String nodeName;
    private UUID retryTaskUuid;
  }

  static enum ActionType {
    NONE,
    SUBMIT,
    RETRY
  }

  @Inject
  public AutoMasterFailover(
      RuntimeConfGetter confGetter,
      YBClientService ybClientService,
      NodeUIApiHelper apiHelper,
      Commissioner commissioner,
      CustomerTaskManager customerTaskManager) {
    this.confGetter = confGetter;
    this.ybClientService = ybClientService;
    this.apiHelper = apiHelper;
    this.commissioner = commissioner;
    this.customerTaskManager = customerTaskManager;
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
          customerTask = submitMasterFailoverTask(customer, universe, action.getNodeName());
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
            // Disable the schedule but keep it to track the failure count.
            runtimeParams
                .getJobScheduler()
                .disableSchedule(runtimeParams.getJobSchedule().getUuid(), true);
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
      commissioner.waitForTask(customerTask.getTaskUUID());
      return TaskInfo.maybeGet(customerTask.getTaskUUID());
    } finally {
      log.info("Master failover check completed for universe {}", universe.getUniverseUUID());
    }
  }

  private String validateAndGetFailedNodeName(Customer customer, Universe universe) {
    if (universe.getUniverseDetails().universePaused) {
      log.debug(
          "Skipping automated master failover for universe {} because it is paused",
          universe.getUniverseUUID());
      return null;
    }
    // Before performing any advanced checks, ensure that the YBA view of masters is the same as the
    // of the db to be conservative.
    try (YBClient ybClient =
        ybClientService.getClient(
            universe.getMasterAddresses(), universe.getCertificateNodetoNode())) {
      checkClusterConsistency(universe, ybClient);
      Set<String> failedMasters = getFailedMastersForUniverse(universe, ybClient);
      log.info("Failed masters for universe {}: {}", universe.getUniverseUUID(), failedMasters);
      if (failedMasters.size() > 1) {
        // Currently, we want to be conservative and only perform automated master failover if
        // there is only one failed master. In case there are more than one failed masters, we
        // rely on manual intervention to solve the issue.
        String errMsg =
            String.format(
                "Universe %s has more than one failed master %s",
                universe.getUniverseUUID(), failedMasters);
        log.info(errMsg);
        throw new IllegalStateException(errMsg);
      }
      int replicationFactor =
          universe.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor;
      if (failedMasters.size() > replicationFactor / 2) {
        String errMsg =
            String.format(
                "Universe %s has majority master failure %s",
                universe.getUniverseUUID(), failedMasters);
        log.info(errMsg);
        throw new IllegalStateException(errMsg);
      }
      return Iterables.getFirst(failedMasters, null);
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
   * Helper method to identify failed masters for a universe.
   *
   * <p>Two different checks are performed: 1. Master heartbeat delays are checked to make sure that
   * the master is alive and heartbeating to the master leader. 2. Follower lag is checked to make
   * sure that the master can catch up with the leader using WAL logs.
   *
   * @param universe The universe object
   * @param ybClient The yb client object for the universe, to make rpc calls
   * @return list of node names on which the master has been identified as failed
   */
  @VisibleForTesting
  Set<String> getFailedMastersForUniverse(Universe universe, YBClient ybClient) {
    Long maxAcceptableFollowerLagMs =
        confGetter
            .getConfForScope(universe, UniverseConfKeys.autoMasterFailoverMaxMasterFollowerLag)
            .toMillis();
    Long maxMasterHeartbeatDelayMs =
        confGetter
            .getConfForScope(universe, UniverseConfKeys.autoMasterFailoverMaxMasterHeartbeatDelay)
            .toMillis();
    Set<String> failedMasters = new HashSet<>();
    List<ServerInfo> masters = getMasters(ybClient);
    boolean isMasterLeaderPresent = masters.stream().anyMatch(ServerInfo::isLeader);
    if (!isMasterLeaderPresent) {
      log.error("Cannot find a master leader in the universe {}", universe.getUniverseUUID());
      return failedMasters;
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
                    ipAddress,
                    universe.getUniverseUUID());
                return;
              }
              if (masterHeartbeatDelays.get(masterUuid) > maxMasterHeartbeatDelayMs) {
                log.error(
                    "Failing master {} in universe {} as hearbeat delay exceeds threshold {}ms",
                    masterInfo.getHost(),
                    masterUuid,
                    universe.getUniverseUUID(),
                    maxMasterHeartbeatDelayMs);
                failedMasters.add(node.getNodeName());
                return;
              }
              HostAndPort hp = HostAndPort.fromParts(node.cloudInfo.private_ip, node.masterRpcPort);
              if (ybClient.waitForServer(hp, 5000)) {
                Map<String, Long> followerLags = getFollowerLagMs(ipAddress, node.masterHttpPort);
                if (!CheckFollowerLag.followerLagWithinThreshold(
                    followerLags, maxAcceptableFollowerLagMs)) {
                  log.error(
                      "Failing master {} in universe {} as follower lag exceeds threshold {}ms",
                      ipAddress,
                      universe.getUniverseUUID(),
                      maxAcceptableFollowerLagMs);
                  failedMasters.add(node.getNodeName());
                }
              } else {
                log.error(
                    "Failing master {} in universe {} as it is not alive",
                    ipAddress,
                    universe.getUniverseUUID());
                failedMasters.add(node.getNodeName());
              }
            });
    return failedMasters;
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
    boolean allNodesLive = universe.getNodes().stream().allMatch(n -> n.state == NodeState.Live);
    if (!allNodesLive) {
      log.info(
          "Skipping master failover for universe {} because not all nodes are live",
          universe.getUniverseUUID());
      return Action.builder().actionType(ActionType.NONE).build();
    }
    AllowedTasks allowedTasks =
        UniverseTaskBase.getAllowedTasksOnFailure(universeDetails.placementModificationTaskUuid);
    if (!allowedTasks.isRestricted()) {
      boolean autoSyncMasterAddrs =
          universe.getNodes().stream().anyMatch(n -> n.autoSyncMasterAddrs);
      if (autoSyncMasterAddrs) {
        // Always sync even if another master may have failed.
        return areAllTabletServersAlive(universe)
            ? Action.builder()
                .actionType(ActionType.SUBMIT)
                .taskType(TaskType.SyncMasterAddresses)
                .build()
            : Action.builder().actionType(ActionType.NONE).build();
      }
      String failedNodeName = validateAndGetFailedNodeName(customer, universe);
      return failedNodeName == null
          ? Action.builder().actionType(ActionType.NONE).build()
          : Action.builder()
              .actionType(ActionType.SUBMIT)
              .taskType(TaskType.MasterFailover)
              .nodeName(failedNodeName)
              .build();
    }
    // The universe is restricted.
    if (allowedTasks.getLockedTaskType() == TaskType.SyncMasterAddresses) {
      return areAllTabletServersAlive(universe)
          ? Action.builder()
              .actionType(ActionType.RETRY)
              .taskType(TaskType.SyncMasterAddresses)
              .retryTaskUuid(universeDetails.placementModificationTaskUuid)
              .build()
          : Action.builder().actionType(ActionType.NONE).build();
    }
    if (allowedTasks.getLockedTaskType() == TaskType.MasterFailover) {
      String failedNodeName = validateAndGetFailedNodeName(customer, universe);
      if (failedNodeName != null) {
        TaskInfo taskInfo = TaskInfo.getOrBadRequest(universeDetails.placementModificationTaskUuid);
        JsonNode node = taskInfo.getTaskParams().get("nodeName");
        if (!node.asText().equals(failedNodeName)) {
          String errMsg =
              String.format(
                  "Failed node names %s and %s do not match", node.asText(), failedNodeName);
          log.error(errMsg);
          return Action.builder().actionType(ActionType.NONE).build();
        }
      }
      return Action.builder()
          .actionType(ActionType.RETRY)
          .taskType(TaskType.MasterFailover)
          .retryTaskUuid(universeDetails.placementModificationTaskUuid)
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
    log.info("Getting follower lag for endpoint {} {}", endpoint, apiHelper);
    try {
      JsonNode currentNodeMetricsJson = apiHelper.getRequest(endpoint);
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
      Customer customer, Universe universe, String failedNodeName) {
    CustomerTask lastTask = CustomerTask.getLastTaskByTargetUuid(universe.getUniverseUUID());
    if (lastTask != null && lastTask.getCompletionTime() != null) {
      // Cooldown is calculated from the last task.
      Duration cooldownPeriod =
          confGetter.getConfForScope(universe, UniverseConfKeys.autoMasterFailoverCooldown);
      Instant restrictionEndTime =
          lastTask
              .getCompletionTime()
              .toInstant()
              .plus(cooldownPeriod.getSeconds(), ChronoUnit.SECONDS);
      if (restrictionEndTime.isAfter(Instant.now())) {
        log.info("Universe {} is cooling down", universe.getUniverseUUID());
        return null;
      }
    }

    NodeTaskParams taskParams = new NodeTaskParams();
    NodeDetails node = universe.getNode(failedNodeName);
    NodeDetails possibleReplacementCandidate =
        UniverseDefinitionTaskBase.findReplacementMaster(universe, node);
    if (possibleReplacementCandidate == null) {
      log.error(
          "No replacement master found for node {} in universe {}",
          failedNodeName,
          universe.getUniverseUUID());
      return null;
    }
    log.debug(
        "Found a possible replacement master candidate {} for universe {}",
        possibleReplacementCandidate.getNodeName(),
        universe.getUniverseUUID());
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.nodeName = failedNodeName;
    taskParams.expectedUniverseVersion = universe.getVersion();
    taskParams.azUuid = node.azUuid;
    taskParams.placementUuid = node.placementUuid;
    taskParams.clusters = universe.getUniverseDetails().clusters;
    taskParams.rootCA = universe.getUniverseDetails().rootCA;
    // Submit the task to initiate master failover.
    UUID taskUUID = commissioner.submit(TaskType.MasterFailover, taskParams);
    log.info(
        "Submitted master failover for universe {} node {}, task uuid = {}.",
        universe.getUniverseUUID(),
        failedNodeName,
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
    UUID taskUUID = commissioner.submit(TaskType.SyncMasterAddresses, taskParams);
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
    String masterLeaderIp = universe.getMasterLeaderHostText();
    if (StringUtils.isEmpty(masterLeaderIp)) {
      log.error("Cannot find a master leader in universe {}", universe.getUniverseUUID());
      return false;
    }
    int masterHttpPort = universe.getUniverseDetails().communicationPorts.masterHttpPort;
    String endpoint = String.format(TABLET_SERVERS_URL_FORMAT, masterLeaderIp, masterHttpPort);
    log.info("Getting tablet servers from endpoint {} {}", endpoint, apiHelper);
    try {
      JsonNode tabletServerResponse = apiHelper.getRequest(endpoint);
      JsonNode errors = tabletServerResponse.get("error");
      if (errors != null) {
        log.error(
            "Error tablet servers from endpoint {} for universe {} - {}",
            endpoint,
            universe.getUniverseUUID(),
            errors);
        return false;
      }
      Set<NodeDetails> allTservers = new HashSet<>(universe.getTServers());
      Iterator<Entry<String, JsonNode>> clusterIter = tabletServerResponse.fields();
      while (clusterIter.hasNext()) {
        Entry<String, JsonNode> clusterEntry = clusterIter.next();
        Iterator<Entry<String, JsonNode>> serverIter = clusterEntry.getValue().fields();
        while (serverIter.hasNext()) {
          Entry<String, JsonNode> serverInfo = serverIter.next();
          String ipPort = serverInfo.getKey();
          if (StringUtils.isEmpty(ipPort)) {
            continue;
          }
          String serverIp = ipPort.split(":")[0];
          NodeDetails nodeDetails = universe.getNodeByAnyIP(serverIp);
          if (nodeDetails == null) {
            log.warn(
                "Unknown node with IP {} in universe {}", serverIp, universe.getUniverseUUID());
            continue;
          }
          JsonNode statusNode = serverInfo.getValue().get("status");
          if (statusNode == null || statusNode.isNull()) {
            continue;
          }
          if ("ALIVE".equalsIgnoreCase(statusNode.asText())) {
            allTservers.remove(nodeDetails);
          }
        }
      }
      if (allTservers.isEmpty()) {
        log.debug("All the tservers are alive in universe {}", universe.getUniverseUUID());
        return true;
      }
    } catch (Exception e) {
      log.error(
          "Error in getting live tservers from endpoint {} for universe {} - {}",
          endpoint,
          universe.getUniverseUUID(),
          e.getMessage());
    }
    return false;
  }
}
