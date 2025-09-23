// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.common.utils.CapacityReservationUtil.CAPACITY_RESERVATION_KEY;

import com.google.inject.Inject;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.cloud.azu.AZUClientFactory;
import com.yugabyte.yw.cloud.azu.AZUResourceGroupApiClient;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.utils.CapacityReservationUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class DoCapacityReservation extends ServerSubTaskBase {
  public static final String GROUP_SUFFIX = "_reservationGroup";
  public static Pattern ZONE_NUMBER_PATTERN = Pattern.compile("-([0-9]+)$");

  private AZUClientFactory azuClientFactory;
  private CloudAPI.Factory cloudAPIFactory;

  @Inject
  protected DoCapacityReservation(
      BaseTaskDependencies baseTaskDependencies,
      AZUClientFactory azuClientFactory,
      CloudAPI.Factory cloudAPIFactory) {
    super(baseTaskDependencies);
    this.azuClientFactory = azuClientFactory;
    this.cloudAPIFactory = cloudAPIFactory;
  }

  public static class Params extends ServerSubTaskParams {
    public UUID providerUUID;
    public Map<String, String> nodeToInstanceType;
    public List<NodeDetails> nodes;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    if (taskParams().nodes == null || taskParams().nodes.isEmpty()) {
      return;
    }
    Provider provider = Provider.getOrBadRequest(taskParams().providerUUID);

    UniverseDefinitionTaskParams.CapacityReservationState capacityReservationState =
        getOrCreateCapacityReservationState(provider.getCloudCode());

    try {
      if (provider.getCloudCode() == Common.CloudType.azu) {
        AZUResourceGroupApiClient apiClient = azuClientFactory.getClient(provider);
        UniverseDefinitionTaskParams.AzureReservationInfo azureReservationInfo =
            capacityReservationState.getAzureReservationInfo();
        Map<UUID, AvailabilityZone> zones = new HashMap<>();
        for (NodeDetails node : taskParams().nodes) {
          String instanceType = taskParams().nodeToInstanceType.get(node.nodeName);
          if (!CapacityReservationUtil.azureCheckInstanceTypeIsSupported(instanceType)) {
            continue;
          }
          AvailabilityZone zone =
              zones.computeIfAbsent(
                  node.azUuid, uuid -> AvailabilityZone.getOrBadRequest(node.azUuid));

          UniverseDefinitionTaskParams.AzureRegionReservation regionReservation =
              azureReservationInfo
                  .getReservationsByRegionMap()
                  .computeIfAbsent(
                      zone.getRegion().getCode(),
                      code -> {
                        UniverseDefinitionTaskParams.AzureRegionReservation r =
                            new UniverseDefinitionTaskParams.AzureRegionReservation();
                        r.setRegion(zone.getRegion().getCode());
                        r.setGroupName(
                            getCapacityReservationGroupName(
                                taskParams().getUniverseUUID(), r.getRegion()));
                        return r;
                      });

          regionReservation.getZones().add(extractZoneNumber(zone.getCode()).toString());

          UniverseDefinitionTaskParams.PerInstanceTypeReservation perType =
              regionReservation.getReservationsByType().get(instanceType);
          if (perType == null) {
            perType = new UniverseDefinitionTaskParams.PerInstanceTypeReservation();
            regionReservation.getReservationsByType().put(instanceType, perType);
          }
          String zoneID = extractZoneNumber(zone.getCode()).toString();
          UniverseDefinitionTaskParams.ZonedReservation zonedReservation =
              perType
                  .getZonedReservation()
                  .computeIfAbsent(
                      zoneID, x -> new UniverseDefinitionTaskParams.ZonedReservation());
          zonedReservation.setZone(zoneID);
          zonedReservation.getVmNames().add(node.nodeName);
        }
        for (UniverseDefinitionTaskParams.AzureRegionReservation regionReservation :
            azureReservationInfo.getReservationsByRegionMap().values()) {

          String groupId =
              apiClient.createCapacityReservationGroup(
                  regionReservation.getGroupName(),
                  regionReservation.getRegion(),
                  regionReservation.getZones());
          log.info("Created group {}", groupId);

          for (Map.Entry<String, UniverseDefinitionTaskParams.PerInstanceTypeReservation> entry :
              regionReservation.getReservationsByType().entrySet()) {
            String instanceType = entry.getKey();
            entry
                .getValue()
                .getZonedReservation()
                .forEach(
                    (zoneID, reservation) -> {
                      log.debug(
                          "Processing {} zone {} vms {}",
                          instanceType,
                          reservation.getZone(),
                          reservation.getVmNames());
                      Integer count = reservation.getVmNames().size();
                      String capacityReservation =
                          apiClient.createCapacityReservation(
                              regionReservation.getGroupName(),
                              regionReservation.getRegion(),
                              zoneID,
                              getInstanceReservationName(instanceType, zoneID),
                              instanceType,
                              count,
                              Map.of(
                                  "universe-name",
                                  universe.getName(),
                                  "universe-uuid",
                                  universe.getUniverseUUID().toString()));
                      log.info("Created reservation {} for {}", capacityReservation, instanceType);
                      reservation.setReservationName(capacityReservation);
                    });
          }
        }
      } else if (provider.getCloudCode() == Common.CloudType.aws) {
        CloudAPI cloudAPI = cloudAPIFactory.get(provider.getCloudCode().name());
        UniverseDefinitionTaskParams.AwsReservationInfo awsReservationInfo =
            capacityReservationState.getAwsReservationInfo();
        Map<UUID, AvailabilityZone> zones = new HashMap<>();
        for (NodeDetails node : taskParams().nodes) {
          AvailabilityZone zone =
              zones.computeIfAbsent(
                  node.azUuid, uuid -> AvailabilityZone.getOrBadRequest(node.azUuid));

          String instanceType = taskParams().nodeToInstanceType.get(node.nodeName);
          UniverseDefinitionTaskParams.AwsZoneReservation zoneReservation =
              awsReservationInfo
                  .getReservationsByZoneMap()
                  .computeIfAbsent(
                      zone.getCode(),
                      code -> {
                        UniverseDefinitionTaskParams.AwsZoneReservation r =
                            new UniverseDefinitionTaskParams.AwsZoneReservation();
                        r.setRegion(zone.getRegion().getCode());
                        r.setZone(code);
                        r.setReservationName(
                            getZoneInstanceCapacityReservationName(
                                taskParams().getUniverseUUID(), code, instanceType));
                        return r;
                      });
          UniverseDefinitionTaskParams.PerInstanceTypeReservation perType =
              zoneReservation.getReservationsByType().get(instanceType);
          if (perType == null) {
            perType = new UniverseDefinitionTaskParams.PerInstanceTypeReservation();
            zoneReservation.getReservationsByType().put(instanceType, perType);
          }
          String zoneCode = zone.getCode();
          UniverseDefinitionTaskParams.ZonedReservation zonedReservation =
              perType
                  .getZonedReservation()
                  .computeIfAbsent(
                      zoneCode, x -> new UniverseDefinitionTaskParams.ZonedReservation());
          zonedReservation.setZone(zoneCode);
          zonedReservation.getVmNames().add(node.nodeName);
        }

        for (UniverseDefinitionTaskParams.AwsZoneReservation zoneReservation :
            awsReservationInfo.getReservationsByZoneMap().values()) {

          for (Map.Entry<String, UniverseDefinitionTaskParams.PerInstanceTypeReservation> entry :
              zoneReservation.getReservationsByType().entrySet()) {
            String instanceType = entry.getKey();
            entry
                .getValue()
                .getZonedReservation()
                .forEach(
                    (zoneCode, reservation) -> {
                      log.debug(
                          "Processing {} zone {} vms {}",
                          instanceType,
                          reservation.getZone(),
                          reservation.getVmNames());
                      Integer count = reservation.getVmNames().size();
                      String capacityReservation =
                          cloudAPI.createCapacityReservation(
                              provider,
                              getZoneInstanceCapacityReservationName(
                                  taskParams().getUniverseUUID(), zoneCode, instanceType),
                              zoneReservation.getRegion(),
                              reservation.getZone(),
                              instanceType,
                              count);
                      log.info("Created reservation {} for {}", capacityReservation, instanceType);
                      reservation.setReservationName(capacityReservation);
                    });
          }
        }
      } else {
        throw new UnsupportedOperationException("Not supported for " + provider.getCloudCode());
      }
    } finally {
      Universe.saveDetails(
          taskParams().getUniverseUUID(),
          u -> {
            u.getUniverseDetails().setCapacityReservationState(capacityReservationState);
          });
      getTaskCache().put(CAPACITY_RESERVATION_KEY, Json.toJson(capacityReservationState));
    }
  }

  public static Integer extractZoneNumber(String zone) {
    Matcher matcher = ZONE_NUMBER_PATTERN.matcher(zone);
    if (matcher.find()) {
      return Integer.parseInt(matcher.group(1));
    }
    return -1;
  }

  private UniverseDefinitionTaskParams.CapacityReservationState getOrCreateCapacityReservationState(
      Common.CloudType cloudType) {
    UniverseDefinitionTaskParams.CapacityReservationState capacityReservationState = null;
    if (!isFirstTry()) {
      capacityReservationState =
          Universe.getOrBadRequest(taskParams().getUniverseUUID())
              .getUniverseDetails()
              .getCapacityReservationState();
    }
    if (capacityReservationState == null) {
      capacityReservationState = new UniverseDefinitionTaskParams.CapacityReservationState();
    }
    CapacityReservationUtil.initReservationForProviderType(capacityReservationState, cloudType);
    return capacityReservationState;
  }

  public static String getCapacityReservationGroupName(UUID universeUUID, String region) {
    return universeUUID.toString() + "_" + region + GROUP_SUFFIX;
  }

  public static String getInstanceReservationName(String instanceType, String zoneID) {
    return instanceType + "_in_" + zoneID;
  }

  public static String getZoneInstanceCapacityReservationName(
      UUID universeUUID, String zone, String instanceType) {
    return instanceType + "-" + zone + "-" + universeUUID.toString();
  }
}
