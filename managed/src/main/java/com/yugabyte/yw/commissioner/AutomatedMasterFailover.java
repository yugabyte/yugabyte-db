package com.yugabyte.yw.commissioner;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckClusterConsistency;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckFollowerLag;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.NodeUIApiHelper;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.nodeui.MetricGroup;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.GetMasterHeartbeatDelaysResponse;
import org.yb.client.ListMastersResponse;
import org.yb.client.YBClient;
import org.yb.util.ServerInfo;
import play.libs.Json;

@Singleton
@Slf4j
public class AutomatedMasterFailover {
  private final PlatformScheduler platformScheduler;
  private final RuntimeConfGetter confGetter;
  private final YBClientService ybClientService;
  private final ApiHelper apiHelper;

  private static final String YB_AUTOMATED_MASTER_FAILOVER_SCHEDULED_RUN_INTERVAL =
      "yb.automated_master_failover.run_interval";

  private static final String URL_SUFFIX = "/metrics?metrics=follower_lag_ms";

  @Inject
  public AutomatedMasterFailover(
      PlatformScheduler platformScheduler,
      RuntimeConfGetter confGetter,
      YBClientService ybClientService,
      NodeUIApiHelper apiHelper) {
    this.platformScheduler = platformScheduler;
    this.confGetter = confGetter;
    this.ybClientService = ybClientService;
    this.apiHelper = apiHelper;
  }

  public void start() {
    platformScheduler.schedule(
        getClass().getSimpleName(),
        Duration.ZERO,
        this.scheduleInterval(),
        this::detectFailedMasters);
  }

  private Duration scheduleInterval() {
    return confGetter
        .getStaticConf()
        .getDuration(YB_AUTOMATED_MASTER_FAILOVER_SCHEDULED_RUN_INTERVAL);
  }

