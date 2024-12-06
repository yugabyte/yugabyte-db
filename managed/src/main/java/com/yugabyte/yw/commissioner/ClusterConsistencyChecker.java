// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.commissioner.tasks.subtasks.CheckClusterConsistency;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import java.util.Collections;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

@Slf4j
public class ClusterConsistencyChecker {
  private Map<UUID, CheckClusterConsistency.CheckResult> checkResults = new ConcurrentHashMap<>();

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
    Universe.getAllWithoutResources().stream()
        .filter(
            u -> u.getUniverseDetails() != null && !u.getUniverseDetails().isUniverseBusyByTask())
        .forEach(
            univ -> {
              universeCheckExecutor.submit(
                  () -> {
                    Customer c = Customer.get(univ.getCustomerId());
                    boolean cloudEnabled =
                        confGetter.getConfForScope(c, CustomerConfKeys.cloudEnabled);
                    checkResults.put(
                        univ.getUniverseUUID(), checkUniverseConsistency(univ, cloudEnabled));
                  });
            });
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
