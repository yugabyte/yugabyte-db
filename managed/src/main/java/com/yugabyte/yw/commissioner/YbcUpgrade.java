// Copyright (c) YugaByte, Inc

package com.yugabyte.yw.commissioner;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YbcManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArraySet;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YbcClient;
import org.yb.ybc.ControllerStatus;
import org.yb.ybc.UpgradeRequest;
import org.yb.ybc.UpgradeResponse;
import org.yb.ybc.UpgradeResultRequest;
import org.yb.ybc.UpgradeResultResponse;

@Singleton
@Slf4j
public class YbcUpgrade {

  private final PlatformScheduler platformScheduler;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private final YbcClientService ybcClientService;
  private final YbcManager ybcManager;

  public static final String YBC_UPGRADE_INTERVAL = "ybc.upgrade.scheduler_interval";
  public static final String YBC_UNIVERSE_UPGRADE_BATCH_SIZE_PATH =
      "ybc.upgrade.universe_batch_size";
  public static final String YBC_NODE_UPGRADE_BATCH_SIZE_PATH = "ybc.upgrade.node_batch_size";

  private final int YBC_UNIVERSE_UPGRADE_BATCH_SIZE;
  private final int YBC_NODE_UPGRADE_BATCH_SIZE;
  public final int MAX_YBC_UPGRADE_POLL_RESULT_TRIES = 30;
  public final long YBC_UPGRADE_POLL_RESULT_SLEEP_MS = 10000;
  private final String PLAT_YBC_PACKAGE_URL;

  private final CopyOnWriteArraySet<UUID> ybcUpgradeUniverseSet = new CopyOnWriteArraySet<>();
  private final Set<UUID> failedYBCUpgradeUniverseSet = new HashSet<>();
  private static final Map<UUID, Set<String>> unreachableNodes = new ConcurrentHashMap<>();

  @Inject
  public YbcUpgrade(
      PlatformScheduler platformScheduler,
      RuntimeConfigFactory runtimeConfigFactory,
      YbcClientService ybcClientService,
      YbcManager ybcManager) {
    this.platformScheduler = platformScheduler;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.ybcClientService = ybcClientService;
    this.ybcManager = ybcManager;
    this.YBC_UNIVERSE_UPGRADE_BATCH_SIZE = getYBCUniverseBatchSize();
    this.YBC_NODE_UPGRADE_BATCH_SIZE = getYBCNodeBatchSize();
    this.PLAT_YBC_PACKAGE_URL = "http://" + Util.getHostIP() + ":9000/api/v1/fetch_package";
  }

  public void start() {
    Duration duration = this.upgradeInterval();
    platformScheduler.schedule("Ybc Upgrade", Duration.ZERO, duration, this::scheduleRunner);
  }

  private Duration upgradeInterval() {
    return runtimeConfigFactory.globalRuntimeConf().getDuration(YBC_UPGRADE_INTERVAL);
  }

  private int getYBCUniverseBatchSize() {
    return runtimeConfigFactory.globalRuntimeConf().getInt(YBC_UNIVERSE_UPGRADE_BATCH_SIZE_PATH);
  }

  private int getYBCNodeBatchSize() {
    return runtimeConfigFactory.globalRuntimeConf().getInt(YBC_NODE_UPGRADE_BATCH_SIZE_PATH);
  }

  public synchronized void setYBCUpgradeProcess(UUID universeUUID) {
    ybcUpgradeUniverseSet.add(universeUUID);
    unreachableNodes.put(universeUUID, new HashSet<>());
  }

  public synchronized void removeYBCUpgradeProcess(UUID universeUUID) {
    ybcUpgradeUniverseSet.remove(universeUUID);
  }

  public synchronized boolean checkYBCUpgradeProcessExists(UUID universeUUID) {
    return ybcUpgradeUniverseSet.contains(universeUUID);
  }

