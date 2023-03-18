// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.Lists;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.NodeAgentClient.NodeAgentUpgradeParam;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.common.NodeAgentManager.InstallerFiles;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.controllers.handlers.NodeAgentHandler;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.nodeagent.Server.PingResponse;
import com.yugabyte.yw.nodeagent.Server.ServerInfo;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.Semaphore;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Builder;
import lombok.Getter;
import lombok.NonNull;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
@Singleton
public class NodeAgentPoller {
  public static final String RETENTION_DURATION_PROPERTY = "yb.node_agent.retention_duration";
  private static final Duration POLLER_INITIAL_DELAY = Duration.ofMinutes(1);
  private static final String LIVE_POLLER_POOL_NAME = "node_agent.live_node_poller";
  private static final String DEAD_POLLER_POOL_NAME = "node_agent.dead_node_poller";
  private static final int MAX_FAILED_CONN_COUNT = 100;

  private final ConfigHelper configHelper;
  private final RuntimeConfGetter confGetter;
  private final PlatformExecutorFactory platformExecutorFactory;
  private final PlatformScheduler platformScheduler;
  private final NodeAgentClient nodeAgentClient;
  private final NodeAgentManager nodeAgentManager;

  private final Map<UUID, PollerTask> pollerTasks = new ConcurrentHashMap<>();

  // Poller with more threads allowing less queuing.
  private ExecutorService livePollerExecutor;
  // Poller with less threads allowing more queuing.
  private ExecutorService deadPollerExecutor;
  // Tracks the current number of active upgrades.
  private Semaphore activeUpgrades;

  @Inject
  public NodeAgentPoller(
      ConfigHelper configHelper,
      RuntimeConfGetter confGetter,
      PlatformExecutorFactory platformExecutorFactory,
      PlatformScheduler platformScheduler,
      NodeAgentManager nodeAgentManager,
      NodeAgentClient nodeAgentClient) {
    this.configHelper = configHelper;
    this.confGetter = confGetter;
    this.platformExecutorFactory = platformExecutorFactory;
    this.platformScheduler = platformScheduler;
    this.nodeAgentManager = nodeAgentManager;
    this.nodeAgentClient = nodeAgentClient;
  }

  @Builder
  @Getter
  static class PollerTaskParam {
    @NonNull private UUID nodeAgentUuid;
    @NonNull private String softwareVersion;
    @NonNull private Duration lifetime;
  }

  // PollerTask is created for each node agent.
  @VisibleForTesting
  class PollerTask implements Runnable {
    private final PollerTaskParam param;
    private final AtomicReference<Future<?>> future = new AtomicReference<>();
    private final AtomicInteger lastFailedCount = new AtomicInteger();
    private final AtomicBoolean isUpgradeTokenAcquired = new AtomicBoolean();

    PollerTask(PollerTaskParam param) {
      this.param = param;
    }

    PollerTaskParam getParam() {
      return param;
    }

    private void schedule(ExecutorService pollerExecutor) {
      try {
        future.set(pollerExecutor.submit(this));
      } catch (RejectedExecutionException e) {
        log.error("Failed to schedule poller task for {}", param.getNodeAgentUuid());
      }
    }

    private boolean isSchedulable() {
      return future.get() == null;
    }

    private boolean isNodeAgentAlive() {
      return lastFailedCount.get() < MAX_FAILED_CONN_COUNT;
    }

    private boolean acquireUpgradeToken() {
      if (isUpgradeTokenAcquired.get()) {
        log.info("Node agent {} is already being upgraded", param.getNodeAgentUuid());
        return true;
      }
      if (activeUpgrades.tryAcquire()) {
        log.info("Upgrading node agent {}", param.getNodeAgentUuid());
        isUpgradeTokenAcquired.set(true);
        return true;
      }
      log.info(
          "Postponing node agent {} upgrade as max parallel upgrades has reached",
          param.getNodeAgentUuid());
      isUpgradeTokenAcquired.set(false);
      return false;
    }

    private void releaseUpgradeToken() {
      if (isUpgradeTokenAcquired.get()) {
        activeUpgrades.release();
        isUpgradeTokenAcquired.set(false);
      }
    }

    @Override
    public void run() {
      try {
        NodeAgent.maybeGet(param.getNodeAgentUuid()).ifPresent(n -> poll(n));
      } catch (Exception e) {
        log.error("Error in polling for node {} - {}", param.getNodeAgentUuid(), e.getMessage(), e);
      } finally {
        future.set(null);
      }
    }

