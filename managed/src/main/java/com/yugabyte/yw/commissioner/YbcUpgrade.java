// Copyright (c) YugaByte, Inc

package com.yugabyte.yw.commissioner;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArraySet;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YbcClient;
import org.yb.ybc.ControllerStatus;
import org.yb.ybc.ShutdownRequest;
import org.yb.ybc.ShutdownResponse;
import org.yb.ybc.UpgradeRequest;
import org.yb.ybc.UpgradeRequest.Builder;
import org.yb.ybc.UpgradeResponse;
import org.yb.ybc.UpgradeResultRequest;
import org.yb.ybc.UpgradeResultResponse;

@Singleton
@Slf4j
public class YbcUpgrade {

  private final PlatformScheduler platformScheduler;
  private final RuntimeConfGetter confGetter;
  private final YbcClientService ybcClientService;
  private final YbcManager ybcManager;
  private final NodeUniverseManager nodeUniverseManager;
  private final Config config;

  public static final String YBC_UPGRADE_INTERVAL = "ybc.upgrade.scheduler_interval";
  public static final String YBC_UNIVERSE_UPGRADE_BATCH_SIZE_PATH =
      "ybc.upgrade.universe_batch_size";
  public static final String YBC_ALLOW_SCHEDULED_UPGRADE_PATH =
      "ybc.upgrade.allow_scheduled_upgrade";
  public static final String YBC_NODE_UPGRADE_BATCH_SIZE_PATH = "ybc.upgrade.node_batch_size";

  private final int YBC_UNIVERSE_UPGRADE_BATCH_SIZE;
  private final int YBC_NODE_UPGRADE_BATCH_SIZE;
  public final int MAX_YBC_UPGRADE_POLL_RESULT_TRIES = 10;
  public final long YBC_UPGRADE_POLL_RESULT_SLEEP_MS = 10000;
  private final long YBC_REMOTE_TIMEOUT_SEC = 60;
  private final String PACKAGE_PERMISSIONS = "755";

  private final CopyOnWriteArraySet<UUID> ybcUpgradeUniverseSet = new CopyOnWriteArraySet<>();
  private final CopyOnWriteArraySet<UUID> ybcUpgradeUniverseSetOnK8s = new CopyOnWriteArraySet<>();
  private final Set<UUID> failedYBCUpgradeUniverseSet = new HashSet<>();
  private final Set<UUID> failedYBCUpgradeUniverseSetOnK8s = new HashSet<>();
  private final Map<UUID, Set<String>> unreachableNodes = new ConcurrentHashMap<>();

  @Inject
  public YbcUpgrade(
      Config config,
      PlatformScheduler platformScheduler,
      RuntimeConfGetter confGetter,
      YbcClientService ybcClientService,
      YbcManager ybcManager,
      NodeUniverseManager nodeUniverseManager) {
    this.config = config;
    this.platformScheduler = platformScheduler;
    this.confGetter = confGetter;
    this.ybcClientService = ybcClientService;
    this.ybcManager = ybcManager;
    this.nodeUniverseManager = nodeUniverseManager;
    this.YBC_UNIVERSE_UPGRADE_BATCH_SIZE = getYBCUniverseBatchSize();
    this.YBC_NODE_UPGRADE_BATCH_SIZE = getYBCNodeBatchSize();
  }

  public void start() {
    if (config.getBoolean("yb.cloud.enabled")) {
      return;
    }
    Duration duration = this.upgradeInterval();
    platformScheduler.schedule("Ybc Upgrade", Duration.ZERO, duration, this::scheduleRunner);
  }

  private Duration upgradeInterval() {
    return confGetter.getGlobalConf(GlobalConfKeys.ybcUpgradeInterval);
  }

  private int getYBCUniverseBatchSize() {
    return confGetter.getGlobalConf(GlobalConfKeys.ybcUniverseBatchSize);
  }

  private int getYBCNodeBatchSize() {
    return confGetter.getGlobalConf(GlobalConfKeys.ybcNodeBatchSize);
  }

  public synchronized void setYBCUpgradeProcess(UUID universeUUID) {
    ybcUpgradeUniverseSet.add(universeUUID);
    unreachableNodes.put(universeUUID, new HashSet<>());
  }

  public synchronized void setYBCUpgradeProcessOnK8s(UUID universeUUID) {
    ybcUpgradeUniverseSetOnK8s.add(universeUUID);
  }

