// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.utils;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.common.config.ConfKeyInfo;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import java.util.List;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class CapacityReservationUtil {
  public static final String CAPACITY_RESERVATION_KEY = "CapacityReservationKey";

  public static String getReservationIfPresent(
      TaskExecutor.TaskCache taskCache, Common.CloudType cloudType, String nodeName) {
    if (taskCache.get(CapacityReservationUtil.CAPACITY_RESERVATION_KEY) != null) {
      try {
        UniverseDefinitionTaskParams.CapacityReservationState capacityReservationState =
            taskCache.get(
                CapacityReservationUtil.CAPACITY_RESERVATION_KEY,
                UniverseDefinitionTaskParams.CapacityReservationState.class);
        log.debug("Cap state {}", Json.toJson(capacityReservationState));
        UniverseDefinitionTaskParams.ReservationInfo reservationInfo =
            getReservationForProviderType(capacityReservationState, cloudType);
        if (reservationInfo instanceof UniverseDefinitionTaskParams.AzureReservationInfo) {
          for (UniverseDefinitionTaskParams.AzureRegionReservation regionReservation :
              ((UniverseDefinitionTaskParams.AzureReservationInfo) reservationInfo)
                  .getReservationsByRegionMap()
                  .values()) {
            for (UniverseDefinitionTaskParams.PerInstanceTypeReservation perInstanceType :
                regionReservation.getReservationsByType().values()) {
              for (UniverseDefinitionTaskParams.ZonedReservation zonedReservation :
                  perInstanceType.getZonedReservation().values()) {
                if (zonedReservation.getVmNames().contains(nodeName)) {
                  log.debug(
                      "Using azure capacity reservation {}", zonedReservation.getReservationName());
                  return regionReservation.getGroupName();
                }
              }
            }
          }
        } else if (reservationInfo instanceof UniverseDefinitionTaskParams.AwsReservationInfo) {
          for (UniverseDefinitionTaskParams.AwsZoneReservation zoneReservation :
              ((UniverseDefinitionTaskParams.AwsReservationInfo) reservationInfo)
                  .getReservationsByZoneMap()
                  .values()) {
            for (UniverseDefinitionTaskParams.PerInstanceTypeReservation perInstanceType :
                zoneReservation.getReservationsByType().values()) {
              for (UniverseDefinitionTaskParams.ZonedReservation zonedReservation :
                  perInstanceType.getZonedReservation().values()) {
                if (zonedReservation.getVmNames().contains(nodeName)) {
                  log.debug(
                      "Using aws capacity reservation {}", zonedReservation.getReservationName());
                  return zonedReservation.getReservationName();
                }
              }
            }
          }
        }
      } catch (Exception e) {
        log.error("Failed to deserialize reservation", e);
      }
    }
    return null;
  }

  public enum OperationType {
    CREATE,
    EDIT,
    RESIZE,
    RESUME
  }

  public static <T> T getReservationForProviderType(
      UniverseDefinitionTaskParams.CapacityReservationState state, Common.CloudType cloudType) {
    switch (cloudType) {
      case azu:
        return (T) state.getAzureReservationInfo();
      case aws:
        return (T) state.getAwsReservationInfo();
    }
    return null;
  }

  public static void initReservationForProviderType(
      UniverseDefinitionTaskParams.CapacityReservationState state, Common.CloudType cloudType) {
    switch (cloudType) {
      case azu:
        state.setAzureReservationInfo(new UniverseDefinitionTaskParams.AzureReservationInfo());
      case aws:
        state.setAwsReservationInfo(new UniverseDefinitionTaskParams.AwsReservationInfo());
    }
  }

  public static boolean isReservationSupported(
      RuntimeConfGetter confGetter, Common.CloudType cloudType, OperationType operationType) {
    ConfKeyInfo<Boolean> enabledFlag = null;
    ConfKeyInfo<List> operationsList = null;
    switch (cloudType) {
      case azu:
        enabledFlag = GlobalConfKeys.enableCapacityReservationAzure;
        operationsList = GlobalConfKeys.capacityReservationOperationsAzure;
        break;
      case aws:
        enabledFlag = GlobalConfKeys.enableCapacityReservationAws;
        operationsList = GlobalConfKeys.capacityReservationOperationsAws;
        break;
    }
    if (enabledFlag == null) {
      return false;
    }
    if (!confGetter.getGlobalConf(enabledFlag)) {
      return false;
    }
    if (operationsList != null) {
      List<String> supportedList = confGetter.getGlobalConf(operationsList);
      return supportedList.contains(operationType.name());
    }
    return false;
  }
}
