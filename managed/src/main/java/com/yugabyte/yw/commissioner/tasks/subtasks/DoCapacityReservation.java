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
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.utils.CapacityReservationUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.metrics.CapacityReservationMetrics;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class DoCapacityReservation extends ServerSubTaskBase {
  public static final String GROUP_SUFFIX = "_RnG";
  public static Pattern ZONE_NUMBER_PATTERN = Pattern.compile("-([0-9]+)$");

  private AZUClientFactory azuClientFactory;
  private CloudAPI.Factory cloudAPIFactory;
  private CapacityReservationMetrics reservationMetrics;

  @Inject
  protected DoCapacityReservation(
      BaseTaskDependencies baseTaskDependencies,
      AZUClientFactory azuClientFactory,
      CloudAPI.Factory cloudAPIFactory,
      CapacityReservationMetrics reservationMetrics) {
    super(baseTaskDependencies);
    this.azuClientFactory = azuClientFactory;
    this.cloudAPIFactory = cloudAPIFactory;
    this.reservationMetrics = reservationMetrics;
  }

  public static class Params extends ServerSubTaskParams {
    public UUID providerUUID;
    public Map<String, String> nodeToInstanceType;
    public List<NodeDetails> nodes;
  }

  public static class CapacityReservationException extends RuntimeException {
    public CapacityReservationException(String message, RuntimeException e) {
      super(message, e);
    }

    public CapacityReservationException(RuntimeException e) {
      super(e);
    }
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
    int retries = confGetter.getGlobalConf(GlobalConfKeys.capacityReservationMaxRetries);
    int sleepBetweenRetriesSec =
        confGetter.getGlobalConf(GlobalConfKeys.capacityReservationRetrySeconds);
    long sleepMs = TimeUnit.SECONDS.toMillis(sleepBetweenRetriesSec);

    UniverseDefinitionTaskParams.CapacityReservationState capacityReservationState =
        getOrCreateCapacityReservationState();
    CapacityReservationUtil.initReservationForProvider(capacityReservationState, provider);
    UniverseDefinitionTaskParams.ClusterType clusterType =
        CommonUtils.getClusterType(provider, universe);
    try {
      if (provider.getCloudCode() == Common.CloudType.azu) {
        UniverseDefinitionTaskParams.AzureReservationInfo azureReservationInfo =
            fillAzureReservationInfo(capacityReservationState, provider, clusterType);
        doAzureReservation(universe, azureReservationInfo, provider, retries, sleepMs);
      } else if (provider.getCloudCode() == Common.CloudType.aws) {
        UniverseDefinitionTaskParams.AwsReservationInfo awsReservationInfo =
            fillAwsReservationInfo(capacityReservationState, provider, clusterType);
        doAwsReservation(universe, awsReservationInfo, provider, retries, sleepMs);
      } else {
        throw new UnsupportedOperationException("Not supported for " + provider.getCloudCode());
      }
    } catch (RuntimeException e) {
      throw new CapacityReservationException(
          "Failed to create capacity reservation: " + e.getMessage(), e);
    } finally {
      Universe.saveDetails(
          taskParams().getUniverseUUID(),
          u -> {
            u.getUniverseDetails().setCapacityReservationState(capacityReservationState);
          });
      getTaskCache().put(CAPACITY_RESERVATION_KEY, Json.toJson(capacityReservationState));
    }
  }

  private UniverseDefinitionTaskParams.AzureReservationInfo fillAzureReservationInfo(
      UniverseDefinitionTaskParams.CapacityReservationState capacityReservationState,
      Provider provider,
      UniverseDefinitionTaskParams.ClusterType clusterType) {
    UniverseDefinitionTaskParams.AzureReservationInfo azureReservationInfo =
        CapacityReservationUtil.getReservationForProvider(capacityReservationState, provider);
    Map<UUID, AvailabilityZone> zones = new HashMap<>();
    for (NodeDetails node : taskParams().nodes) {
      String instanceType = taskParams().nodeToInstanceType.get(node.nodeName);
      if (!CapacityReservationUtil.azureCheckInstanceTypeIsSupported(instanceType)) {
        continue;
      }
      AvailabilityZone zone =
          zones.computeIfAbsent(node.azUuid, uuid -> AvailabilityZone.getOrBadRequest(node.azUuid));

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
                            taskParams().getUniverseUUID(), clusterType, r.getRegion()));
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
              .computeIfAbsent(zoneID, x -> new UniverseDefinitionTaskParams.ZonedReservation());
      zonedReservation.setZone(zoneID);
      zonedReservation.getVmNames().add(node.nodeName);
    }
    return azureReservationInfo;
  }

  private void doAzureReservation(
      Universe universe,
      UniverseDefinitionTaskParams.AzureReservationInfo azureReservationInfo,
      Provider provider,
      int retries,
      long sleepBetweenRetriesMs) {
    AZUResourceGroupApiClient apiClient = azuClientFactory.getClient(provider);
    Set<String> createdGroups = new HashSet<>();
    Set<String> processedReservations = new HashSet<>();

    doWithConstTimeout(
        sleepBetweenRetriesMs,
        sleepBetweenRetriesMs * retries,
        () -> {
          for (UniverseDefinitionTaskParams.AzureRegionReservation regionReservation :
              azureReservationInfo.getReservationsByRegionMap().values()) {

            if (!createdGroups.contains(regionReservation.getGroupName())) {
              String groupId =
                  reservationMetrics.wrapWithMetrics(
                      universe.getUniverseUUID(),
                      1,
                      Common.CloudType.azu,
                      CapacityReservationUtil.ReservationAction.CREATE_GROUP,
                      () ->
                          apiClient.createCapacityReservationGroup(
                              regionReservation.getGroupName(),
                              regionReservation.getRegion(),
                              regionReservation.getZones(),
                              Map.of(
                                  "universe-name",
                                  universe.getName(),
                                  "universe-uuid",
                                  universe.getUniverseUUID().toString())));
              log.info("Created group {}", groupId);
              createdGroups.add(regionReservation.getGroupName());
            }

            for (Map.Entry<String, UniverseDefinitionTaskParams.PerInstanceTypeReservation> entry :
                regionReservation.getReservationsByType().entrySet()) {
              String instanceType = entry.getKey();
              entry
                  .getValue()
                  .getZonedReservation()
                  .forEach(
                      (zoneID, reservation) -> {
                        String instanceReservationName =
                            getInstanceReservationName(instanceType, zoneID);
                        String uniqueName =
                            regionReservation.getRegion() + "%" + instanceReservationName;
                        if (processedReservations.contains(uniqueName)) {
                          return;
                        }
                        log.debug(
                            "Processing {} zone {} vms {}",
                            instanceType,
                            reservation.getZone(),
                            reservation.getVmNames());
                        Integer count = reservation.getVmNames().size();
                        String capacityReservation =
                            reservationMetrics.wrapWithMetrics(
                                universe.getUniverseUUID(),
                                count,
                                Common.CloudType.azu,
                                CapacityReservationUtil.ReservationAction.RESERVE,
                                () ->
                                    apiClient.createCapacityReservation(
                                        regionReservation.getGroupName(),
                                        regionReservation.getRegion(),
                                        zoneID,
                                        instanceReservationName,
                                        instanceType,
                                        count,
                                        Map.of(
                                            "universe-name",
                                            universe.getName(),
                                            "universe-uuid",
                                            universe.getUniverseUUID().toString())));
                        log.info(
                            "Created reservation {} for {}", capacityReservation, instanceType);
                        reservation.setReservationName(capacityReservation);
                        processedReservations.add(uniqueName);
                      });
            }
          }
        });
  }

  private UniverseDefinitionTaskParams.AwsReservationInfo fillAwsReservationInfo(
      UniverseDefinitionTaskParams.CapacityReservationState capacityReservationState,
      Provider provider,
      UniverseDefinitionTaskParams.ClusterType clusterType) {
    UniverseDefinitionTaskParams.AwsReservationInfo awsReservationInfo =
        CapacityReservationUtil.getReservationForProvider(capacityReservationState, provider);
    Map<UUID, AvailabilityZone> zones = new HashMap<>();
    for (NodeDetails node : taskParams().nodes) {
      AvailabilityZone zone =
          zones.computeIfAbsent(node.azUuid, uuid -> AvailabilityZone.getOrBadRequest(node.azUuid));

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
                            taskParams().getUniverseUUID(), clusterType, code, instanceType));
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
              .computeIfAbsent(zoneCode, x -> new UniverseDefinitionTaskParams.ZonedReservation());
      zonedReservation.setZone(zoneCode);
      zonedReservation.getVmNames().add(node.nodeName);
    }
    return awsReservationInfo;
  }

  private void doAwsReservation(
      Universe universe,
      UniverseDefinitionTaskParams.AwsReservationInfo awsReservationInfo,
      Provider provider,
      int retries,
      long sleepBetweenRetriesMs) {
    CloudAPI cloudAPI = cloudAPIFactory.get(provider.getCloudCode().name());
    Set<String> processedReservations = new HashSet<>();
    UniverseDefinitionTaskParams.ClusterType clusterType =
        CommonUtils.getClusterType(provider, universe);

    doWithConstTimeout(
        sleepBetweenRetriesMs,
        sleepBetweenRetriesMs * retries,
        () -> {
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
                        String reservationName =
                            getZoneInstanceCapacityReservationName(
                                universe.getUniverseUUID(), clusterType, zoneCode, instanceType);
                        if (processedReservations.contains(reservationName)) {
                          return;
                        }
                        log.debug(
                            "Processing {} zone {} vms {}",
                            instanceType,
                            reservation.getZone(),
                            reservation.getVmNames());
                        Integer count = reservation.getVmNames().size();
                        String capacityReservation =
                            reservationMetrics.wrapWithMetrics(
                                universe.getUniverseUUID(),
                                count,
                                Common.CloudType.aws,
                                CapacityReservationUtil.ReservationAction.RESERVE,
                                () ->
                                    cloudAPI.createCapacityReservation(
                                        provider,
                                        reservationName,
                                        zoneReservation.getRegion(),
                                        reservation.getZone(),
                                        instanceType,
                                        count,
                                        Map.of(
                                            "universe-name",
                                            universe.getName(),
                                            "universe-uuid",
                                            universe.getUniverseUUID().toString())));
                        log.info(
                            "Created reservation {} for {}", capacityReservation, instanceType);
                        reservation.setReservationName(capacityReservation);
                        processedReservations.add(reservationName);
                      });
            }
          }
        });
  }

  public static Integer extractZoneNumber(String zone) {
    Matcher matcher = ZONE_NUMBER_PATTERN.matcher(zone);
    if (matcher.find()) {
      return Integer.parseInt(matcher.group(1));
    }
    return -1;
  }

  private UniverseDefinitionTaskParams.CapacityReservationState
      getOrCreateCapacityReservationState() {
    UniverseDefinitionTaskParams.CapacityReservationState capacityReservationState =
        Universe.getOrBadRequest(taskParams().getUniverseUUID())
            .getUniverseDetails()
            .getCapacityReservationState();
    if (capacityReservationState == null) {
      capacityReservationState = new UniverseDefinitionTaskParams.CapacityReservationState();
    }
    return capacityReservationState;
  }

  public static String getCapacityReservationGroupName(
      UUID universeUUID, UniverseDefinitionTaskParams.ClusterType clusterType, String region) {
    return universeUUID.toString() + "_" + region + "_" + clusterType + GROUP_SUFFIX;
  }

  public static String getInstanceReservationName(String instanceType, String zoneID) {
    return instanceType + "_in_" + zoneID;
  }

  public static String getZoneInstanceCapacityReservationName(
      UUID universeUUID,
      UniverseDefinitionTaskParams.ClusterType clusterType,
      String zone,
      String instanceType) {
    return instanceType + "-" + zone + "-" + universeUUID.toString() + "-" + clusterType;
  }
}
