// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static com.google.common.base.Preconditions.checkState;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Stopwatch;
import com.google.common.collect.ImmutableSet;
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
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.function.Supplier;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
@Singleton
/**
 * Auto node agent enabler running in the background to migrate universes to node agents. The first
 * step is to mark the universes pending node agent installations. As long as the marker is present,
 * the universe cannot use node agents for communication. The marker can also be set externally when
 * a new node is added while client is disabled for the provider.
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
  private ExecutorService universeInstallerExecutor;
  private volatile boolean enabled;

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
    checkState(!isEnabled(), "Node agent enabler is already enabled");
    Duration scannerInterval =
        confGetter.getGlobalConf(GlobalConfKeys.nodeAgentEnablerScanInterval);
    if (scannerInterval.isZero()) {
      log.info("Node agent enabler is disabled because the scanner interval is to zero");
      return;
    }
    enable();
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
  @VisibleForTesting
  void markUniverses() {
    Customer.getAll()
        .forEach(
            c -> {
              AtomicReference<Set<String>> cachedIps = new AtomicReference<>();
              Supplier<Set<String>> supplier =
                  () -> {
                    Set<String> ips = cachedIps.get();
                    if (ips == null) {
                      ips =
                          NodeAgent.getAll(c.getUuid()).stream()
                              .filter(NodeAgent::isActive)
                              .map(NodeAgent::getIp)
                              .collect(ImmutableSet.toImmutableSet());
                      cachedIps.set(ips);
                    }
                    return ips;
                  };
              c.getUniverses().stream()
                  .filter(u -> !u.getUniverseDetails().installNodeAgent)
                  .filter(
                      u -> {
                        Optional<Boolean> optional =
                            isNodeAgentEnabled(u, p -> true /* include provider flag */);
                        return optional.isPresent() && optional.get() == false;
                      })
                  .filter(
                      u ->
                          u.getNodes().stream()
                              .anyMatch(
                                  n ->
                                      n.cloudInfo == null
                                          || StringUtils.isEmpty(n.cloudInfo.private_ip)
                                          || !supplier.get().contains(n.cloudInfo.private_ip)))
                  .forEach(u -> markUniverse(u.getUniverseUUID()));
            });
  }

  /**
   * Checks if node agent enabler is enabled.
   *
   * @return true if it is enabled else false.
   */
  public boolean isEnabled() {
    return enabled;
  }

  @VisibleForTesting
  void enable() {
    enabled = true;
  }

  /**
   * Checks if the universe should be marked for pending node agent installation. It returns true
   * for all the eligible universes even if the background installation may not happen because it is
   * not supported. This is for audit and future changes.
   *
   * @param universe the given universe.
   * @return true if it should be marked, else false.
   */
  public boolean shouldMarkUniverse(Universe universe) {
    return isEnabled() && isNodeAgentEnabled(universe, p -> true).orElse(false) == false;
  }

  /**
   * Checks if node agent client is enabled for the provider and the universe if it is non-null.
   * Client check adds additional requirements.
   *
   * @param provider the given provider.
   * @param universe the given universe.
   * @return true if the client is enabled.
   */
  public boolean isNodeAgentClientEnabled(Provider provider, @Nullable Universe universe) {
    if (!isNodeAgentServerEnabled(provider, universe)) {
      return false;
    }
    if (universe != null && universe.getUniverseDetails().installNodeAgent) {
      log.debug(
          "Node agent is not available on all nodes for universe {}", universe.getUniverseUUID());
      // Check if mixed mode is allowed.
      if (!confGetter.getConfForScope(universe, UniverseConfKeys.allowNodeAgentClientMixMode)) {
        return false;
      }
    }
    // All checks passed.
    return true;
  }

  /**
   * Checks if node agent server is enabled for the provider and universe if it is non-null.
   * Enabling server means that installation for server can be performed.
   *
   * @param provider the given provider.
   * @param universe the given universe.
   * @return true if the server is enabled.
   */
  public boolean isNodeAgentServerEnabled(Provider provider, @Nullable Universe universe) {
    boolean clientEnabled =
        confGetter.getConfForScope(provider, ProviderConfKeys.enableNodeAgentClient);
    if (!clientEnabled) {
      log.debug("Node agent server is disabled for provider {}", provider.getUuid());
      return false;
    }
    if (!isEnabled()) {
      log.debug("Node agent server is disabled for old provider {}", provider.getUuid());
      return provider.getDetails().isEnableNodeAgent();
    }
    // The internal provider flag is not checked if enabler is enabled.
    if (universe != null
        && isNodeAgentEnabled(universe, p -> !isEnabled()).orElse(false) == false) {
      return false;
    }
    return true;
  }

  /*
   * Checks if background installation for node agents is enabled for the given universe. It is
   * disabled if node agent client is currently disabled. As node agent is enabled for all new
   * providers by default, background installation is enabled unless it is explicitly disabled.
   * For old providers, its support depends on the provider type.
   *
   * 1. Cloud service providers - supported if the client runtime config is not disabled.
   * 2. Onprem fully manual providers - supported if the client runtime config is not disabled.
   * 3. Onprem non-manual providers - not supported.
   *
   * For 1 and 2, provider flag must not be checked.
   */
  private boolean isBackgroundInstallNodeAgentEnabled(Universe universe) {
    Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    if (primaryCluster.userIntent.useSystemd == false) {
      log.info(
          "Unsupported universe {} for background node-agent installation as systemd is disabled",
          universe.getUniverseUUID());
      return false;
    }
    return isNodeAgentEnabled(
            universe,
            p -> {
              if (p.getCloudCode() != CloudType.onprem || p.getDetails().isSkipProvisioning()) {
                // Do not include provider flag for cloud and fully manual onprem providers when the
                // enabler is on.
                return !isEnabled();
              }
              // Always check provider flag for onprem non-manual providers.
              return true;
            })
        .orElse(false);
  }

  // This checks if node agent is enabled for the universe with the optional parameter to include or
  // exclude the flag or field set in provider details.
  private Optional<Boolean> isNodeAgentEnabled(
      Universe universe, Predicate<Provider> includeProviderFlag) {
    Map<String, Boolean> providerEnabledMap = new HashMap<>();
    for (Cluster cluster : universe.getUniverseDetails().clusters) {
      if (cluster.userIntent == null
          || cluster.userIntent.providerType == CloudType.kubernetes
          || cluster.userIntent.provider == null) {
        // Unsupported cluster is found.
        return Optional.empty();
      }
      boolean enabled =
          providerEnabledMap.computeIfAbsent(
              cluster.userIntent.provider,
              k -> {
                Provider provider =
                    Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
                if (!confGetter.getConfForScope(provider, ProviderConfKeys.enableNodeAgentClient)) {
                  log.debug("Node agent is not enabled for provider {}", provider.getUuid());
                  return false;
                }
                if (includeProviderFlag != null
                    && includeProviderFlag.test(provider)
                    && !provider.getDetails().isEnableNodeAgent()) {
                  log.debug("Node agent is not enabled for old provider {}", provider.getUuid());
                  return false;
                }
                return true;
              });
      if (!enabled) {
        return Optional.of(false);
      }
    }
    return Optional.of(universe.getUniverseDetails().clusters.size() > 0);
  }

  /**
   * Mark universe to install node agent only if the node agent enabler is enabled.
   *
   * @param universeUuid the given universe UUID.
   */
  public void markUniverse(UUID universeUuid) {
    if (isEnabled()) {
      Universe.saveUniverseDetails(
          universeUuid,
          null /* version increment CB */,
          u -> {
            UniverseDefinitionTaskParams d = u.getUniverseDetails();
            d.installNodeAgent = true;
            u.setUniverseDetails(d);
          });
      log.debug("Marked universe {} to install node agent", universeUuid);
    }
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
              installer.future.get(3, TimeUnit.SECONDS);
              log.debug("Installation completed for universe {}", installer.getUniverseUuid());
              // Go to the next universe.
            } catch (CancellationException e) {
              log.warn("Installer cancelled for universe {}", installer.getUniverseUuid());
              installer.cancelAll();
              // Go to the next universe.
            } catch (InterruptedException e) {
              log.warn(
                  "Wait interrupted for installer for universe {}", installer.getUniverseUuid());
              installer.cancelAll();
              // Go to the next universe.
            } catch (TimeoutException e) {
              Duration timeout =
                  confGetter.getConfForScope(
                      universeOpt.get(), UniverseConfKeys.nodeAgentEnablerInstallTimeout);
              Instant expiresAt =
                  installer.getCreatedAt().plus(timeout.getSeconds(), ChronoUnit.SECONDS);
              if (expiresAt.isAfter(Instant.now())) {
                // There is still time before expiry. Go to next customer.
                continue;
              }
              log.error(
                  "Installation timed out for universe {} after {} secs",
                  installer.getUniverseUuid(),
                  timeout.getSeconds());
              // Cancel expired installer and go to next customer.
              installer.cancelAll();
              // Go to next universe.
            } catch (Exception e) {
              log.error(
                  "Installation failed for universe {} - {}",
                  installer.getUniverseUuid(),
                  e.getCause().getMessage());
              installer.cancelAll();
              // Go to next universe.
            }
          } else {
            log.info(
                "Cancelling node agent installations because universe {} is not found",
                installer.getUniverseUuid());
            // Universe does not exist anymore.
            installer.cancelAll();
            // Go to next universe.
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
                "Skipping processed universe {} for customer {} in the current interation",
                universe.getName(),
                customer.getUuid());
            continue;
          }
          if (!shouldInstallNodeAgents(universe, false /* Ignore universe lock */)) {
            log.trace(
                "Skipping installation for universe {} for customer {} as it is not eligible",
                universe.getName(),
                customer.getUuid());
            continue;
          }
          log.info(
              "Picking up universe {} ({}) for customer {} for installation",
              universe.getName(),
              universe.getUniverseUUID(),
              customer.getUuid());
          try {
            UniverseNodeAgentInstaller nextInstaller =
                new UniverseNodeAgentInstaller(customer.getUuid(), universe);
            nextInstaller.future =
                CompletableFuture.runAsync(nextInstaller, universeInstallerExecutor);
            customerNodeAgentInstallers.put(customer.getUuid(), nextInstaller);
            // Break to go to next customer.
            break;
          } catch (RejectedExecutionException e) {
            log.error(
                "Failed to submit installer for universe {} - {}",
                universe.getUniverseUUID(),
                e.getMessage());
          }
        }
        if (customerNodeAgentInstallers.get(customer.getUuid()) == installer) {
          // Same reference means no new installer was created.
          log.info("Removing the completed installer for universe {}", installer.getUniverseUuid());
          customerNodeAgentInstallers.remove(customer.getUuid());
        }
      }
    } catch (Exception e) {
      log.error("Error encountered in scanning universes to enable node agents", e);
    }
  }

  // Used only for testing.
  @VisibleForTesting
  List<UniverseNodeAgentInstaller> getUniverseNodeAgentInstallers() {
    List<UniverseNodeAgentInstaller> installers =
        new ArrayList<>(customerNodeAgentInstallers.values());
    Collections.sort(installers, Comparator.comparing(UniverseNodeAgentInstaller::getCreatedAt));
    return installers;
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
          } catch (CancellationException e) {
            log.warn("Installation got cancelled", e);
            // Ignore error.
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
   * Checks if node agents should be installed immediately on this universe.
   *
   * @param universe the universe to be checked.
   * @param ignoreUniverseLock true to ignore universe lock, otherwise the check returns false if
   *     the universe is locked.
   * @return true if node agents should be installed on the universe else false.
   */
  public boolean shouldInstallNodeAgents(Universe universe, boolean ignoreUniverseLock) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    if (!isEnabled()) {
      log.trace(
          "Skipping installation for universe {} as enabler is disabled",
          universe.getUniverseUUID());
      return false;
    }
    if (!details.installNodeAgent) {
      log.trace(
          "Skipping installation for universe {} as marker is not set", universe.getUniverseUUID());
      // No marker set to install node-agent.
      return false;
    }
    if (details.universePaused) {
      log.info("Skipping installation for universe {} as it is paused", universe.getUniverseUUID());
      // No marker set to install node-agent.
      return false;
    }
    if (!ignoreUniverseLock && details.updateInProgress) {
      log.debug(
          "Skipping installation for universe {} as another task is already running",
          universe.getUniverseUUID());
      // This only prevents starting installation but allows another task to run in parallel.
      return false;
    }
    if (universe.getNodes().stream().anyMatch(n -> n.state != NodeDetails.NodeState.Live)) {
      log.info(
          "Nodes cannot be processed for universe {} as there are non Live nodes",
          universe.getUniverseUUID());
      return false;
    }
    if (universe.getNodes().stream()
        .anyMatch(n -> n.cloudInfo == null || StringUtils.isEmpty(n.cloudInfo.private_ip))) {
      log.info(
          "Nodes cannot be processed for universe {} as there are unset private IPs",
          universe.getUniverseUUID());
      return false;
    }
    return isBackgroundInstallNodeAgentEnabled(universe);
  }

  /**
   * Cancel the installers running for nodes in the universe.
   *
   * @param universeUuid the universe UUID.
   */
  public void cancelForUniverse(UUID universeUuid) {
    if (isEnabled()) {
      Universe.maybeGet(universeUuid)
          .ifPresent(
              u -> {
                UniverseNodeAgentInstaller installer =
                    customerNodeAgentInstallers.get(Customer.get(u.getCustomerId()).getUuid());
                if (installer != null) {
                  log.info(
                      "Cancelling existing installations for universe {}", u.getUniverseUUID());
                  installer.cancelAll();
                }
              });
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
        UUID customerUuid,
        UUID universeUuid,
        NodeDetails nodeDetails,
        NodeAgent nodeAgent,
        Duration cooldown)
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
                    if (!nodeAgentOpt.get().isActive()) {
                      Duration cooldown =
                          confGetter.getConfForScope(
                              universe, UniverseConfKeys.nodeAgentEnablerReinstallCooldown);
                      return nodeAgentInstaller.reinstall(
                          getCustomerUuid(), getUniverseUuid(), node, nodeAgentOpt.get(), cooldown);
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
    // care. This method gets blocked till the installation is complete.
    private boolean processNodes(Universe universe, Function<NodeDetails, Boolean> callback) {
      if (!shouldInstallNodeAgents(universe, false /* Ignore universe lock */)) {
        log.trace(
            "Skipping installation for universe {} as it is not eligible",
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
            // Synchronize to protect race against cancellation (cancelAll).
            synchronized (this) {
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
            }
          });

      try {
        latch.await();
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
        // Clear on normal exit.
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

    private synchronized void cancelAll() {
      log.info(
          "Cancelling installation for universe {} and customer {}",
          getUniverseUuid(),
          getCustomerUuid());
      futures.entrySet().stream()
          .filter(entry -> !entry.getValue().isCancelled())
          .forEach(
              entry -> {
                try {
                  entry.getValue().cancel(true);
                } catch (Exception e) {
                  log.error(
                      "Error occurred while cancelling installation on node IP {} for universe {} -"
                          + " {}",
                      entry.getKey(),
                      getUniverseUuid(),
                      e.getMessage());
                }
              });
      futures.clear();
      if (!future.isCancelled()) {
        future.cancel(true);
      }
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
