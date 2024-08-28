// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Stopwatch;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Date;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
/**
 * Auto node agent enabler running in the background to migrate universes to node agents. The first
 * step is to mark the universes pending node agent installations. As long as the marker is present,
 * the universe cannot use node agents for communication. The marker can also be set externally when
 * a new node is added while client is disabled for the provider.*
 */
public class NodeAgentEnabler {
  private static final String UNIVERSE_INSTALLER_POOL_NAME =
      "node_agent.enabler.universe_installer";
  private static final String NODE_INSTALLER_POOL_NAME = "node_agent.enabler.node_installer";
  private static final Duration SCANNER_INITIAL_DELAY = Duration.ofMinutes(5);

  private final RuntimeConfGetter confGetter;
  private final PlatformExecutorFactory platformExecutorFactory;
  private final PlatformScheduler platformScheduler;
  private final NodeAgentInstaller nodeAgentInstaller;
  private final Map<UUID, UniverseNodeAgentInstaller> customerNodeAgentInstallers;
  // This controls the number of customers as only one universe is picked at a time for each
  // customer.
  private ExecutorService universeInstallerExecutor;

  @Inject
  public NodeAgentEnabler(
      RuntimeConfGetter confGetter,
      PlatformExecutorFactory platformExecutorFactory,
      PlatformScheduler platformScheduler,
      NodeAgentInstaller nodeAgentInstaller) {
    this.confGetter = confGetter;
    this.platformExecutorFactory = platformExecutorFactory;
    this.platformScheduler = platformScheduler;
    this.nodeAgentInstaller = nodeAgentInstaller;
    this.customerNodeAgentInstallers = new ConcurrentHashMap<>();
  }

  public void init() {
    Duration scannerInterval =
        confGetter.getGlobalConf(GlobalConfKeys.nodeAgentEnablerScanInterval);
    if (scannerInterval.isZero()) {
      log.info("Node agent enabler is disabled because the scanner interval is to zero");
      return;
    }
    // Mark the eligible universes on init.
    // TODO we may not want to run this everytime on startup. Will be fixed in subsequent tasks.
    markUniverses();
    universeInstallerExecutor =
        platformExecutorFactory.createExecutor(
            UNIVERSE_INSTALLER_POOL_NAME,
            new ThreadFactoryBuilder().setNameFormat("UniverseNodeAgentInstaller-%d").build());
    platformScheduler.schedule(
        NodeAgentEnabler.class.getSimpleName(),
        SCANNER_INITIAL_DELAY,
        scannerInterval,
        this::scanUniverses);
  }

  /**
   * Mark all the eligible universes for enabling node agents. The marker field is also used to
   * quickly decide if the universe can use node-agent or not. As long as the marker field is set to
   * true, the universe has nodes pending node-agent installation.
   */
  public void markUniverses() {
    Customer.getAll()
        .forEach(
            c ->
                c.getUniverses()
                    .forEach(
                        universe -> {
                          UniverseDefinitionTaskParams universeDetails =
                              universe.getUniverseDetails();
                          Cluster primaryCluster = universeDetails.getPrimaryCluster();
                          if (primaryCluster.userIntent == null
                              || primaryCluster.userIntent.providerType == CloudType.kubernetes) {
                            return;
                          }
                          if (universeDetails.installNodeAgent) {
                            // Marker already set.
                            return;
                          }
                          Provider provider =
                              Provider.getOrBadRequest(
                                  UUID.fromString(primaryCluster.userIntent.provider));
                          boolean isClientEnabled =
                              confGetter.getConfForScope(
                                  provider, ProviderConfKeys.enableNodeAgentClient);
                          ProviderDetails providerDetails = provider.getDetails();
                          if (isClientEnabled && providerDetails.enableNodeAgent) {
                            // Nothing to be done as node agent is enabled.
                            return;
                          }
                          Universe.saveUniverseDetails(
                              universe.getUniverseUUID(),
                              null /* version increment CB */,
                              u -> {
                                UniverseDefinitionTaskParams d = u.getUniverseDetails();
                                d.installNodeAgent = true;
                                u.setUniverseDetails(d);
                              });
                        }));
  }

  // Used only for testing.
  @VisibleForTesting
  void setUniverseInstallerExecutor(ExecutorService installerExecutor) {
    this.universeInstallerExecutor = installerExecutor;
  }