  void scheduleRunner() {
    log.info("Running YBC Upgrade schedule");
    try {
      List<UUID> targetUniverseList = new ArrayList<UUID>();
      int nodeCount = 0;
      String ybcVersion = ybcManager.getStableYbcVersion();
      for (Customer customer : Customer.getAll()) {
        for (Universe universe : Universe.getAllWithoutResources(customer)) {
          if (!canUpgradeYBC(universe, ybcVersion)) {
            continue;
          }
          int numNodesInUniverse = universe.getNodes().size();
          if (targetUniverseList.size() > YBC_UNIVERSE_UPGRADE_BATCH_SIZE) {
            break;
          } else if (targetUniverseList.size() > 0
              && nodeCount + numNodesInUniverse > YBC_NODE_UPGRADE_BATCH_SIZE) {
            break;
          } else {
            nodeCount += numNodesInUniverse;
            targetUniverseList.add(universe.universeUUID);
          }
        }
      }

      if (targetUniverseList.size() == 0) {
        failedYBCUpgradeUniverseSet.clear();
      }

      targetUniverseList.forEach(
          (universeUUID) -> {
            try {
              this.upgradeYBC(universeUUID, ybcVersion);
            } catch (Exception e) {
              log.error(
                  "YBC Upgrade request failed for universe {} with error: {}", universeUUID, e);
              failedYBCUpgradeUniverseSet.add(universeUUID);
              removeYBCUpgradeProcess(universeUUID);
            }
          });

      int numRetries = 0;
      while (ybcUpgradeUniverseSet.size() > 0 && numRetries < MAX_YBC_UPGRADE_POLL_RESULT_TRIES) {
        numRetries++;
        boolean found = false;
        for (UUID universeUUID : targetUniverseList) {
          if (checkYBCUpgradeProcessExists(universeUUID)) {
            found = true;
            pollUpgradeTaskResult(universeUUID, ybcVersion, false);
          }
        }
        if (!found) {
          break;
        }
        Thread.sleep(YBC_UPGRADE_POLL_RESULT_SLEEP_MS);
      }

      targetUniverseList.forEach(
          (universeUUID) -> {
            if (checkYBCUpgradeProcessExists(universeUUID)) {
              pollUpgradeTaskResult(universeUUID, ybcVersion, true);
              removeYBCUpgradeProcess(universeUUID);
              failedYBCUpgradeUniverseSet.add(universeUUID);
            }
          });

    } catch (Exception e) {
      log.error("Error occurred while running YBC upgrade scheduler", e);
    }
  }

  public boolean canUpgradeYBC(Universe universe, String ybcVersion) {
    return universe.isYbcEnabled()
        && !universe.getUniverseDetails().universePaused
        && !universe.getUniverseDetails().updateInProgress
        && !universe.getUniverseDetails().ybcSoftwareVersion.equals(ybcVersion)
        && !failedYBCUpgradeUniverseSet.contains(universe.universeUUID);
  }

  public synchronized void upgradeYBC(UUID universeUUID, String ybcVersion) throws Exception {
    if (checkYBCUpgradeProcessExists(universeUUID)) {
      log.warn("YBC upgrade process already exists for universe {}", universeUUID);
      return;
    } else {
      setYBCUpgradeProcess(universeUUID);
    }
    Universe universe = Universe.getOrBadRequest(universeUUID);
    Integer ybcPort = universe.getUniverseDetails().communicationPorts.ybControllerrRpcPort;
    String certFile = universe.getCertificateNodetoNode();
    for (NodeDetails node : universe.getNodes()) {
      String nodeIp = node.cloudInfo.private_ip;
      UpgradeRequest upgradeRequest =
          UpgradeRequest.newBuilder()
              .setLocation(PLAT_YBC_PACKAGE_URL)
              .setYbcVersion(ybcVersion)
              .setHomeDir(Util.getNodeHomeDir(universeUUID, node.nodeName))
              .build();
      YbcClient client = null;
      UpgradeResponse resp = null;
      try {
        client = ybcClientService.getNewClient(nodeIp, ybcPort, certFile);
        resp = client.Upgrade(upgradeRequest);
        if (resp == null) {
          unreachableNodes.getOrDefault(universe.universeUUID, new HashSet<>()).add(nodeIp);
          log.warn("Skipping node {} for YBC upgrade as it is unreachable", nodeIp);
          continue;
        }
        // yb-controller throws this error only when we tries to upgrade it to same version.
        if (resp.getStatus().getCode().equals(ControllerStatus.HTTP_BAD_REQUEST)) {
          log.warn(
              "YBC {} version is already present on the node {} of universe {}",
              ybcVersion,
              nodeIp,
              universeUUID);
        } else if (!resp.getStatus().getCode().equals(ControllerStatus.OK)) {
          throw new RuntimeException(
              "Error occurred while sending  ybc update request: "
                  + resp.getStatus().getCode().toString());
        }
      } catch (Exception e) {
        throw e;
      } finally {
        ybcClientService.closeClient(client);
      }
    }
  }

