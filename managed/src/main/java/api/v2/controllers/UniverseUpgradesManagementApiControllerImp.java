package api.v2.controllers;

import api.v2.models.UpgradeUniverseGFlags;
import api.v2.models.YBPTask;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.gflags.SpecificGFlags.PerProcessFlags;
import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.KubernetesGFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;
import play.mvc.Http.Status;

@Slf4j
public class UniverseUpgradesManagementApiControllerImp
    extends UniverseUpgradesManagementApiControllerImpInterface {
  @Inject private UpgradeUniverseHandler upgradeUniverseHandler;

  private Cluster getClusterByUuid(String clusterUuid, GFlagsUpgradeParams v1Params) {
    Optional<Cluster> cls =
        v1Params.clusters.stream()
            .filter(c -> c.uuid.equals(UUID.fromString(clusterUuid)))
            .findAny();
    return cls.orElse(null);
  }

  @Override
  public YBPTask upgradeGFlags(
      Http.Request request, UUID cUUID, UUID uniUUID, UpgradeUniverseGFlags upgradeGFlags)
      throws Exception {
    log.info("Starting {} upgrade GFlags with {}", upgradeGFlags.getUpgradeOption(), upgradeGFlags);

    // get universe from db
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);
    // construct a GFlagsUpgradeParams that includes above universe details
    GFlagsUpgradeParams v1Params =
        Util.isKubernetesBasedUniverse(universe)
            ? convert(universe.getUniverseDetails(), KubernetesGFlagsUpgradeParams.class, null)
            : convert(universe.getUniverseDetails(), GFlagsUpgradeParams.class, null);
    // fill in SpecificGFlags from universeGFlags params into v1Params
    if (upgradeGFlags.getUniverseGflags() != null) {
      upgradeGFlags
          .getUniverseGflags()
          .forEach(
              (clusterUuid, clusterGFlags) -> {
                Cluster cluster = getClusterByUuid(clusterUuid, v1Params);
                if (cluster == null) {
                  throw new PlatformServiceException(
                      Status.NOT_FOUND, "Cluster ID not found " + clusterUuid);
                }
                cluster.userIntent.specificGFlags =
                    SpecificGFlags.construct(clusterGFlags.getMaster(), clusterGFlags.getTserver());
                if (clusterGFlags.getAzGflags() != null) {
                  Map<UUID, PerProcessFlags> perAZ = new HashMap<>();
                  clusterGFlags
                      .getAzGflags()
                      .forEach(
                          (azuuid, azGFlags) -> {
                            PerProcessFlags perProcessFlags = new PerProcessFlags();
                            perProcessFlags.value =
                                ImmutableMap.of(
                                    ServerType.MASTER,
                                    azGFlags.getMaster(),
                                    ServerType.TSERVER,
                                    azGFlags.getTserver());
                            perAZ.put(UUID.fromString(azuuid), perProcessFlags);
                          });
                  cluster.userIntent.specificGFlags.setPerAZ(perAZ);
                }
              });
    }
    // fill in upgrade option
    if (upgradeGFlags.getUpgradeOption() == null) {
      v1Params.upgradeOption = UpgradeOption.ROLLING_UPGRADE;
    } else {
      switch (upgradeGFlags.getUpgradeOption()) {
        case ROLLING:
          v1Params.upgradeOption = UpgradeOption.ROLLING_UPGRADE;
          break;
        case NON_RESTART:
          v1Params.upgradeOption = UpgradeOption.NON_RESTART_UPGRADE;
          break;
        case NON_ROLLING:
          v1Params.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;
          break;
        default:
          v1Params.upgradeOption = UpgradeOption.ROLLING_UPGRADE;
          break;
      }
    }
    // fill in other upgrade properties
    if (upgradeGFlags.getSleepAfterMasterRestartMillis() != null) {
      v1Params.sleepAfterMasterRestartMillis = upgradeGFlags.getSleepAfterMasterRestartMillis();
    }
    if (upgradeGFlags.getSleepAfterTserverRestartMillis() != null) {
      v1Params.sleepAfterTServerRestartMillis = upgradeGFlags.getSleepAfterTserverRestartMillis();
    }
    if (upgradeGFlags.getKubernetesResourceDetails() != null) {
      KubernetesResourceDetails krd = new KubernetesResourceDetails();
      krd.name = upgradeGFlags.getKubernetesResourceDetails().getName();
      krd.namespace = upgradeGFlags.getKubernetesResourceDetails().getNamespace();
      krd.resourceType = upgradeGFlags.getKubernetesResourceDetails().getResourceType().name();
      v1Params.setKubernetesResourceDetails(krd);
    }
    // invoke v1 upgrade api UpgradeUniverseHandler.upgradeGFlags
    UUID taskUuid = upgradeUniverseHandler.upgradeGFlags(v1Params, customer, universe);
    // construct a v2 Task to return from here
    YBPTask ybpTask = new YBPTask().taskUuid(taskUuid).resourceUuid(universe.getUniverseUUID());

    log.info("Started gflags upgrade task {}", mapper.writeValueAsString(ybpTask));
    return ybpTask;
  }
}