    private void poll(NodeAgent nodeAgent) {
      try {
        nodeAgentClient.waitForServerReady(nodeAgent, Duration.ofSeconds(2));
      } catch (RuntimeException e) {
        // Release on connection error if it was already acquired.
        releaseUpgradeToken();
        int count =
            lastFailedCount.updateAndGet(val -> val >= MAX_FAILED_CONN_COUNT ? val : val + 1);
        if (count % 10 == 0) {
          log.warn(
              "Node agent {} has not been responding for count {}- {}",
              nodeAgent.uuid,
              count,
              e.getMessage());
        }
        Instant expiryDate =
            Instant.now().minus(param.getLifetime().toMinutes(), ChronoUnit.MINUTES);
        if (expiryDate.isAfter(nodeAgent.updatedAt.toInstant())) {
          // Purge the node agent record and its certs.
          Set<String> nodeIps =
              NodeInstance.getAll()
                  .stream()
                  .map(node -> node.getDetails().ip)
                  .collect(Collectors.toSet());
          if (!nodeIps.contains(nodeAgent.ip)) {
            log.info(
                "Purging node agent {} because connection failed. Error: {}",
                nodeAgent.uuid,
                e.getMessage());
            nodeAgentManager.purge(nodeAgent);
          }
        }
        return;
      }
      boolean wasDead = !isNodeAgentAlive();
      lastFailedCount.set(0);
      if (wasDead) {
        // Return to schedule on the live executor.
        return;
      }
      switch (nodeAgent.state) {
        case READY:
          {
            String ybaVersion = param.getSoftwareVersion();
            if (Util.compareYbVersions(ybaVersion, nodeAgent.version, true) == 0) {
              nodeAgent.heartbeat();
              return;
            }
            if (!acquireUpgradeToken()) {
              return;
            }
            nodeAgent.saveState(State.UPGRADE);
            // Fall-thru to complete in single cycle.
          }
        case UPGRADE:
          {
            if (!acquireUpgradeToken()) {
              return;
            }
            log.info("Initiating upgrade for node agent {}", nodeAgent.uuid);
            InstallerFiles installerFiles = nodeAgentManager.getInstallerFiles(nodeAgent, null);
            // Upload the installer files including new cert and key to the remote node agent.
            uploadInstallerFiles(nodeAgent, installerFiles);
            NodeAgentUpgradeParam upgradeParam =
                NodeAgentUpgradeParam.builder()
                    .certDir(installerFiles.getCertDir())
                    .packagePath(installerFiles.getPackagePath())
                    .build();
            // Set up the config and symlink on the remote node agent.
            nodeAgentClient.startUpgrade(nodeAgent, upgradeParam);
            // Point the node agent to the new cert and key locally.
            // At this point, the node agent is still with old cert and key.
            // So, this client has to trust both old and new certs.
            // The new key should also work on node agent.
            nodeAgentManager.replaceCerts(nodeAgent);
            nodeAgent.saveState(State.UPGRADED);
            // Fall-thru to complete in single cycle.
          }
        case UPGRADED:
          {
            if (!acquireUpgradeToken()) {
              return;
            }
            log.info("Finalizing upgrade for node agent {}", nodeAgent.uuid);
            // Inform the node agent to restart and load the new cert and key on restart.
            String nodeAgentHome = nodeAgentClient.finalizeUpgrade(nodeAgent);
            PingResponse pingResponse =
                nodeAgentClient.waitForServerReady(nodeAgent, Duration.ofMinutes(2));
            ServerInfo serverInfo = pingResponse.getServerInfo();
            if (serverInfo.getRestartNeeded()) {
              log.info("Server restart is needed for node agent {}", nodeAgent.uuid);
            } else {
              // If the node has restarted and loaded the new cert and key,
              // delete the local merged certs.
              nodeAgentManager.postUpgrade(nodeAgent);
              nodeAgent.home = nodeAgentHome;
              nodeAgent.version = serverInfo.getVersion();
              nodeAgent.saveState(State.READY);
              releaseUpgradeToken();
              log.info("Node agent {} has been upgraded successfully", nodeAgent.uuid);
              // Release the node agent is already upgraded.
            }
            break;
          }
        default:
          log.trace("Unhandled state: {}", nodeAgent.state);
      }
    }
  }

  @VisibleForTesting
  PollerTask createPollerTask(PollerTaskParam param) {
    return new PollerTask(param);
  }