  /**
   * Scans universes and enables one universe at a time for each customer while customers are
   * processed concurrently.
   */
  @VisibleForTesting
  void scanUniverses() {
    try {
      // Sort customer by name for deterministic order.
      Iterator<Customer> customerIter =
          Customer.getAll().stream()
              .sorted(
                  Comparator.comparing(Customer::getCreationDate).thenComparing(Customer::getName))
              .iterator();
      while (customerIter.hasNext()) {
        Customer customer = customerIter.next();
        UniverseNodeAgentInstaller installer = customerNodeAgentInstallers.get(customer.getUuid());
        if (installer != null) {
          log.info(
              "Found in-progress installer for universe {} and customer {}",
              installer.getUniverseUuid(),
              installer.getCustomerUuid());
          Optional<Universe> universeOpt = Universe.maybeGet(installer.getUniverseUuid());
          if (universeOpt.isPresent()) {
            try {
              log.debug(
                  "Waiting briefly for node agent installation to complete on universe {}",
                  installer.getUniverseUuid());
              installer.future.get(1, TimeUnit.SECONDS);
              customerNodeAgentInstallers.remove(customer.getUuid());
              log.debug("Installation completed for universe {}", installer.getUniverseUuid());
            } catch (InterruptedException e) {
              log.warn(
                  "Wait interrupted for installer for universe {}", installer.getUniverseUuid());
              installer.cancelAll();
              customerNodeAgentInstallers.remove(customer.getUuid());
              break;
            } catch (ExecutionException e) {
              log.error(
                  "Installation failed for universe {} - {}",
                  installer.getUniverseUuid(),
                  e.getCause().getMessage());
              installer.cancelAll();
              customerNodeAgentInstallers.remove(customer.getUuid());
            } catch (TimeoutException e) {
              Duration timeout =
                  confGetter.getConfForScope(
                      universeOpt.get(), UniverseConfKeys.nodeAgentEnablerInstallTimeout);
              Instant expiresAt =
                  installer.getCreatedAt().plus(timeout.getSeconds(), ChronoUnit.SECONDS);
              if (expiresAt.isBefore(Instant.now())) {
                // There is still time before expiry. Go to next customer.
                continue;
              }
              log.error(
                  "Installation timed out for universe {} after {} secs",
                  installer.getUniverseUuid(),
                  timeout.getSeconds());
              // Cancel expired installer.
              installer.cancelAll();
              customerNodeAgentInstallers.remove(customer.getUuid());
            }
          } else {
            log.info(
                "Cancelling node agent installations because universe {} is not found",
                installer.getUniverseUuid());
            // Universe does not exist anymore.
            installer.cancelAll();
            customerNodeAgentInstallers.remove(customer.getUuid());
          }
        }
        log.debug("Continuing to the next eligible universe for customer {}", customer.getUuid());
        Iterator<Universe> universeIter =
            customer.getUniverses().stream()
                .sorted(
                    Comparator.comparing(Universe::getCreationDate)
                        .thenComparing(Universe::getName))
                .iterator();
        while (universeIter.hasNext()) {
          Universe universe = universeIter.next();
          // Round-robin to give equal priority to every universe within each customer.
          if (installer != null && installer.alreadyProcessed(universe)) {
            log.trace(
                "Skipping processed universe {} for customer {} in the current run of the schedule",
                universe.getName(),
                customer.getUuid());
            continue;
          }
          UniverseDefinitionTaskParams details = universe.getUniverseDetails();
          if (!details.installNodeAgent) {
            log.debug(
                "Skipping universe {} for customer {} as marker is not set",
                universe.getUniverseUUID(),
                customer.getUuid());
            // No marker set to install node-agent.
            continue;
          }
          UUID providerUuid = UUID.fromString(details.getPrimaryCluster().userIntent.provider);
          Optional<Provider> providerOpt = Provider.maybeGet(providerUuid);
          if (!providerOpt.isPresent()
              || !confGetter.getConfForScope(
                  providerOpt.get(), ProviderConfKeys.enableNodeAgentClient)) {
            // Leave the marker intact but skip the installation because the client is intentionally
            // disabled by the user.
            log.info(
                "Skipping installation for universe {} as node agent client is not enabled for"
                    + " provider {}",
                universe.getUniverseUUID(),
                providerUuid);
            continue;
          }
          log.info(
              "Picking up universe {} ({}) for customer {} for installation",
              universe.getName(),
              universe.getUniverseUUID(),
              customer.getUuid());
          try {
            installer = new UniverseNodeAgentInstaller(customer.getUuid(), universe);
            installer.future = CompletableFuture.runAsync(installer, universeInstallerExecutor);
            customerNodeAgentInstallers.put(customer.getUuid(), installer);
          } catch (RejectedExecutionException e) {
            log.error(
                "Failed to submit installer for universe {} - {}",
                universe.getUniverseUUID(),
                e.getMessage());
          }
        }
      }
    } catch (Exception e) {
      log.error("Error encountered in scanning universes to enable node agents", e);
    }
  }

