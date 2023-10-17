// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
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
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Only ROLLING_UPGRADE option is supported for resizing node (changing VM type).");
    }

    RuntimeConfigFactory runtimeConfigFactory =
        Play.current().injector().instanceOf(RuntimeConfigFactory.class);
    boolean allowUnsupportedInstances =
        runtimeConfigFactory
            .forUniverse(universe)
            .getBoolean("yb.internal.allow_unsupported_instances");

    boolean hasClustersToResize = false;
    for (Cluster cluster : clusters) {
      UserIntent newUserIntent = cluster.userIntent;
      UserIntent currentUserIntent =
          universe.getUniverseDetails().getClusterByUuid(cluster.uuid).userIntent;
      if (!hasResizeChanges(currentUserIntent, newUserIntent)) {
        continue;
      }

      String errorStr =
          checkResizeIsPossible(currentUserIntent, newUserIntent, allowUnsupportedInstances);
      if (errorStr != null) {
        throw new PlatformServiceException(BAD_REQUEST, errorStr);
      }
      hasClustersToResize = true;
    }
    if (!hasClustersToResize) {
      throw new IllegalArgumentException("No changes!");
    }
  }

  private boolean hasResizeChanges(UserIntent currentUserIntent, UserIntent newUserIntent) {
    if (currentUserIntent == null || newUserIntent == null) {
      return false;
    }
    return !(Objects.equals(currentUserIntent.instanceType, newUserIntent.instanceType)
        && Objects.equals(currentUserIntent.deviceInfo, newUserIntent.deviceInfo));
  }

  /**
   * Checks if smart resize is available
   *
   * @param currentUserIntent current user intent
   * @param newUserIntent desired user intent
   * @param allowUnsupportedInstances boolean to skip instance type checking
   * @return null if available, otherwise returns error message
   */
  public static String checkResizeIsPossible(
      UserIntent currentUserIntent, UserIntent newUserIntent, boolean allowUnsupportedInstances) {
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
      DeviceInfo newDeviceInfoCloned = newUserIntent.deviceInfo.clone();
      newDeviceInfoCloned.volumeSize = currDiskSize;
      if (!newDeviceInfoCloned.equals(currentUserIntent.deviceInfo)) {
        return "Only volume size should be changed to do smart resize";
      }
      diskChanged = !Objects.equals(currDiskSize, newUserIntent.deviceInfo.volumeSize);
    }

    String newInstanceTypeCode = newUserIntent.instanceType;
    if (!diskChanged && currentUserIntent.instanceType.equals(newInstanceTypeCode)) {
      return "Nothing changed!";
    }
    if (hasEphemeralStorage(currentUserIntent)) {
      return "ResizeNode operation is not supported for instances with ephemeral drives";
    }
    // Checking new instance is valid.
    if (!newInstanceTypeCode.equals(currentUserIntent.instanceType)) {
      String provider = currentUserIntent.provider;
      List<InstanceType> instanceTypes =
          InstanceType.findByProvider(
              Provider.getOrBadRequest(UUID.fromString(provider)),
              Play.current().injector().instanceOf(Config.class),
              Play.current().injector().instanceOf(ConfigHelper.class),
              allowUnsupportedInstances);
      InstanceType newInstanceType =
          instanceTypes
              .stream()
              .filter(type -> type.getInstanceTypeCode().equals(newInstanceTypeCode))
              .findFirst()
              .orElse(null);
      if (newInstanceType == null) {
        return String.format(
            "Provider %s of type %s does not contain the intended instance type '%s'",
            currentUserIntent.provider, currentUserIntent.providerType, newInstanceTypeCode);
      }
    }

    return null;
  }

  public static class Converter extends BaseConverter<ResizeNodeParams> {}
}
