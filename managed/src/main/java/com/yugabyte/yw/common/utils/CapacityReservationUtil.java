// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.utils;

import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.common.config.ConfKeyInfo;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Provider;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class CapacityReservationUtil {

  public static final String CAPACITY_RESERVATION_KEY = "CapacityReservationKey";

  public static final Pattern AZURE_VM_PATTERN =
      Pattern.compile("^([A-Za-z_]+?)(\\d+)([0-9a-zA-Z_-]*?)(_v\\d+)?$");

  public static final String[][] AZURE_SUPPORTED_VERSIONS = {
    {"A", "", "2"},
    {"B", "", ""},
    {"B", "ps", "2"},
    {"B", "s,as", "2"},
    {"D", "", "2+"},
    {"DS", "", "2+"},
    {"D", "s", "3+"},
    {"D", "ads", "5,6"},
    {"D", "a", "4"},
    {"D", "as", "4+"},
    {"D", "d", "4,5"},
    {"D", "ds", "4+"},
    {"D", "ls,lds", "5+"},
    {"DC", "s", "2"},
    {"DC", "as,ads", "5"},
    {"DC", "es,eds", "5"},
    {"EC", "as,ads", "5"},
    {"EC", "es,eds", "5"},
    {"D", "pls,ps,pds,plds", "5+"},
    {"E", "ps,pds", "5+"},
    {"E", "", ""},
    {"E", "a,as", "4"},
    {"E", "as,ads", "5+"},
    {"E", "bds,bs", "5"},
    {"E", "d,ds", "4+"},
    {"F", "", ""},
    {"F", "x", ""},
    {"L", "s,as", "3"},
  };

  public enum ReservationAction {
    RESERVE,
    RELEASE,
    CREATE_GROUP,
    DELETE_GROUP;
  }

  public static boolean azureCheckInstanceTypeIsSupported(String instanceType) {
    try {
      Matcher matcher = AZURE_VM_PATTERN.matcher(instanceType);
      if (matcher.find()) {
        String family = matcher.group(1);
        String suffix = matcher.group(3);
        String vNumber = matcher.group(4);
        for (String[] parts : AZURE_SUPPORTED_VERSIONS) {
          if (azureVersionMatches(family, suffix, vNumber, parts)) {
            return true;
          }
        }
      }
    } catch (Exception e) {
      log.error("Failed to check instance type", e);
    }
    log.debug("Instance type {} is not supported", instanceType);
    return false;
  }

  private static boolean azureVersionMatches(
      String family, String suffix, String vNumber, String[] parts) {
    boolean result = false;
    if (family.split("_")[1].equals(parts[0])) {
      for (String sub : parts[1].split(",")) {
        if (suffix.split("_")[0].equals(sub)) {
          int version = 0;
          if (vNumber != null) {
            version = Integer.parseInt(vNumber.substring(2));
          }
          for (String subV : parts[2].split(",")) {
            if (subV.endsWith("+")) {
              int desired = Integer.parseInt(subV.substring(0, subV.length() - 1));
              result = result || version >= desired;
            } else if (subV.isBlank()) {
              result = true;
            } else {
              result = result || version == Integer.parseInt(subV);
            }
          }
        }
      }
    }
    return result;
  }

  public static String getReservationIfPresent(
      TaskExecutor.TaskCache taskCache, Provider provider, String nodeName) {
    if (taskCache.get(CapacityReservationUtil.CAPACITY_RESERVATION_KEY) != null) {
      try {
        UniverseDefinitionTaskParams.CapacityReservationState capacityReservationState =
            taskCache.get(
                CapacityReservationUtil.CAPACITY_RESERVATION_KEY,
                UniverseDefinitionTaskParams.CapacityReservationState.class);
        log.debug("Cap state {}", Json.toJson(capacityReservationState));
        UniverseDefinitionTaskParams.ReservationInfo reservationInfo =
            getReservationForProvider(capacityReservationState, provider);
        if (reservationInfo instanceof UniverseDefinitionTaskParams.AzureReservationInfo) {
          for (UniverseDefinitionTaskParams.AzureRegionReservation regionReservation :
              ((UniverseDefinitionTaskParams.AzureReservationInfo) reservationInfo)
                  .getReservationsByRegionMap()
                  .values()) {
            for (UniverseDefinitionTaskParams.PerInstanceTypeReservation perInstanceType :
                regionReservation.getReservationsByType().values()) {
              for (UniverseDefinitionTaskParams.ZonedReservation zonedReservation :
                  perInstanceType.getZonedReservation().values()) {
                if (zonedReservation.getReservationName() != null
                    && zonedReservation.getVmNames().contains(nodeName)) {
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

  public static <T extends UniverseDefinitionTaskParams.ReservationInfo>
      T getReservationForProvider(
          UniverseDefinitionTaskParams.CapacityReservationState state, Provider provider) {
    switch (provider.getCloudCode()) {
      case azu:
        return (T) state.getAzureReservationInfos().get(provider.getUuid());
      case aws:
        return (T) state.getAwsReservationInfos().get(provider.getUuid());
    }
    return null;
  }

  public static void initReservationForProvider(
      UniverseDefinitionTaskParams.CapacityReservationState state, Provider provider) {
    switch (provider.getCloudCode()) {
      case azu:
        state
            .getAzureReservationInfos()
            .putIfAbsent(
                provider.getUuid(), new UniverseDefinitionTaskParams.AzureReservationInfo());
        break;
      case aws:
        state
            .getAwsReservationInfos()
            .putIfAbsent(provider.getUuid(), new UniverseDefinitionTaskParams.AwsReservationInfo());
        break;
    }
  }

  public static boolean isReservationSupported(
      RuntimeConfGetter confGetter, Provider provider, OperationType operationType) {
    if (!isReservationEnabled(confGetter, provider)) {
      return false;
    }
    ConfKeyInfo<List> operationsList = null;
    switch (provider.getCloudCode()) {
      case azu:
        operationsList = GlobalConfKeys.capacityReservationOperationsAzure;
        break;
      case aws:
        operationsList = GlobalConfKeys.capacityReservationOperationsAws;
        break;
    }

    if (operationsList != null) {
      List<String> supportedList = confGetter.getGlobalConf(operationsList);
      return supportedList.contains(operationType.name());
    }
    return false;
  }

  public static boolean isReservationEnabled(RuntimeConfGetter confGetter, Provider provider) {
    ConfKeyInfo<Boolean> enabledFlag = null;
    switch (provider.getCloudCode()) {
      case azu:
        enabledFlag = ProviderConfKeys.enableCapacityReservationAzure;
        break;
      case aws:
        enabledFlag = ProviderConfKeys.enableCapacityReservationAws;
        break;
    }
    if (enabledFlag == null) {
      return false;
    }
    return confGetter.getConfForScope(provider, enabledFlag);
  }
}
