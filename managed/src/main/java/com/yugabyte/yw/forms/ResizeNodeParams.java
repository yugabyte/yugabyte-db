// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.cronutils.utils.VisibleForTesting;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import java.util.EnumSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.extern.slf4j.Slf4j;
import play.api.Play;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = ResizeNodeParams.Converter.class)
@Data
@EqualsAndHashCode(callSuper = true)
@Slf4j
public class ResizeNodeParams extends UpgradeTaskParams {

  private static final Set<Common.CloudType> SUPPORTED_CLOUD_TYPES =
      EnumSet.of(Common.CloudType.gcp, Common.CloudType.aws);

  private boolean forceResizeNode;

  @Override
  public void verifyParams(Universe universe) {
    super.verifyParams(universe);

    if (upgradeOption != UpgradeOption.ROLLING_UPGRADE) {
      throw new IllegalArgumentException(
          "Only ROLLING_UPGRADE option is supported for resizing node (changing VM type).");
    }

    UserIntent newUserIntent = getPrimaryCluster().userIntent;
    UserIntent currentUserIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;

    String errorStr =
        checkResizeIsPossible(currentUserIntent, newUserIntent, isSkipInstanceChecking());
    if (errorStr != null) {
      throw new IllegalArgumentException(errorStr);
    }
  }

  /**
   * Checks if smart resize is available
   *
   * @param currentUserIntent
   * @param newUserIntent
   * @return null if available, otherwise returns error message
   */
  public static String checkResizeIsPossible(
      UserIntent currentUserIntent, UserIntent newUserIntent) {
    return checkResizeIsPossible(currentUserIntent, newUserIntent, false);
  }

  private static String checkResizeIsPossible(
      UserIntent currentUserIntent, UserIntent newUserIntent, boolean skipInstanceChecking) {
    if (currentUserIntent == null || newUserIntent == null) {
      return "Should have both intents, but got: " + currentUserIntent + ", " + newUserIntent;
    }
    // Check valid provider.
    if (!SUPPORTED_CLOUD_TYPES.contains(newUserIntent.providerType)) {
      return "Smart resizing is only supported for AWS / GCP, It is: "
          + currentUserIntent.providerType.toString();
    }
    // Checking disk.
    boolean diskChanged = false;
    if (newUserIntent.deviceInfo != null && newUserIntent.deviceInfo.volumeSize != null) {
      Integer currDiskSize = currentUserIntent.deviceInfo.volumeSize;
      if (currDiskSize > newUserIntent.deviceInfo.volumeSize) {
        return "Disk size cannot be decreased. It was "
            + currDiskSize
            + " got "
            + newUserIntent.deviceInfo.volumeSize;
      }
      if (!Objects.equals(
          currentUserIntent.deviceInfo.numVolumes, newUserIntent.deviceInfo.numVolumes)) {
        return "Number of volumes cannot be changed. It was "
            + currentUserIntent.deviceInfo.numVolumes
            + " got "
            + newUserIntent.deviceInfo.numVolumes;
      }
      diskChanged = !Objects.equals(currDiskSize, newUserIntent.deviceInfo.volumeSize);
    }

    String newInstanceTypeCode = newUserIntent.instanceType;
    if (!diskChanged && currentUserIntent.instanceType.equals(newInstanceTypeCode)) {
      return "Nothing changed!";
    }
    // Checking new instance is valid.
    if (!newInstanceTypeCode.equals(currentUserIntent.instanceType) && !skipInstanceChecking) {
      if (currentUserIntent.providerType.equals(Common.CloudType.aws)) {
        if (newInstanceTypeCode.contains("i3")) {
          return "ResizeNode operation does not support the instance type " + newInstanceTypeCode;
        }
        int dotPosition = newInstanceTypeCode.indexOf('.');
        if (dotPosition > 0 && newInstanceTypeCode.charAt(dotPosition - 1) == 'd') {
          return "ResizeNode operation does not support the instance type " + newInstanceTypeCode;
        }
      }
      String provider = currentUserIntent.provider;
      List<InstanceType> instanceTypes =
          InstanceType.findByProvider(
              Provider.getOrBadRequest(UUID.fromString(provider)),
              Play.current().injector().instanceOf(Config.class),
              Play.current().injector().instanceOf(ConfigHelper.class));
      log.info(instanceTypes.toString());
      InstanceType newInstanceType =
          instanceTypes
              .stream()
              .filter(type -> type.getInstanceTypeCode().equals(newInstanceTypeCode))
              .findFirst()
              .orElse(null);
      if (newInstanceType == null) {
        return "Provider "
            + currentUserIntent.providerType
            + " does not have the intended instance type "
            + newInstanceTypeCode;
      }
      // Make sure instance type has the right storage.
      if (newInstanceType.instanceTypeDetails != null
          && newInstanceType.instanceTypeDetails.volumeDetailsList != null
          && newInstanceType.instanceTypeDetails.volumeDetailsList.size() > 0
          && newInstanceType.instanceTypeDetails.volumeDetailsList.get(0).volumeType
              == InstanceType.VolumeType.NVME) {
        return "Instance type "
            + newInstanceTypeCode
            + " has NVME storage and is not supported by the ResizeNode operation";
      }
    }

    return null;
  }

  @VisibleForTesting
  protected boolean isSkipInstanceChecking() {
    return false;
  }

  public static class Converter extends BaseConverter<ResizeNodeParams> {}
}
