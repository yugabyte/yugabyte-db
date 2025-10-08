package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class OperatorResourceMigrateHandler {

  private final RuntimeConfGetter confGetter;

  @Inject
  public OperatorResourceMigrateHandler(RuntimeConfGetter confGetter) {
    this.confGetter = confGetter;
  }

  public void precheckUniverseImport(Universe universe) {
    // validate the universe is kubernetes
    if (!Util.isKubernetesBasedUniverse(universe)) {
      log.error(
          "Universe {} is not a Kubernetes universe, cannot migrate to operator",
          universe.getName());
      throw new PlatformServiceException(BAD_REQUEST, "Universe is not a Kubernetes universe");
    }
    if (!confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)) {
      log.error("Operator is not enabled, cannot migrate universe {}", universe.getName());
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Operator is not enabled. Please enable the runtime config"
              + " 'yb.kubernetes.operator.enabled' and restart YBA");
    }

    // Read Replicas clusters are not supported
    if (universe.getUniverseDetails().clusters.size() > 1) {
      log.error(
          "Universe {} has read-only clusters, cannot migrate to operator", universe.getName());
      throw new PlatformServiceException(BAD_REQUEST, "Universe has read-only clusters");
    }

    // XCluster is not supported by operator
    if (!XClusterConfig.getByUniverseUuid(universe.getUniverseUUID()).isEmpty()) {
      log.error("Universe {} has xClusterInfo set, cannot migrate to operator", universe.getName());
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot migrate universes in an xcluster setup.");
    }

    // AZ Level overrides are not supported by operator
    Map<String, String> azOverrides =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.azOverrides;
    if (azOverrides != null && azOverrides.size() > 0) {
      log.error(
          "Universe {} has AZ level overrides set, cannot migrate to operator", universe.getName());
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot migrate universes with AZ level overrides.");
    }
  }

  public UUID migrateUniverseToOperator(Universe universe) {
    // TODO: Implement the migration logic
    return UUID.randomUUID();
  }
}