  // Used only for testing.
  @VisibleForTesting
  void waitFor(Duration timeout) throws TimeoutException, InterruptedException {
    Duration waitTime = timeout;
    List<UniverseNodeAgentInstaller> installers =
        new ArrayList<>(customerNodeAgentInstallers.values());
    Stopwatch watch = Stopwatch.createStarted();
    while (installers.size() > 0) {
      Iterator<UniverseNodeAgentInstaller> iter = installers.iterator();
      while (iter.hasNext()) {
        CompletableFuture<Void> future = iter.next().getFuture();
        if (future != null) {
          try {
            long millis = waitTime.toMillis();
            if (millis < 1) {
              throw new TimeoutException();
            }
            future.get(millis, TimeUnit.MILLISECONDS);
            waitTime = timeout.minus(watch.elapsed());
            iter.remove();
          } catch (TimeoutException e) {
            throw e;
          } catch (InterruptedException e) {
            throw e;
          } catch (Exception e) {
            throw new RuntimeException(e);
          }
        }
      }
    }
  }

  /**
   * This must be implemented to handle the node agent installation on a node. The methods must
   * block the caller until they complete.
   */
  public interface NodeAgentInstaller {
    /** Install node agent on the node. */
    boolean install(UUID customerUuid, UUID universeUuid, NodeDetails nodeDetails) throws Exception;

    /**
     * Reinstall node agent on the node. The implementation may choose not to reinstall and return
     * false.
     */
    boolean reinstall(
        UUID customerUuid, UUID universeUuid, NodeDetails nodeDetails, NodeAgent nodeAgent)
        throws Exception;

    /** Set installNodeAgent property in the universe details to false by locking the universe. */
    boolean migrate(UUID customerUuid, UUID universeUuid) throws Exception;
  }

  /** Node agent installer for all nodes in a universe. */
  @Getter
  public class UniverseNodeAgentInstaller implements Runnable {
    private final Map<String, CompletableFuture<Boolean>> futures;
    private final Instant createdAt;
    private final UUID customerUuid;
    private final UUID universeUuid;
    private final String universeName;
    private final Date universeCreationDate;
    private volatile CompletableFuture<Void> future;
    // This controls the number of nodes within a universe.
    private ExecutorService nodeInstallerExecutor;

    public UniverseNodeAgentInstaller(UUID customerUuid, Universe universe) {
      this.customerUuid = customerUuid;
      this.universeUuid = universe.getUniverseUUID();
      this.universeName = universe.getName();
      this.universeCreationDate = universe.getCreationDate();
      this.futures = new ConcurrentHashMap<>();
      this.createdAt = Instant.now();
    }

    private void init() {
      nodeInstallerExecutor =
          platformExecutorFactory.createExecutor(
              NODE_INSTALLER_POOL_NAME,
              new ThreadFactoryBuilder().setNameFormat("NodeAgentInstaller-%d").build());
    }

    private void destroy() {
      if (nodeInstallerExecutor != null) {
        nodeInstallerExecutor.shutdownNow();
      }
    }

    @Override
    public void run() {
      Optional<Universe> universeOpt = Universe.maybeGet(universeUuid);
      if (!universeOpt.isPresent()) {
        return;
      }
      init();
      try {
        Universe universe = universeOpt.get();
        boolean isEnabled =
            processNodes(
                universe,
                node -> {
                  try {
                    String nodeIp = node.cloudInfo.private_ip;
                    Optional<NodeAgent> nodeAgentOpt = NodeAgent.maybeGetByIp(nodeIp);
                    if (!nodeAgentOpt.isPresent()) {
                      return nodeAgentInstaller.install(getCustomerUuid(), getUniverseUuid(), node);
                    }
                    if (nodeAgentOpt.get().getState() == NodeAgent.State.REGISTERING) {
                      return nodeAgentInstaller.reinstall(
                          getCustomerUuid(), getUniverseUuid(), node, nodeAgentOpt.get());
                    }
                    log.debug(
                        "Node agent is already installed for node {}({}) in universe {}",
                        node.getNodeName(),
                        nodeIp,
                        universe.getUniverseUUID());
                    return true;
                  } catch (Exception e) {
                    throw new RuntimeException(e);
                  }
                });
        if (!isEnabled) {
          log.warn("Node agents could not be enabled for universe {}", universe.getUniverseUUID());
        }
      } finally {
        destroy();
      }
    }

