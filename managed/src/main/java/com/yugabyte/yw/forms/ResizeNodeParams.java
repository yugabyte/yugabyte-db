// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import java.util.ArrayList;
import java.util.EnumSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.function.Function;
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

    RuntimeConfigFactory runtimeConfigFactory =
        Play.current().injector().instanceOf(RuntimeConfigFactory.class);

    for (Cluster cluster : clusters) {
      UserIntent newUserIntent = cluster.userIntent;
      UserIntent currentUserIntent =
          universe.getUniverseDetails().getClusterByUuid(cluster.uuid).userIntent;

      String errorStr =
          getResizeIsPossibleError(
              currentUserIntent, newUserIntent, universe, runtimeConfigFactory, true);
      if (errorStr != null) {
        throw new IllegalArgumentException(errorStr);
      }
    }
  }

  /**
   * Checks if smart resize is available
   *
   * @param currentUserIntent current user intent
   * @param newUserIntent desired user intent
   * @param universe current universe
   * @param verifyVolumeSize whether to check volume size
   * @return
   */
  public static boolean checkResizeIsPossible(
      UserIntent currentUserIntent,
      UserIntent newUserIntent,
      Universe universe,
      boolean verifyVolumeSize) {

    RuntimeConfigFactory runtimeConfigFactory =
        Play.current().injector().instanceOf(RuntimeConfigFactory.class);

    return checkResizeIsPossible(
        currentUserIntent, newUserIntent, universe, runtimeConfigFactory, verifyVolumeSize);
  }

  /**
   * Checks if smart resize is available
   *
   * @param currentUserIntent current user intent
   * @param newUserIntent desired user intent
   * @param universe current universe
   * @param runtimeConfigFactory config factory
   * @param verifyVolumeSize whether to check volume size
   * @return
   */
  public static boolean checkResizeIsPossible(
      UserIntent currentUserIntent,
      UserIntent newUserIntent,
      Universe universe,
      RuntimeConfigFactory runtimeConfigFactory,
      boolean verifyVolumeSize) {
    String res =
        getResizeIsPossibleError(
            currentUserIntent, newUserIntent, universe, runtimeConfigFactory, verifyVolumeSize);
    if (res != null) {
      log.debug("resize is forbidden: " + res);
    }
    return res == null;
  }

  /**
   * Checks if smart resize is available and returns error message
   *
   * @param currentUserIntent current user intent
   * @param newUserIntent desired user intent
   * @param universe current universe
   * @param verifyVolumeSize whether to check volume size
   * @return null if available, otherwise returns error message
   */
  private static String getResizeIsPossibleError(
      UserIntent currentUserIntent,
      UserIntent newUserIntent,
      Universe universe,
      RuntimeConfigFactory runtimeConfigFactory,
      boolean verifyVolumeSize) {

    boolean allowUnsupportedInstances =
        runtimeConfigFactory
            .forUniverse(universe)
            .getBoolean("yb.internal.allow_unsupported_instances");
    if (currentUserIntent == null || newUserIntent == null) {
      return "Should have both intents, but got: " + currentUserIntent + ", " + newUserIntent;
    }
    // Check valid provider.
    if (!SUPPORTED_CLOUD_TYPES.contains(currentUserIntent.providerType)) {
      return "Smart resizing is only supported for AWS / GCP, It is: "
          + currentUserIntent.providerType.toString();
    }
    List<String> errors = new ArrayList<>();
    // Checking disk.
    boolean diskChanged =
        checkDiskChanged(
            currentUserIntent,
            newUserIntent,
            intent -> intent.deviceInfo,
            errors::add,
            verifyVolumeSize);
    boolean masterDiskChanged =
        newUserIntent.dedicatedNodes
            ? masterDiskChanged =
                checkDiskChanged(
                    currentUserIntent,
                    newUserIntent,
                    intent -> intent.masterDeviceInfo,
                    errors::add,
                    verifyVolumeSize)
            : false;
    // Checking instance type.
    boolean instanceTypeChanged =
        checkInstanceTypeChanged(
            currentUserIntent,
            newUserIntent,
            intent -> intent.instanceType,
            errors::add,
            allowUnsupportedInstances);
    boolean masterInstanceTypeChanged =
        newUserIntent.dedicatedNodes
            ? checkInstanceTypeChanged(
                currentUserIntent,
                newUserIntent,
                intent -> intent.masterInstanceType,
                errors::add,
                allowUnsupportedInstances)
            : false;

    if (errors.size() > 0) {
      return errors.get(0);
    }
    if ((diskChanged || instanceTypeChanged)
        && hasEphemeralStorage(
            currentUserIntent.providerType,
            currentUserIntent.instanceType,
            currentUserIntent.deviceInfo)) {
      return "ResizeNode operation is not supported for instances with ephemeral drives";
    }
    if ((masterDiskChanged || masterInstanceTypeChanged)
        && hasEphemeralStorage(
            currentUserIntent.providerType,
            currentUserIntent.masterInstanceType,
            currentUserIntent.masterDeviceInfo)) {
      return "ResizeNode operation is not supported for instances with ephemeral drives";
    }
    if (verifyVolumeSize
        && !diskChanged
        && !instanceTypeChanged
        && !masterDiskChanged
        && !masterInstanceTypeChanged) {
      return "Nothing changed!";
    }
    return null;
  }

  private static boolean checkDiskChanged(
      UserIntent currentUserIntent,
      UserIntent newUserIntent,
      Function<UserIntent, DeviceInfo> getter,
      Consumer<String> errorConsumer,
      boolean verifyVolumeSize) {
    DeviceInfo newDeviceInfo = getter.apply(newUserIntent);
    DeviceInfo currentDeviceInfo = getter.apply(currentUserIntent);

    if (newDeviceInfo != null && newDeviceInfo.volumeSize != null) {
      Integer currDiskSize = currentDeviceInfo.volumeSize;
      if (verifyVolumeSize && currDiskSize > newDeviceInfo.volumeSize) {
        errorConsumer.accept(
            "Disk size cannot be decreased. It was "
                + currDiskSize
                + " got "
                + newDeviceInfo.volumeSize);
      }
      // If numVolumes is specified in the newUserIntent,
      // make sure it is the same as the current value.
      if (newDeviceInfo.numVolumes != null
          && !newDeviceInfo.numVolumes.equals(currentDeviceInfo.numVolumes)) {
        errorConsumer.accept(
            "Number of volumes cannot be changed. It was "
                + currentDeviceInfo.numVolumes
                + " got "
                + newDeviceInfo.numVolumes);
      }
      return !Objects.equals(currDiskSize, newDeviceInfo.volumeSize);
    }
    return false;
  }

  private static boolean checkInstanceTypeChanged(
      UserIntent currentUserIntent,
      UserIntent newUserIntent,
      Function<UserIntent, String> getter,
      Consumer<String> errorConsumer,
      boolean allowUnsupportedInstances) {
    String currentInstanceTypeCode = getter.apply(currentUserIntent);
    String newInstanceTypeCode = getter.apply(newUserIntent);
    if (newInstanceTypeCode != null
        && !Objects.equals(newInstanceTypeCode, currentInstanceTypeCode)) {
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
        errorConsumer.accept(
            String.format(
                "Provider %s of type %s does not contain the intended instance type '%s'",
                currentUserIntent.provider, currentUserIntent.providerType, newInstanceTypeCode));
      }
      return true;
    }
    return false;
  }

  public static class Converter extends BaseConverter<ResizeNodeParams> {}
}