  public synchronized void removeYBCUpgradeProcess(UUID universeUUID) {
    ybcUpgradeUniverseSet.remove(universeUUID);
    unreachableNodes.remove(universeUUID);
  }

  public synchronized void removeYBCUpgradeProcessOnK8s(UUID universeUUID) {
    ybcUpgradeUniverseSetOnK8s.remove(universeUUID);
  }

  public synchronized boolean checkYBCUpgradeProcessExists(UUID universeUUID) {
    return ybcUpgradeUniverseSet.contains(universeUUID);
  }

  public synchronized boolean checkYBCUpgradeProcessExistsOnK8s(UUID universeUUID) {
    return ybcUpgradeUniverseSetOnK8s.contains(universeUUID);
  }

  void scheduleRunner() {
    log.info("Running YBC Upgrade schedule");
    try {
      List<UUID> targetUniverseList = new ArrayList<UUID>();
      List<UUID> k8sUniverseList = new ArrayList<UUID>();
      int nodeCount = 0;
      String ybcVersion = ybcManager.getStableYbcVersion();
      for (Customer customer : Customer.getAll()) {
        for (Universe universe : Universe.getAllWithoutResources(customer)) {
          if (universe
              .getUniverseDetails()
              .getPrimaryCluster()
              .userIntent
              .providerType
              .equals(Common.CloudType.kubernetes)) {
            if (!canUpgradeYBCOnK8s(universe, ybcVersion)) {
              continue;
            }
            k8sUniverseList.add(universe.getUniverseUUID());
          } else {
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
              targetUniverseList.add(universe.getUniverseUUID());
            }
          }
        }
      }

      if (targetUniverseList.size() == 0) {
        failedYBCUpgradeUniverseSet.clear();
      }

      if (k8sUniverseList.isEmpty()) {
        failedYBCUpgradeUniverseSetOnK8s.clear();
      }

      targetUniverseList.forEach(
          (universeUUID) -> {
            Universe universe = Universe.getOrBadRequest(universeUUID);
            try {
              this.upgradeYBC(universeUUID, ybcVersion, false);
            } catch (Exception e) {
              log.error(
                  "YBC Upgrade request failed for universe {} with error: {}", universeUUID, e);
              failedYBCUpgradeUniverseSet.add(universeUUID);
              removeYBCUpgradeProcess(universeUUID);
            }
          });

      k8sUniverseList.forEach(
          (universeUUID) -> {
            try {
              this.upgradeYbcOnK8s(universeUUID, ybcVersion, false);
            } catch (Exception ex) {
              log.error(
                  "YBC Upgrade request failed for universe {} with error: {}", universeUUID, ex);
            }
          });

      int numRetries = 0;
      while (ybcUpgradeUniverseSet.size() > 0 && numRetries < MAX_YBC_UPGRADE_POLL_RESULT_TRIES) {
        numRetries++;
        boolean found = false;
        Iterator<UUID> iter = targetUniverseList.iterator();
        while (iter.hasNext()) {
          UUID universeUUID = iter.next();
          if (checkYBCUpgradeProcessExists(universeUUID)) {
            found = true;
            Optional<Universe> optional = Universe.maybeGet(universeUUID);
            if (!optional.isPresent()) {
              iter.remove();
            } else {
              pollUpgradeTaskResult(universeUUID, ybcVersion, false);
            }
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

      if (ybcUpgradeUniverseSetOnK8s.size() > 0) {
        Iterator<UUID> iter = k8sUniverseList.iterator();
        while (iter.hasNext()) {
          UUID universeUUID = iter.next();
          Optional<Universe> optional = Universe.maybeGet(universeUUID);
          if (!optional.isPresent()) {
            iter.remove();
            continue;
          } else if (checkYBCUpgradeProcessExistsOnK8s(universeUUID)
              && !failedYBCUpgradeUniverseSetOnK8s.contains(universeUUID)) {
            waitForYbcServersOnK8s(universeUUID, ybcVersion);
          }

          if (checkYBCUpgradeProcessExistsOnK8s(universeUUID)) {
            removeYBCUpgradeProcessOnK8s(universeUUID);
            failedYBCUpgradeUniverseSetOnK8s.add(universeUUID);
          }
        }
      }

    } catch (Exception e) {
      log.error("Error occurred while running YBC upgrade scheduler", e);
    }
  }

  public boolean canUpgradeYBC(Universe universe, String ybcVersion) {
    return universe.isYbcEnabled()
        && !universe.getUniverseDetails().universePaused
        && !universe.getUniverseDetails().updateInProgress
        && !universe.getUniverseDetails().getYbcSoftwareVersion().equals(ybcVersion)
        && !failedYBCUpgradeUniverseSet.contains(universe.getUniverseUUID());
  }

  public boolean canUpgradeYBCOnK8s(Universe universe, String ybcVersion) {
    return universe.isYbcEnabled()
        && !universe.getUniverseDetails().universePaused
        && !universe.getUniverseDetails().updateInProgress
        && !universe.getUniverseDetails().getYbcSoftwareVersion().equals(ybcVersion)
        && !failedYBCUpgradeUniverseSetOnK8s.contains(universe.getUniverseUUID());
  }

  public synchronized void upgradeYBC(UUID universeUUID, String ybcVersion, boolean force)
      throws Exception {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    if (!force
        && !confGetter.getConfForScope(universe, UniverseConfKeys.ybcAllowScheduledUpgrade)) {
      log.debug(
          "Skipping scheduled ybc upgrade on universe {} as it was disabled.",
          universe.getUniverseUUID());
      return;
    }
    if (checkYBCUpgradeProcessExists(universeUUID)) {
      log.warn("YBC upgrade process already exists for universe {}", universeUUID);
      return;
    } else {
      setYBCUpgradeProcess(universeUUID);
    }
    universe
        .getNodes()
        .forEach(
            (node) -> {
              try {
                upgradeYbcOnNode(universe, node, ybcVersion);
              } catch (Exception e) {
                log.error(
                    "Ybc upgrade request failed on node: {} universe: {}, with error: ",
                    node.nodeName,
                    universe.getUniverseUUID(),
                    e);
              }
            });
  }

  public synchronized void upgradeYbcOnK8s(UUID universeUUID, String ybcVersion, boolean force)
      throws Exception {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    if (!force
        && !confGetter.getConfForScope(universe, UniverseConfKeys.ybcAllowScheduledUpgrade)) {
      log.debug("Skipping scheduled ybc upgrade on universe {} as it was disabled.", universeUUID);
      return;
    }

    if (checkYBCUpgradeProcessExistsOnK8s(universeUUID)) {
      log.warn("YBC upgrade process already exists for universe {}", universeUUID);
      return;
    } else {
      setYBCUpgradeProcessOnK8s(universeUUID);
    }

    try {
      Set<NodeDetails> primaryTservers =
          new HashSet<NodeDetails>(universe.getTServersInPrimaryCluster());
      List<String> commandArgs =
          Arrays.asList("/bin/bash", "-c", "/home/yugabyte/tools/k8s_ybc_parent.py stop");
      PlacementInfo primaryPI = universe.getUniverseDetails().getPrimaryCluster().placementInfo;
      Map<String, Map<String, String>> k8sConfigMap =
          KubernetesUtil.getKubernetesConfigPerPodName(primaryPI, primaryTservers);
      for (NodeDetails primaryTserver : primaryTservers) {
        Map<String, String> config = k8sConfigMap.get(primaryTserver.nodeName);
        ybcManager.copyYbcPackagesOnK8s(config, universe, primaryTserver, ybcVersion);
        ybcManager.performActionOnYbcK8sNode(config, primaryTserver, commandArgs);
      }

      if (universe.getUniverseDetails().getReadOnlyClusters().size() != 0) {
        Cluster readOnlyCluster = universe.getUniverseDetails().getReadOnlyClusters().get(0);
        Set<NodeDetails> readOnlyTservers =
            new HashSet<NodeDetails>(universe.getNodesInCluster(readOnlyCluster.uuid));
        PlacementInfo readOnlyPI = readOnlyCluster.placementInfo;
        k8sConfigMap = KubernetesUtil.getKubernetesConfigPerPodName(readOnlyPI, readOnlyTservers);
        for (NodeDetails readOnlyTserver : readOnlyTservers) {
          Map<String, String> config = k8sConfigMap.get(readOnlyTserver.nodeName);
          ybcManager.copyYbcPackagesOnK8s(config, universe, readOnlyTserver, ybcVersion);
          ybcManager.performActionOnYbcK8sNode(config, readOnlyTserver, commandArgs);
        }
      }
      failedYBCUpgradeUniverseSetOnK8s.remove(universeUUID);
    } catch (Exception ex) {
      log.error("YBC Upgrade request failed for universe {} with error: {}", universeUUID, ex);
      failedYBCUpgradeUniverseSetOnK8s.add(universeUUID);
    }
  }

  public synchronized boolean waitForYbcServersOnK8s(UUID universeUUID, String ybcVersion) {
    try {
      Universe universe = Universe.getOrBadRequest(universeUUID);
      ybcManager.waitForYbc(universe, new HashSet<NodeDetails>(universe.getTServers()));
      updateUniverseYBCVersion(universeUUID, ybcVersion);
      removeYBCUpgradeProcessOnK8s(universeUUID);
      failedYBCUpgradeUniverseSet.remove(universeUUID);
    } catch (Exception ex) {
      log.error("Ybc server start failed on universe: {}, with error: {}", universeUUID, ex);
      return false;
    }
    return true;
  }

  private void upgradeYbcOnNode(Universe universe, NodeDetails node, String ybcVersion) {
    if (!universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd) {
      checkCronStatus(universe, node);
    }
    log.info("Placing ybc package on db node: {}", node.cloudInfo.private_ip);
    placeYbcPackageOnDBNode(universe, node, ybcVersion);
    int ybcPort = universe.getUniverseDetails().communicationPorts.ybControllerrRpcPort;
    String certFile = universe.getCertificateNodetoNode();
    String nodeIp = node.cloudInfo.private_ip;
    Builder builder =
        UpgradeRequest.newBuilder()
            .setYbcVersion(ybcVersion)
            .setHomeDir(Util.getNodeHomeDir(universe.getUniverseUUID(), node));
    String location = ybcManager.getYbcPackageTmpLocation(universe, node, ybcVersion);
    builder.setLocation(location);
    UpgradeRequest upgradeRequest = builder.build();
    YbcClient client = null;
    UpgradeResponse resp = null;
    try {
      client = ybcClientService.getNewClient(nodeIp, ybcPort, certFile);
      resp = client.Upgrade(upgradeRequest);
      if (resp == null) {
        unreachableNodes.getOrDefault(universe.getUniverseUUID(), new HashSet<>()).add(nodeIp);
        log.warn("Skipping node {} for YBC upgrade as it is unreachable", nodeIp);
        return;
      }
      // yb-controller throws this error only when we tries to upgrade it to same version.
      if (resp.getStatus().getCode().equals(ControllerStatus.HTTP_BAD_REQUEST)) {
        log.warn(
            "YBC {} version is already present on the node {} of universe {}",
            ybcVersion,
            nodeIp,
            universe.getUniverseUUID());
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

  public synchronized boolean pollUpgradeTaskResult(
      UUID universeUUID, String ybcVersion, boolean verbose) {
    try {
      Universe universe = Universe.getOrBadRequest(universeUUID);
      UpgradeResultRequest request =
          UpgradeResultRequest.newBuilder().setYbcVersion(ybcVersion).build();
      int ybcPort = universe.getUniverseDetails().communicationPorts.ybControllerrRpcPort;
      String certFile = universe.getCertificateNodetoNode();
      boolean success = true;
      for (NodeDetails node : universe.getNodes()) {
        String nodeIp = node.cloudInfo.private_ip;
        YbcClient client = null;
        try {
          client = ybcClientService.getNewClient(nodeIp, ybcPort, certFile);
          UpgradeResultResponse resp = client.UpgradeResult(request);
          if (resp == null || resp.getStatus().equals(ControllerStatus.IN_PROGRESS)) {
            success = false;
          } else if (resp.getStatus().equals(ControllerStatus.COMMAND_FAILED)) {
            if (confGetter.getGlobalConf(GlobalConfKeys.forceYbcShutdownDuringUpgrade)) {
              log.info("Shutting down ybc on node: {}", node.cloudInfo.private_ip);
              shutdownYbcOnNode(universe, node);
              success = false;
            } else {
              log.error(
                  "YBC upgrade failed on node: {} with error: {}",
                  node.cloudInfo.private_ip,
                  resp.toString());
              throw new RuntimeException(
                  "YBC upgrade failed on node: "
                      + node.cloudInfo.private_ip
                      + " with error: "
                      + resp.getStatus());
            }
          } else if (!resp.getStatus().equals(ControllerStatus.COMPLETE)) {
            throw new RuntimeException(
                "YBC Upgrade is not completed on node "
                    + nodeIp
                    + " universe "
                    + universeUUID
                    + ".");
          } else {
            log.info(
                "Found ybc version: {} on node: {}",
                resp.getCurrentYbcVersion(),
                node.cloudInfo.private_ip);
            // Verify that required version is upgraded.
            if (!resp.getCurrentYbcVersion().equals(ybcVersion)) {
              throw new RuntimeException(
                  "YBC Upgrade failed as expected ybc version: "
                      + ybcVersion
                      + " but found: "
                      + resp.getCurrentYbcVersion());
            }
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

  public synchronized void shutdownYbcOnNode(Universe universe, NodeDetails node) {
    int ybcPort = universe.getUniverseDetails().communicationPorts.ybControllerrRpcPort;
    String certFile = universe.getCertificateNodetoNode();
    ShutdownRequest request = ShutdownRequest.newBuilder().build();
    String nodeIp = node.cloudInfo.private_ip;
    YbcClient client = null;
    try {
      client = ybcClientService.getNewClient(nodeIp, ybcPort, certFile);
      if (client == null) {
        // Assuming ybc is already dead if YBA is unable to create yb-client.
        return;
      }
      ShutdownResponse resp = client.shutdown(request);
      if (!resp.getStatus().getCode().equals(ControllerStatus.OK)) {
        log.error("YBC shutdown failed with error: {}", resp.toString());
        throw new RuntimeException("YBC shutdown failed with error: " + resp.getStatus());
      }
    } catch (Exception e) {
      log.error("YBC shutdown failed with error: ", e);
      throw e;
    } finally {
      ybcClientService.closeClient(client);
    }
  }

  private void updateUniverseYBCVersion(UUID universeUUID, String ybcVersion) {
    UniverseUpdater updater =
        new UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            universeDetails.setYbcSoftwareVersion(ybcVersion);
            universe.setUniverseDetails(universeDetails);
          }
        };
    Universe.saveDetails(universeUUID, updater, false);
  }

  public String getUniverseYbcVersion(UUID universeUUID) {
    return Universe.getOrBadRequest(universeUUID).getUniverseDetails().getYbcSoftwareVersion();
  }

  private void placeYbcPackageOnDBNode(Universe universe, NodeDetails node, String ybcVersion) {
    String ybcServerPackage = ybcManager.getYbcServerPackageForNode(universe, node, ybcVersion);
    ShellProcessContext context =
        ShellProcessContext.builder()
            .logCmdOutput(false)
            .traceLogging(true)
            .timeoutSecs(YBC_REMOTE_TIMEOUT_SEC)
            .build();
    String targetFile = ybcManager.getYbcPackageTmpLocation(universe, node, ybcVersion);
    nodeUniverseManager
        .uploadFileToNode(
            node, universe, ybcServerPackage, targetFile, PACKAGE_PERMISSIONS, context)
        .processErrors();
  }

  private void checkCronStatus(Universe universe, NodeDetails node) {
    ShellProcessContext context =
        ShellProcessContext.builder()
            .logCmdOutput(false)
            .traceLogging(true)
            .timeoutSecs(YBC_REMOTE_TIMEOUT_SEC)
            .build();
    List<String> cmd = new ArrayList<>();
    String homeDir = nodeUniverseManager.getYbHomeDir(node, universe);
    String ybCtlLocation = homeDir + "/bin/yb-server-ctl.sh";
    String ybCtlControllerCmd = ybCtlLocation + " controller";
    String cronCheckCmd = ybCtlControllerCmd + " cron-check";
    String cronStartCmd = ybCtlControllerCmd + " start";

    cmd.addAll(Arrays.asList("crontab", "-l", "|", "grep", "-q", cronCheckCmd));
    cmd.add("||");
    cmd.add("(");
    cmd.addAll(Arrays.asList("crontab", "-l", "2>/dev/null", "||", "true;"));
    cmd.addAll(
        Arrays.asList(
            "echo",
            "-e",
            "#Ansible: Check liveness of controller\n*/1 * * * * "
                + cronCheckCmd
                + " || "
                + cronStartCmd));
    cmd.add(")");
    cmd.add("|");
    cmd.addAll(Arrays.asList("crontab", "-", ";"));
    nodeUniverseManager.runCommand(node, universe, cmd, context).processErrors();
  }
}