  void detectFailedMasters() {
    if (HighAvailabilityConfig.isFollower()) {
      log.debug("Skipping Automated Master Failover Check as this is a HA standby");
      return;
    }
    log.info("Running Automated Master Failover Check");
    List<Customer> customers = Customer.getAll();
    for (Customer customer : customers) {
      boolean cloudEnabled = confGetter.getConfForScope(customer, CustomerConfKeys.cloudEnabled);
      Set<Universe> universes = customer.getUniverses();
      for (Universe universe : universes) {
        boolean isAutomatedMasterFailoverEnabled =
            confGetter.getConfForScope(
                universe, UniverseConfKeys.enableAutomatedMasterFailoverForUniverse);

        if (!isAutomatedMasterFailoverEnabled || universe.getUniverseDetails().universePaused) {
          log.debug(
              "Skipping universe {} for automated master failover check becasue {}",
              universe.getUniverseUUID(),
              !isAutomatedMasterFailoverEnabled
                  ? "automated master failover is disabled"
                  : "universe is paused");
          continue;
        }
        // Before performing any advanced checks, we will make sure that the YBA view of masters is
        // the same as the of the db. If not, we want to be on the conservative side and not perform
        // automated master failover.
        try (YBClient ybClient =
            ybClientService.getClient(
                universe.getMasterAddresses(), universe.getCertificateNodetoNode())) {
          List<String> errors =
              CheckClusterConsistency.checkCurrentServers(
                  ybClient, universe, null, false, cloudEnabled);
          if (!errors.isEmpty()) {
            log.error(
                "Skipping automated master failover for universe {} as the master view of YBA is"
                    + " not consistent with the db. Errors: {}",
                universe.getUniverseUUID(),
                errors);
            continue;
          }
          List<String> failedMasters = getFailedMastersForUniverse(universe, ybClient);
          log.info("Failed masters for universe {}: {}", universe.getUniverseUUID(), failedMasters);
        } catch (Exception e) {
          log.error(
              "Error in determining failed masters for universe {}", universe.getUniverseUUID(), e);
          continue;
        }
      }
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
   * @return list of master uuids that are identified as failed
   */
  public List<String> getFailedMastersForUniverse(Universe universe, YBClient ybClient)
      throws Exception {
    Long maxAcceptableFollowerLagMs =
        confGetter
            .getConfForScope(universe, UniverseConfKeys.automatedMasterFailoverMaxMasterFollowerLag)
            .toMillis();
    Long maxMasterHeartbeatDelayMs =
        confGetter
            .getConfForScope(
                universe, UniverseConfKeys.automatedMasterFailoverMaxMasterHeartbeatDelay)
            .toMillis();
    List<String> failedMasters = new ArrayList<>();
    Map<String, Long> masterHeartbeatDelaysMap =
        getMasterHeartbeatDelaysForUniverse(universe, ybClient);
    ListMastersResponse listMastersResponse = ybClient.listMasters();
    for (ServerInfo masterInfo : listMastersResponse.getMasters()) {
      // While performing automated master failover, we assume that the master leader is always
      // healthy.
      // Even if master leader is not healthy, we do not want to perform automated master failover
      // because in that case, it means that something is seriously wrong with the universe.
      if (masterInfo.isLeader()) {
        continue;
      }
      String masterUuid = masterInfo.getUuid();
      String ipAddress = masterInfo.getHost();
      NodeDetails node = universe.getNodeByPrivateIP(ipAddress);
      int port = node.masterHttpPort;
      if (!masterHeartbeatDelaysMap.containsKey(masterUuid)) {
        // In case the master heartbeat map does not contain a master, this is a discrepancy as the
        // master list was also fetched from the db. So we want to be conservative and not take any
        // action.
        throw new RuntimeException(
            "Cannot find heartbeat delay for master with uuid "
                + masterUuid
                + " in universe "
                + universe.getUniverseUUID());
      }
      if (masterHeartbeatDelaysMap.get(masterUuid) > maxMasterHeartbeatDelayMs) {
        log.error(
            "Marking master {} as failed in universe {} as hearbeat delay exceeded set threshold"
                + " of {} ms",
            masterUuid,
            universe.getUniverseUUID(),
            maxMasterHeartbeatDelayMs);
        failedMasters.add(masterUuid);
        continue;
      }
      // If the master is heartbeating correctly, we want to ensure that the follower lag is not
      // too high.
      Map<String, Long> tabletFollowerLagMap = this.getFollowerLagMs(ipAddress, port);
      if (!CheckFollowerLag.followerLagWithinThreshold(
          tabletFollowerLagMap, maxAcceptableFollowerLagMs)) {
        log.error(
            "Marking master {} as failed in universe {} as follower lag exceeded set threshold of"
                + " {} ms",
            masterUuid,
            universe.getUniverseUUID(),
            maxAcceptableFollowerLagMs);
        failedMasters.add(masterInfo.getUuid().toString());
      }
    }
    int replicationFactor =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor;
    if (failedMasters.size() > replicationFactor / 2) {
      log.error(
          "Majority of masters failed. Not performing automated master failover",
          universe.getUniverseUUID());
      failedMasters.clear();
    }
    return failedMasters;
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
  public Map<String, Long> getFollowerLagMs(String ip, int port) {
    String endpoint = String.format("http://%s:%s%s", ip, port, URL_SUFFIX);
    Map<String, Long> tabletFollowerLagMap = new HashMap<>();
    log.info("Getting follower lag for endpoint {} {}", endpoint, apiHelper);
    try {
      JsonNode currentNodeMetricsJson = apiHelper.getRequest(endpoint);
      JsonNode errors = currentNodeMetricsJson.get("error");
      if (errors != null) {
        log.warn("Url request: {} failed. Error: {}", endpoint, errors);
      } else {
        ObjectMapper objectMapper = Json.mapper();
        List<MetricGroup> metricGroups =
            objectMapper.readValue(
                currentNodeMetricsJson.toString(), new TypeReference<List<MetricGroup>>() {});
        tabletFollowerLagMap = MetricGroup.getTabletFollowerLagMap(metricGroups);
      }
    } catch (Exception e) {
      log.error("Error getting follower lag for endpoint {}", endpoint, e);
    }
    return tabletFollowerLagMap;
  }

  /**
   * Helper method to get the master heartbeat delays for a universe via a rpc to the master leader.
   *
   * @param universe The universe object
   * @param ybClient The yb client object for the universe, to make rpc calls
   * @return map of master uuid to heartbeat delay, excluding the master leader leader
   */
  private Map<String, Long> getMasterHeartbeatDelaysForUniverse(
      Universe universe, YBClient ybClient) throws Exception {
    Map<String, Long> masterHeartbeatDelays = new HashMap<>();
    GetMasterHeartbeatDelaysResponse response = ybClient.getMasterHeartbeatDelays();
    if (response.hasError()) {
      log.error(
          "Error while getting heartbeat delays for universe {}: {}",
          universe.getUniverseUUID(),
          response.errorMessage());
    } else {
      masterHeartbeatDelays = response.getMasterHeartbeatDelays();
    }
    return masterHeartbeatDelays;
  }
}