    // Process universe nodes to install node agents. If some nodes are added during this operation,
    // the migration will not happen and the next cycle of this call covers the new nodes. If some
    // nodes are deleted, migration will not happen due to installation failure and next cycle takes
    // care.
    private boolean processNodes(Universe universe, Function<NodeDetails, Boolean> callback) {
      if (universe.getNodes().stream().anyMatch(n -> n.state != NodeDetails.NodeState.Live)) {
        log.info(
            "Nodes cannot be processed for universe {} as there are non Live nodes",
            universe.getUniverseUUID());
        return false;
      }
      List<NodeDetails> nodes =
          universe.getNodes().stream()
              .filter(n -> n.cloudInfo != null && n.cloudInfo.private_ip != null)
              .collect(Collectors.toList());
      if (nodes.isEmpty()) {
        return false;
      }
      // This call is not needed as processNodes is not called repeatedly but it can be called if
      // there is a requirement to catch node change in a universe faster.
      cancelInvalidNodes(nodes);
      CountDownLatch latch = new CountDownLatch(nodes.size());
      nodes.forEach(
          n -> {
            String nodeIp = n.cloudInfo.private_ip;
            if (futures.containsKey(nodeIp)) {
              latch.countDown();
              log.debug(
                  "Node agent is already being installed on node {}({}) in universe",
                  n.getNodeName(),
                  nodeIp,
                  universe.getUniverseUUID());
              return;
            }
            CompletableFuture<Boolean> future = null;
            try {
              future =
                  CompletableFuture.supplyAsync(
                      () -> {
                        try {
                          return callback.apply(n);
                        } catch (Exception e) {
                          log.error(
                              "Failed to install node agent on node {}({}) in universe {} - {}",
                              n.getNodeName(),
                              nodeIp,
                              universe.getUniverseUUID(),
                              e.getMessage());
                        } finally {
                          latch.countDown();
                        }
                        return false;
                      },
                      nodeInstallerExecutor);
            } catch (RejectedExecutionException e) {
              // Installer not submitted, create a failed future.
              future = CompletableFuture.completedFuture(false);
              latch.countDown();
            }
            futures.put(nodeIp, future);
          });

      try {
        if (latch.await(1, TimeUnit.MINUTES)) {
          boolean allSucceeded =
              futures.entrySet().stream()
                  .allMatch(
                      entry -> {
                        try {
                          return entry.getValue().get(5, TimeUnit.SECONDS);
                        } catch (Exception e) {
                          log.error(
                              "Error in getting the execution result for IP {} in universe {} - {}",
                              entry.getKey(),
                              getUniverseUuid(),
                              e.getMessage());
                        }
                        return false;
                      });
          futures.clear();
          if (allSucceeded) {
            try {
              return nodeAgentInstaller.migrate(getCustomerUuid(), getUniverseUuid());
            } catch (Exception e) {
              log.error(
                  "Error in migrating to node agent for universe {} - {}",
                  getUniverseUuid(),
                  e.getMessage());
            }
            return false;
          }
        }
      } catch (InterruptedException e) {
        log.error(
            "Interrupted while waiting for installation to finish for universe {} - {}",
            universe.getUniverseUUID(),
            e.getMessage());
      }
      return false;
    }

    private void cancelInvalidNodes(List<NodeDetails> nodes) {
      if (futures.size() > 0) {
        Set<String> validIps =
            nodes.stream().map(n -> n.cloudInfo.private_ip).collect(Collectors.toSet());
        Iterator<Map.Entry<String, CompletableFuture<Boolean>>> iter =
            futures.entrySet().iterator();
        while (iter.hasNext()) {
          Map.Entry<String, CompletableFuture<Boolean>> entry = iter.next();
          if (!validIps.contains(entry.getKey())) {
            log.info(
                "Cancelling installation on node IP {} in universe {}",
                entry.getKey(),
                getUniverseUuid());
            entry.getValue().cancel(true);
            try {
              // Give some time to exit.
              entry.getValue().get(500, TimeUnit.MILLISECONDS);
            } catch (Exception e) {
              log.error(
                  "Cancellation failed for IP {} in universe {} - {}",
                  entry.getKey(),
                  getUniverseUuid(),
                  e.getMessage());
            } finally {
              iter.remove();
            }
          }
        }
      }
    }

    private void cancelAll() {
      log.info(
          "Cancelling installation for universe {} and customer {}",
          getUniverseUuid(),
          getCustomerUuid());
      futures.forEach(
          (n, f) -> {
            try {
              f.cancel(true);
            } catch (Exception e) {
              log.error(
                  "Error occurred while cancelling installation on node IP {} for universe {} - {}",
                  n,
                  getUniverseUuid(),
                  e.getMessage());
            }
          });
      futures.clear();
      future.cancel(true);
    }

    // Checks if the universe is already processed in the current round.
    private boolean alreadyProcessed(Universe universe) {
      int result = universe.getCreationDate().compareTo(getUniverseCreationDate());
      if (result == 0) {
        return universe.getName().compareTo(getUniverseName()) <= 0;
      }
      return result < 0;
    }
  }
}