  public synchronized boolean pollUpgradeTaskResult(
      UUID universeUUID, String ybcVersion, boolean verbose) {
    try {
      Universe universe = Universe.getOrBadRequest(universeUUID);
      UpgradeResultRequest request =
          UpgradeResultRequest.newBuilder().setYbcVersion(ybcVersion).build();
      Integer ybcPort = universe.getUniverseDetails().communicationPorts.ybControllerrRpcPort;
      String certFile = universe.getCertificateNodetoNode();
      boolean success = true;
      for (NodeDetails node : universe.getNodes()) {
        String nodeIp = node.cloudInfo.private_ip;
        if (unreachableNodes.getOrDefault(universeUUID, new HashSet<>()).contains(nodeIp)) {
          continue;
        }
        YbcClient client = null;
        try {
          client = ybcClientService.getNewClient(nodeIp, ybcPort, certFile);
          UpgradeResultResponse resp = client.UpgradeResult(request);
          if (resp == null) {
            throw new RuntimeException(
                "Could not get upgrade task result on node " + nodeIp + " as it is not reachable");
          }
          if (resp.getStatus().equals(ControllerStatus.IN_PROGRESS)) {
            success = false;
            continue;
          } else if (resp.getStatus().equals(ControllerStatus.COMMAND_FAILED)) {
            removeYBCUpgradeProcess(universeUUID);
            failedYBCUpgradeUniverseSet.add(universeUUID);
            throw new RuntimeException(
                "YBC upgrade task failed on node " + nodeIp + "  universe " + universeUUID);
          } else if (!resp.getStatus().equals(ControllerStatus.COMPLETE)) {
            throw new RuntimeException(
                "YBC Upgrade is not completed on node "
                    + nodeIp
                    + " universe "
                    + universeUUID
                    + ".");
          }
        } catch (Exception e) {
          success = false;
          if (verbose) {
            log.error("Upgrade ybc task failed due to error: {}", e);
          } else {
            break;
          }
        } finally {
          ybcClientService.closeClient(client);
        }
      }
      if (success) {
        removeYBCUpgradeProcess(universeUUID);
      }
      if (!success
          || (unreachableNodes.getOrDefault(universeUUID, new HashSet<>()) != null
              && unreachableNodes.getOrDefault(universeUUID, new HashSet<>()).size() > 0)) {
        return false;
      }
      // we will update the ybc version in universe detail only when the ybc is upgraded on all DB
      // nodes.
      updateUniverseYBCVersion(universeUUID, ybcVersion);
      log.info(
          "YBC is upgraded successfully on universe {} to version {}", universeUUID, ybcVersion);
    } catch (Exception e) {
      log.error("YBC upgrade on universe {} errored out with: {}", universeUUID, e);
      return false;
    }
    return true;
  }

  private void updateUniverseYBCVersion(UUID universeUUID, String ybcVersion) {
    UniverseUpdater updater =
        new UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            universeDetails.ybcSoftwareVersion = ybcVersion;
            universe.setUniverseDetails(universeDetails);
          }
        };
    Universe.saveDetails(universeUUID, updater);
  }

  public String getUniverseYbcVersion(UUID universeUUID) {
    return Universe.getOrBadRequest(universeUUID).getUniverseDetails().ybcSoftwareVersion;
  }
}
