// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.commissioner.tasks.subtasks.CheckClusterConsistency;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import java.util.Collections;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

@Slf4j
public class ClusterConsistencyChecker {
  private final Map<UUID, Future<?>> runningChecks = new ConcurrentHashMap<>();
  private final Map<UUID, CheckClusterConsistency.CheckResult> checkResults =
      new ConcurrentHashMap<>();

  private final ExecutorService universeCheckExecutor;
  private final RuntimeConfGetter confGetter;
  private final YBClientService ybClientService;

  public ClusterConsistencyChecker(
      ExecutorService universeCheckExecutor,
      RuntimeConfGetter confGetter,
      YBClientService ybClientService) {
    this.universeCheckExecutor = universeCheckExecutor;
    this.confGetter = confGetter;
    this.ybClientService = ybClientService;
  }

  public void processAll() {
    if (HighAvailabilityConfig.isFollower()) {
      log.debug("Skipping checking universes consistency for follower platform");
      return;
    }
    try {
      Set<UUID> universeUUIDS = new HashSet<>();
      Universe.getAllWithoutResources().stream()
          .peek(u -> universeUUIDS.add(u.getUniverseUUID()))
          .filter(
              u -> u.getUniverseDetails() != null && !u.getUniverseDetails().isUniverseBusyByTask())
          .forEach(
              univ -> {
                if (!confGetter.getConfForScope(
                    univ, UniverseConfKeys.unexpectedServersCheckEnabled)) {
                  log.debug(
                      "Skipping unexpected servers check for universe {}", univ.getUniverseUUID());
                  runningChecks.remove(univ.getUniverseUUID());
                  return;
                }
                Future<?> curFuture = runningChecks.get(univ.getUniverseUUID());
                if (curFuture != null) {
                  log.debug(
                      "Current check is in progress for {}, skipping", univ.getUniverseUUID());
                  return;
                }
                try {
                  runningChecks.put(
                      univ.getUniverseUUID(),
                      universeCheckExecutor.submit(
                          () -> {
                            try {
                              Customer c = Customer.get(univ.getCustomerId());
                              boolean cloudEnabled =
                                  confGetter.getConfForScope(c, CustomerConfKeys.cloudEnabled);
                              checkResults.put(
                                  univ.getUniverseUUID(),
                                  checkUniverseConsistency(univ, cloudEnabled));
                            } finally {
                              runningChecks.remove(univ.getUniverseUUID());
                            }
                          }));
                } catch (RuntimeException e) {
                  log.error("Failed to submit check for universe {}", univ.getUniverseUUID(), e);
                }
              });
      // Clearing results for absent universes.
      checkResults.keySet().retainAll(universeUUIDS);
    } catch (Exception e) {
      log.error("Failed to process checks", e);
    }
  }

  public CheckClusterConsistency.CheckResult getUniverseCheckResult(UUID uuid) {
    return checkResults.get(uuid);
  }

  private CheckClusterConsistency.CheckResult checkUniverseConsistency(
      Universe universe, boolean cloudEnabled) {
    String masterHostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybClientService.getClient(masterHostPorts, certificate)) {
      return CheckClusterConsistency.checkCurrentServers(
          client, universe, Collections.emptySet(), false, cloudEnabled);
    } catch (Exception e) {
      log.error("Failed to check cluster consistency", e);
      return new CheckClusterConsistency.CheckResult();
    }
  }
}
