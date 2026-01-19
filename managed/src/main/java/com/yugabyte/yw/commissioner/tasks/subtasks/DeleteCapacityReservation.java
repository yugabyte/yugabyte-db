// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.cloud.azu.AZUClientFactory;
import com.yugabyte.yw.cloud.azu.AZUResourceGroupApiClient;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.utils.CapacityReservationUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.metrics.CapacityReservationMetrics;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class DeleteCapacityReservation extends ServerSubTaskBase {
  private AZUClientFactory azuClientFactory;
  private CloudAPI.Factory cloudAPIFactory;
  private CapacityReservationMetrics reservationMetrics;

  @Inject
  protected DeleteCapacityReservation(
      BaseTaskDependencies baseTaskDependencies,
      AZUClientFactory azuClientFactory,
      CloudAPI.Factory cloudAPIFactory,
      CapacityReservationMetrics reservationMetrics) {
    super(baseTaskDependencies);
    this.azuClientFactory = azuClientFactory;
    this.cloudAPIFactory = cloudAPIFactory;
    this.reservationMetrics = reservationMetrics;
  }

  public static class Params extends ServerSubTaskParams {}

  @Override
  protected DeleteCapacityReservation.Params taskParams() {
    return (DeleteCapacityReservation.Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    UniverseDefinitionTaskParams.CapacityReservationState capacityReservationState =
        universe.getUniverseDetails().getCapacityReservationState();
    if (capacityReservationState == null) {
      log.warn("No capacity reservation found");
      return;
    }
    Set<UUID> providers =
        universe.getUniverseDetails().clusters.stream()
            .map(c -> UUID.fromString(c.userIntent.provider))
            .collect(Collectors.toSet());
    boolean succeeded = false;
    try {
      for (UUID providerUUID : providers) {
        Provider provider = Provider.getOrBadRequest(providerUUID);
        UniverseDefinitionTaskParams.ReservationInfo reservationForProviderType =
            CapacityReservationUtil.getReservationForProvider(capacityReservationState, provider);
        if (reservationForProviderType == null) {
          log.debug("No reservation for cloud {}", provider.getCloudCode());
          continue;
        }
        log.debug("Got reservation {} from db", Json.toJson(reservationForProviderType));
        if (reservationForProviderType
            instanceof UniverseDefinitionTaskParams.AzureReservationInfo azureReservationInfo) {
          AZUResourceGroupApiClient apiClient = azuClientFactory.getClient(provider);
          Set<String> groups = apiClient.listCapacityReservationGroups();
          for (UniverseDefinitionTaskParams.AzureRegionReservation regionReservation :
              new ArrayList<>(azureReservationInfo.getReservationsByRegionMap().values())) {
            if (groups.contains(regionReservation.getGroupName())) {
              Set<String> reservations =
                  apiClient.listCapacityReservations(regionReservation.getGroupName());
              regionReservation
                  .getReservationsByType()
                  .forEach(
                      (instanceType, perInstanceType) -> {
                        perInstanceType
                            .getZonedReservation()
                            .forEach(
                                (zoneId, reservation) -> {
                                  if (reservations.remove(reservation.getReservationName())) {
                                    log.debug(
                                        "Deleting reservation {} with {} vms",
                                        reservation.getReservationName(),
                                        reservation.getVmNames());
                                    reservationMetrics.wrapWithMetrics(
                                        universe.getUniverseUUID(),
                                        reservation.getVmNames().size(),
                                        Common.CloudType.azu,
                                        CapacityReservationUtil.ReservationAction.RELEASE,
                                        () -> {
                                          apiClient.deleteCapacityReservation(
                                              regionReservation.getGroupName(),
                                              reservation.getReservationName(),
                                              reservation.getVmNames());
                                          return null;
                                        });
                                  } else {
                                    log.debug(
                                        "Reservation {} is not found",
                                        reservation.getReservationName());
                                  }
                                });
                      });
              // This should not happen but just for the sake of safety.
              reservations.forEach(
                  r ->
                      apiClient.deleteCapacityReservation(
                          regionReservation.getGroupName(), r, Collections.emptySet()));
              log.debug("Deleting region reservation {}", regionReservation.getGroupName());
              reservationMetrics.wrapWithMetrics(
                  universe.getUniverseUUID(),
                  1,
                  Common.CloudType.azu,
                  CapacityReservationUtil.ReservationAction.DELETE_GROUP,
                  () -> {
                    apiClient.deleteCapacityReservationGroup(regionReservation.getGroupName());
                    return null;
                  });
            } else {
              log.debug("Group not found: {}", regionReservation.getGroupName());
            }
            azureReservationInfo.getReservationsByRegionMap().remove(regionReservation.getRegion());
          }
          capacityReservationState.getAzureReservationInfos().remove(providerUUID);
        } else if (reservationForProviderType
            instanceof UniverseDefinitionTaskParams.AwsReservationInfo awsReservationInfo) {
          CloudAPI cloudAPI = cloudAPIFactory.get(provider.getCloudCode().name());
          for (UniverseDefinitionTaskParams.AwsZoneReservation zoneReservation :
              new ArrayList<>(awsReservationInfo.getReservationsByZoneMap().values())) {
            zoneReservation
                .getReservationsByType()
                .forEach(
                    (instanceType, perInstanceType) -> {
                      perInstanceType
                          .getZonedReservation()
                          .forEach(
                              (zoneId, reservation) -> {
                                if (zoneReservation.getReservationName() != null) {
                                  log.debug(
                                      "Deleting reservation {} with {} vms",
                                      reservation.getReservationName(),
                                      reservation.getVmNames());
                                  reservationMetrics.wrapWithMetrics(
                                      universe.getUniverseUUID(),
                                      reservation.getVmNames().size(),
                                      Common.CloudType.aws,
                                      CapacityReservationUtil.ReservationAction.RELEASE,
                                      () -> {
                                        cloudAPI.deleteCapacityReservation(
                                            provider,
                                            zoneReservation.getRegion(),
                                            reservation.getReservationName());
                                        return null;
                                      });
                                }
                              });
                    });
            awsReservationInfo.getReservationsByZoneMap().remove(zoneReservation.getZone());
          }
          capacityReservationState.getAwsReservationInfos().remove(providerUUID);
        }
      }
      succeeded = true;
    } finally {
      getTaskCache().delete(CapacityReservationUtil.CAPACITY_RESERVATION_KEY);
      UniverseDefinitionTaskParams.CapacityReservationState toSave =
          succeeded ? null : capacityReservationState;
      Universe.saveDetails(
          taskParams().getUniverseUUID(),
          u -> u.getUniverseDetails().setCapacityReservationState(toSave));
    }
  }
}