  /** Starts background tasks. */
  public void init() {
    Duration pollerInterval = confGetter.getGlobalConf(GlobalConfKeys.nodeAgentPollerInterval);
    if (pollerInterval.isZero()) {
      throw new IllegalArgumentException(
          String.format(
              "%s must be greater than 0", GlobalConfKeys.nodeAgentPollerInterval.getKey()));
    }
    Integer maxParallelUpgrades =
        confGetter.getGlobalConf(GlobalConfKeys.maxParallelNodeAgentUpgrades);
    if (maxParallelUpgrades == null || maxParallelUpgrades < 1) {
      throw new IllegalArgumentException(
          String.format(
              "%s must be greater than 0", GlobalConfKeys.maxParallelNodeAgentUpgrades.getKey()));
    }
    livePollerExecutor =
        platformExecutorFactory.createExecutor(
            LIVE_POLLER_POOL_NAME,
            new ThreadFactoryBuilder().setNameFormat("NodeAgentLivePoller-%d").build());
    deadPollerExecutor =
        platformExecutorFactory.createExecutor(
            DEAD_POLLER_POOL_NAME,
            new ThreadFactoryBuilder().setNameFormat("NodeAgentDeadPoller-%d").build());
    activeUpgrades = new Semaphore(maxParallelUpgrades);
    log.info("Scheduling poller service");
    platformScheduler.schedule(
        NodeAgentHandler.class.getSimpleName() + "Poller",
        POLLER_INITIAL_DELAY,
        pollerInterval,
        this::pollerService);
  }

  private void uploadInstallerFiles(NodeAgent nodeAgent, InstallerFiles installerFiles) {
    Set<String> dirs =
        installerFiles
            .getCreateDirs()
            .stream()
            .map(dir -> dir.toString())
            .collect(Collectors.toSet());
    log.info("Creating directories {} on node agent {}", dirs, nodeAgent.uuid);
    List<String> command = ImmutableList.<String>builder().add("mkdir", "-p").addAll(dirs).build();
    nodeAgentClient.executeCommand(nodeAgent, command);
    installerFiles
        .getCopyFileInfos()
        .stream()
        .forEach(
            f -> {
              log.info(
                  "Uploading {} to {} on node agent {}",
                  f.getSourcePath(),
                  f.getTargetPath(),
                  nodeAgent.uuid);
              nodeAgentClient.uploadFile(
                  nodeAgent, f.getSourcePath().toString(), f.getTargetPath().toString());
              if (StringUtils.isNotBlank(f.getPermission())) {
                nodeAgentClient.executeCommand(
                    nodeAgent,
                    Lists.newArrayList("chmod", f.getPermission(), f.getTargetPath().toString()));
              }
            });
  }

  /**
   * This method is run in interval. Some node agents may not be responding at the moment. Once they
   * come up, they may recover from their states and change to LIVE. Then, they are notified to
   * upgrade. After that, this method becomes idle. It can be improved to do in batches.
   */
  @VisibleForTesting
  void pollerService() {
    try {
      Duration duration = confGetter.getGlobalConf(GlobalConfKeys.deadNodeAgentRetention);
      String softwareVersion =
          Objects.requireNonNull(
              (String) configHelper.getConfig(ConfigType.SoftwareVersion).get("version"));
      Set<UUID> nodeUuids = new HashSet<>();
      NodeAgent.getAll()
          .stream()
          .filter(n -> n.state != State.REGISTERING)
          .peek(n -> nodeUuids.add(n.uuid))
          .map(
              n ->
                  pollerTasks.computeIfAbsent(
                      n.uuid,
                      k ->
                          createPollerTask(
                              PollerTaskParam.builder()
                                  .nodeAgentUuid(n.uuid)
                                  .softwareVersion(softwareVersion)
                                  .lifetime(duration)
                                  .build())))
          .filter(PollerTask::isSchedulable)
          .forEach(p -> p.schedule(p.isNodeAgentAlive() ? livePollerExecutor : deadPollerExecutor));
      Iterator<Entry<UUID, PollerTask>> iter = pollerTasks.entrySet().iterator();
      while (iter.hasNext()) {
        Entry<UUID, PollerTask> entry = iter.next();
        if (!nodeUuids.contains(entry.getKey())) {
          entry.getValue().releaseUpgradeToken();
          iter.remove();
        }
      }
    } catch (Exception e) {
      log.error("Error in pollerService - " + e.getMessage(), e);
    }
  }
}
