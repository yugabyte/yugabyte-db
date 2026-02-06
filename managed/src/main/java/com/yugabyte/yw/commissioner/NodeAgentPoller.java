// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import static com.google.common.base.Preconditions.checkState;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableList;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.NodeAgentClient.NodeAgentUpgradeParam;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.common.NodeAgentManager.InstallerFiles;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.controllers.handlers.NodeAgentHandler;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.nodeagent.PingResponse;
import com.yugabyte.yw.nodeagent.ServerInfo;
import io.prometheus.metrics.core.metrics.Gauge;
import io.prometheus.metrics.model.registry.PrometheusRegistry;
import java.nio.file.Paths;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Date;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicBoolean;
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
  private static final Duration POLLER_INITIAL_DELAY = Duration.ofMinutes(5);
  private static final String LIVE_POLLER_POOL_NAME = "node_agent.live_node_poller";
  private static final String DEAD_POLLER_POOL_NAME = "node_agent.dead_node_poller";
  private static final String UPGRADER_POOL_NAME = "node_agent.upgrader";
  private static final int MAX_FAILED_CONN_COUNT = 10;

  private static final String NODE_AGENT_VERSION_MISMATCH_NAME = "ybp_nodeagent_version_mismatch";
  private static final Gauge NODE_AGENT_VERSION_MISMATCH_GAUGE =
      Gauge.builder()
          .name(NODE_AGENT_VERSION_MISMATCH_NAME)
          .help("Has Node Agent version mismatched")
          .labelNames(
              KnownAlertLabels.NODE_AGENT_UUID.labelName(),
              KnownAlertLabels.NODE_ADDRESS.labelName())
          .register(PrometheusRegistry.defaultRegistry);

  private static final String NODE_AGENT_SERVER_CERT_EXPIRING_NAME =
      "ybp_nodeagent_server_cert_expiring";
  private static final Gauge NODE_AGENT_SERVER_CERT_EXPIRING_GAUGE =
      Gauge.builder()
          .name(NODE_AGENT_SERVER_CERT_EXPIRING_NAME)
          .help("Is Node Agent server cert expiring")
          .labelNames(
              KnownAlertLabels.NODE_AGENT_UUID.labelName(),
              KnownAlertLabels.NODE_ADDRESS.labelName())
          .register(PrometheusRegistry.defaultRegistry);

  private static final String NODE_AGENT_CONNECTION_NAME = "ybp_nodeagent_connection";
  private static final Gauge NODE_AGENT_CONNECTION_GAUGE =
      Gauge.builder()
          .name(NODE_AGENT_CONNECTION_NAME)
          .help("Is Node Agent connection successful")
          .labelNames(
              KnownAlertLabels.NODE_AGENT_UUID.labelName(),
              KnownAlertLabels.NODE_ADDRESS.labelName())
          .register(PrometheusRegistry.defaultRegistry);

  private final RuntimeConfGetter confGetter;
  private final PlatformExecutorFactory platformExecutorFactory;
  private final PlatformScheduler platformScheduler;
  private final NodeAgentClient nodeAgentClient;
  private final NodeAgentManager nodeAgentManager;
  private final SwamperHelper swamperHelper;

  private final Map<UUID, PollerTask> pollerTasks = new ConcurrentHashMap<>();

  // Poller with more threads allowing less queuing.
  private ExecutorService livePollerExecutor;
  // Poller with less threads allowing more queuing.
  private ExecutorService deadPollerExecutor;
  // Upgrade pool to not starve poller pools.
  private ExecutorService upgradeExecutor;

  @Inject
  public NodeAgentPoller(
      RuntimeConfGetter confGetter,
      PlatformExecutorFactory platformExecutorFactory,
      PlatformScheduler platformScheduler,
      NodeAgentManager nodeAgentManager,
      NodeAgentClient nodeAgentClient,
      SwamperHelper swamperHelper) {
    this.confGetter = confGetter;
    this.platformExecutorFactory = platformExecutorFactory;
    this.platformScheduler = platformScheduler;
    this.nodeAgentManager = nodeAgentManager;
    this.nodeAgentClient = nodeAgentClient;
    this.swamperHelper = swamperHelper;
  }

  enum PollerTaskState {
    IDLE,
    SCHEDULED,
    RUNNING,
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
    private final AtomicReference<PollerTaskState> stateRef = new AtomicReference<>();
    private final AtomicBoolean isUpgrading = new AtomicBoolean();
    private volatile int lastFailedCount;
    // Future for the upgrade task.
    private volatile Future<?> future;
    // Prometheus swamper target file status.
    private boolean isTargetFileWritten = false;

    private PollerTask(PollerTaskParam param) {
      this.param = param;
      this.stateRef.set(PollerTaskState.IDLE);
    }

    PollerTaskParam getParam() {
      return param;
    }

    @VisibleForTesting
    void setState(PollerTaskState state) {
      stateRef.set(state);
    }

    private void schedule(ExecutorService pollerExecutor) {
      if (stateRef.compareAndSet(PollerTaskState.IDLE, PollerTaskState.SCHEDULED)) {
        try {
          pollerExecutor.submit(this);
        } catch (RejectedExecutionException e) {
          stateRef.set(PollerTaskState.IDLE);
          log.warn(
              "Failed to schedule poller task for {} - {}. Will be retried later",
              param.getNodeAgentUuid(),
              e.getMessage());
        }
      }
    }

    private boolean isSchedulable() {
      return stateRef.get() == PollerTaskState.IDLE;
    }

    private boolean isNodeAgentAlive() {
      return lastFailedCount < MAX_FAILED_CONN_COUNT;
    }

    private boolean versionMatched(NodeAgent nodeAgent) {
      String ybaVersion = param.getSoftwareVersion();
      boolean versionMatched = Util.areYbVersionsEqual(ybaVersion, nodeAgent.getVersion(), true);
      publishMetric(nodeAgent, NODE_AGENT_VERSION_MISMATCH_GAUGE, versionMatched ? 0 : 1);
      if (!versionMatched) {
        log.debug("YBA version is {}. Version mismatched for node agent {}", ybaVersion, nodeAgent);
      }
      return versionMatched;
    }

    private boolean needsUpgrade(NodeAgent nodeAgent) {
      if (!versionMatched(nodeAgent)) {
        return true;
      }
      // This handles the rare case where YBA has never been upgraded close to a year.
      // There is a chance that while an ongoing API call is made, upgrade starts kicking in, but
      // it is very rare because this happens if YBA has not been upgraded for almost a year and
      // every API call first checks if node agent needs an upgrade and waits if an upgrade is
      // currently running.
      Date expiresAt = nodeAgent.getServerCertExpiry();
      Duration duration = confGetter.getGlobalConf(GlobalConfKeys.nodeAgentServerCertExpiryNotice);
      boolean expiring =
          Instant.now()
              .plus(duration.getSeconds(), ChronoUnit.SECONDS)
              .isAfter(nodeAgent.getServerCertExpiry().toInstant());
      publishMetric(nodeAgent, NODE_AGENT_SERVER_CERT_EXPIRING_GAUGE, expiring ? 0 : 1);
      if (expiring) {
        log.debug("Node agent server cert is expiring soon on {}", expiresAt);
        return true;
      }
      return false;
    }

    @VisibleForTesting
    synchronized void waitForUpgrade() {
      while (isUpgrading.get()) {
        try {
          log.info("Waiting for ongoing upgrade on node agent {}", param.getNodeAgentUuid());
          wait();
        } catch (InterruptedException e) {
          throw new RuntimeException(e);
        }
      }
    }

    private synchronized void cancelUpgrade() {
      if (isUpgrading.get()) {
        Future<?> f = future;
        if (f != null) {
          f.cancel(true);
        }
        notifyAfterUpgrade();
      }
    }

    private synchronized void notifyAfterUpgrade() {
      isUpgrading.set(false);
      future = null;
      notifyAll();
    }

    @Override
    public void run() {
      if (stateRef.compareAndSet(PollerTaskState.SCHEDULED, PollerTaskState.RUNNING)) {
        try {
          NodeAgent.maybeGet(param.getNodeAgentUuid()).ifPresent(n -> poll(n));
        } catch (Throwable t) {
          log.error(
              "Error in polling for node {} - {}", param.getNodeAgentUuid(), t.getMessage(), t);
          if (t instanceof Error) {
            throw (Error) t;
          }
        } finally {
          stateRef.set(PollerTaskState.IDLE);
        }
      }
    }

    private void poll(NodeAgent nodeAgent) {
      if (!isTargetFileWritten) {
        // This method checks if the file already exists to ignore writing again.
        swamperHelper.writeNodeAgentTargetJson(nodeAgent);
        isTargetFileWritten = true;
      }
      try {
        nodeAgentClient.waitForServerReady(nodeAgent, Duration.ofSeconds(2));
        publishMetric(nodeAgent, NODE_AGENT_CONNECTION_GAUGE, 1);
      } catch (RuntimeException e) {
        if (lastFailedCount < MAX_FAILED_CONN_COUNT) {
          lastFailedCount++;
        }
        if (lastFailedCount % 10 == 0) {
          log.warn(
              "Node agent {} has not been responding for count {}- {}",
              nodeAgent,
              lastFailedCount,
              e.getMessage());
        }
        publishMetric(nodeAgent, NODE_AGENT_CONNECTION_GAUGE, 0);
        Instant expiryDate =
            Instant.now().minus(param.getLifetime().toMinutes(), ChronoUnit.MINUTES);
        if (expiryDate.isAfter(nodeAgent.getUpdatedAt().toInstant())) {
          Customer customer = Customer.getOrBadRequest(nodeAgent.getCustomerUuid());
          Set<String> nodeIps =
              NodeInstance.listByCustomer(customer.getUuid()).stream()
                  .map(node -> node.getDetails().ip)
                  .collect(Collectors.toSet());
          customer.getUniverses().stream()
              .flatMap(u -> u.getNodes().stream())
              .filter(
                  n ->
                      n.cloudInfo != null
                          && n.cloudInfo.private_ip != null
                          && !nodeIps.contains(n.cloudInfo.private_ip))
              .map(n -> n.cloudInfo.private_ip)
              .forEach(nodeIps::add);
          if (!nodeIps.contains(nodeAgent.getIp())) {
            log.info(
                "Purging node agent {} because connection failed. Error: {}",
                nodeAgent,
                e.getMessage());
            // Purge the node agent record and its certs.
            nodeAgentManager.purge(nodeAgent);
          }
        }
        return;
      }
      nodeAgent.heartbeat();
      boolean wasDead = !isNodeAgentAlive();
      lastFailedCount = 0;
      if (wasDead) {
        // Return to schedule on the live executor.
        return;
      }
      switch (nodeAgent.getState()) {
        case READY:
          if (!needsUpgrade(nodeAgent)) {
            return;
          }
          // Fall-thru to complete in single cycle.
        case UPGRADE:
        case UPGRADED:
          if (!isUpgrading.compareAndSet(false, true)) {
            log.info("Node agent {} is being upgraded", nodeAgent);
            return;
          }
          // In a rare case, the node could have just been upgraded. It is ok because the node agent
          // state is refreshed after this exclusive access to check the state again before the
          // upgrade, preventing double upgrade.
          try {
            log.info("Submitting upgrade task for node agent {}", nodeAgent);
            // Submit to upgrade pool to not starve poller.
            future =
                upgradeExecutor.submit(
                    () -> {
                      try {
                        Util.doWithCorrelationId(
                            id -> {
                              log.info("Upgrading node agent {} via poller", nodeAgent);
                              upgradeNodeAgent(nodeAgent);
                              return null;
                            });
                      } finally {
                        notifyAfterUpgrade();
                      }
                    });
          } catch (Exception e) {
            notifyAfterUpgrade();
            log.warn(
                "Upgrade for node agent {} cannot be scheduled at the moment - {}",
                nodeAgent,
                e.getMessage());
          }
          break;
        default:
          log.trace("Unhandled state for node agent {}", nodeAgent);
      }
    }

    // This handles upgrade for the given node agent.
    private void upgradeNodeAgent(NodeAgent nodeAgent) {
      if (HighAvailabilityConfig.isFollower()) {
        // Task may have already been submitted. This check ensures that submitted tasks are not
        // run.
        log.info("Skipping node agent upgrade for {} as it is a follower instance", nodeAgent);
        return;
      }
      nodeAgent.refresh();
      checkState(nodeAgent.isActive(), "Invalid state for node agent " + nodeAgent);
      if (nodeAgent.getState() == State.READY) {
        if (!needsUpgrade(nodeAgent)) {
          log.debug("Node agent {} does not need an upgrade", nodeAgent);
          return;
        }
        nodeAgent.saveState(State.UPGRADE);
      }
      if (nodeAgent.getState() == State.UPGRADE) {
        log.info("Uploading upgrade files for node agent {}", nodeAgent);
        InstallerFiles installerFiles =
            nodeAgentManager.getInstallerFiles(
                nodeAgent, Paths.get(nodeAgent.getHome()), versionMatched(nodeAgent));
        // Upload the installer files including new cert and key to the remote node agent.
        uploadInstallerFiles(nodeAgent, installerFiles);
        log.info("Uploaded upgrade files for node agent {}", nodeAgent);
        NodeAgentUpgradeParam upgradeParam =
            NodeAgentUpgradeParam.builder()
                .certDir(installerFiles.getCertDir())
                .packagePath(installerFiles.getPackagePath())
                .build();
        log.info("Starting remote upgrade for node agent {}", nodeAgent);
        // Set up the config and symlink on the remote node agent.
        nodeAgentClient.startUpgrade(nodeAgent, upgradeParam);
        log.info("Completed remote upgrade for node agent", nodeAgent);
        // Point the node agent to the new cert and key locally.
        // At this point, the node agent is still with old cert and key.
        // So, this client has to trust both old and new certs.
        // The new key should also work on node agent.
        // Update the state atomically with the cert update.
        nodeAgentManager.replaceCerts(nodeAgent);
        log.info("Rolled over to new certs for node agent {}", nodeAgent);
      }
      if (nodeAgent.getState() == State.UPGRADED) {
        log.info("Finalizing upgrade for node agent {}", nodeAgent);
        // Inform the node agent to restart and load the new cert and key on restart.
        String nodeAgentHome = nodeAgentClient.finalizeUpgrade(nodeAgent);
        PingResponse pingResponse =
            nodeAgentClient.waitForServerReady(nodeAgent, Duration.ofMinutes(2));
        ServerInfo serverInfo = pingResponse.getServerInfo();
        if (serverInfo.getRestartNeeded()) {
          log.info("Server restart is needed for node agent {}", nodeAgent);
        } else {
          // If the node has restarted and loaded the new cert and key,
          // delete the local merged certs.
          nodeAgentManager.postUpgrade(nodeAgent);
          nodeAgent.finalizeUpgrade(nodeAgentHome, serverInfo.getVersion());
          log.info("Node agent {} has been upgraded successfully", nodeAgent);
        }
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
    // Sync once on startup because the in-memory tracker can lose some UUIDs on restart.
    syncNodeAgentTargetJsons();
    livePollerExecutor =
        platformExecutorFactory.createExecutor(
            LIVE_POLLER_POOL_NAME,
            new ThreadFactoryBuilder().setNameFormat("NodeAgentLivePoller-%d").build());
    deadPollerExecutor =
        platformExecutorFactory.createExecutor(
            DEAD_POLLER_POOL_NAME,
            new ThreadFactoryBuilder().setNameFormat("NodeAgentDeadPoller-%d").build());
    upgradeExecutor =
        platformExecutorFactory.createExecutor(
            UPGRADER_POOL_NAME,
            new ThreadFactoryBuilder().setNameFormat("NodeAgentUpgrader-%d").build());
    log.info("Scheduling poller service");
    platformScheduler.schedule(
        NodeAgentHandler.class.getSimpleName() + "Poller",
        POLLER_INITIAL_DELAY,
        pollerInterval,
        this::pollerService);
  }

  private static void publishMetric(NodeAgent nodeAgent, Gauge guage, double value) {
    guage
        .labelValues(
            nodeAgent.getUuid().toString(),
            String.format("%s:%s", nodeAgent.getIp(), nodeAgent.getPort()))
        .set(value);
  }

  @VisibleForTesting
  void setUpgradeExecutor(ExecutorService upgradeExecutor) {
    this.upgradeExecutor = upgradeExecutor;
  }

  private void uploadInstallerFiles(NodeAgent nodeAgent, InstallerFiles installerFiles) {
    Set<String> dirs =
        installerFiles.getCreateDirs().stream()
            .map(dir -> dir.toString())
            .collect(Collectors.toSet());
    if (dirs.size() > 0) {
      log.info("Creating directories {} on node agent {}", dirs, nodeAgent);
      List<String> command =
          ImmutableList.<String>builder().add("mkdir", "-p").addAll(dirs).build();
      nodeAgentClient.executeCommand(nodeAgent, command).processErrors();
    }
    try {
      installerFiles.getCopyFileInfos().stream()
          .forEach(
              f -> {
                log.info(
                    "Uploading {} to {} on node agent {}",
                    f.getSourcePath(),
                    f.getTargetPath(),
                    nodeAgent);
                int perm = 0;
                if (StringUtils.isNotBlank(f.getPermission())) {
                  try {
                    perm = Integer.parseInt(f.getPermission().trim(), 8);
                  } catch (NumberFormatException e) {
                  }
                }
                nodeAgentClient.uploadFile(
                    nodeAgent,
                    f.getSourcePath().toString(),
                    f.getTargetPath().toString(),
                    null /*user*/,
                    perm,
                    null /*timeout*/);
              });
    } finally {
      installerFiles.cleanupCopiedFiles();
    }
  }

  void syncNodeAgentTargetJsons() {
    Set<UUID> nodeUuids =
        NodeAgent.getAll().stream().map(NodeAgent::getUuid).collect(Collectors.toSet());
    swamperHelper.getTargetNodeAgentUuids().stream()
        .filter(uuid -> !nodeUuids.contains(uuid))
        .forEach(uuid -> swamperHelper.removeNodeAgentTargetJson(uuid));
  }

  private PollerTask getOrCreatePollerTask(
      UUID nodeAgentUuid, Duration lifetime, String softwareVersion) {
    return pollerTasks.computeIfAbsent(
        nodeAgentUuid,
        k ->
            createPollerTask(
                PollerTaskParam.builder()
                    .nodeAgentUuid(nodeAgentUuid)
                    .softwareVersion(softwareVersion)
                    .lifetime(lifetime)
                    .build()));
  }

  /**
   * This method is run in interval. Some node agents may not be responding at the moment. Once they
   * come up, they may recover from their states and change to LIVE. Then, they are notified to
   * upgrade. After that, this method becomes idle. It can be improved to do in batches.
   */
  @VisibleForTesting
  void pollerService() {
    try {
      Duration lifetime = confGetter.getGlobalConf(GlobalConfKeys.deadNodeAgentRetention);
      String softwareVersion = nodeAgentManager.getSoftwareVersion();
      Set<UUID> nodeUuids = new HashSet<>();
      NodeAgent.getAll().stream()
          .filter(n -> n.isActive())
          .peek(n -> nodeUuids.add(n.getUuid()))
          .map(n -> getOrCreatePollerTask(n.getUuid(), lifetime, softwareVersion))
          .filter(PollerTask::isSchedulable)
          .forEach(p -> p.schedule(p.isNodeAgentAlive() ? livePollerExecutor : deadPollerExecutor));
      Iterator<Entry<UUID, PollerTask>> iter = pollerTasks.entrySet().iterator();
      while (iter.hasNext()) {
        Entry<UUID, PollerTask> entry = iter.next();
        if (!nodeUuids.contains(entry.getKey())) {
          entry.getValue().cancelUpgrade();
          swamperHelper.removeNodeAgentTargetJson(entry.getKey());
          iter.remove();
        }
      }
      nodeAgentClient.cleanupCachedClients();
    } catch (Exception e) {
      log.error("Error in pollerService - " + e.getMessage(), e);
    }
  }

  /**
   * Upgrades the given node agent forcefully if there is no running scheduled upgrade. If a
   * scheduled upgrade is running, it waits for the upgrade to finish.
   *
   * @param nodeAgentUuid the given node agent UUID.
   * @param skipOnUnreachable skip upgrade if server is unreachable.
   * @return true if there was an upgrade else false.
   */
  public boolean upgradeNodeAgent(UUID nodeAgentUuid, boolean skipOnUnreachable) {
    NodeAgent nodeAgent = NodeAgent.getOrBadRequest(nodeAgentUuid);
    Duration lifetime = confGetter.getGlobalConf(GlobalConfKeys.deadNodeAgentRetention);
    String softwareVersion = nodeAgentManager.getSoftwareVersion();
    PollerTask pollerTask = getOrCreatePollerTask(nodeAgentUuid, lifetime, softwareVersion);
    if (!pollerTask.needsUpgrade(nodeAgent)) {
      log.trace("Node agent {} does not need an upgrade", nodeAgent);
      return false;
    }
    log.info("Node agent {} needs an upgrade", nodeAgent);
    try {
      nodeAgentClient.waitForServerReady(nodeAgent, Duration.ofSeconds(2));
    } catch (RuntimeException e) {
      if (skipOnUnreachable) {
        return false;
      }
      throw e;
    }
    if (!pollerTask.isUpgrading.compareAndSet(false, true)) {
      log.info("Upgrade is already running for node agent {}", nodeAgent);
      pollerTask.waitForUpgrade();
    } else {
      try {
        log.info("Starting explicit upgrade on node agent {}", nodeAgent);
        pollerTask.upgradeNodeAgent(nodeAgent);
      } catch (RuntimeException e) {
        log.error("Explicit upgrade failed for node agent {}", nodeAgent);
        throw e;
      } finally {
        pollerTask.notifyAfterUpgrade();
      }
    }
    nodeAgent.refresh();
    if (pollerTask.needsUpgrade(nodeAgent)) {
      throw new RuntimeException(
          String.format("Node agent %s still needs upgrade after an upgrade", nodeAgent));
    }
    return true;
  }
}
