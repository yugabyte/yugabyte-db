// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Comparator;
import java.util.Date;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.time.DateUtils;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = ResizeNodeParams.Converter.class)
@Data
@EqualsAndHashCode(callSuper = true)
@Slf4j
public class ResizeNodeParams extends UpgradeWithGFlags {

  private static final Pattern AZU_NO_LOCAL_DISK = Pattern.compile("Standard_(D|E)[0-9]*as\\_v5");

  public static final int AZU_DISK_LIMIT_NO_DOWNTIME = 4 * 1024; // 4 TiB

  private static final Set<Common.CloudType> SUPPORTED_CLOUD_TYPES =
      EnumSet.of(
          Common.CloudType.gcp,
          Common.CloudType.aws,
          Common.CloudType.kubernetes,
          Common.CloudType.azu);

  private boolean forceResizeNode;

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public void verifyParams(Universe universe) {
    verifyParams(universe, null);
  }

  @Override
  public void verifyParams(Universe universe, NodeDetails.NodeState nodeState) {
    super.verifyParams(universe, nodeState); // we call verifyParams which will fail

    RuntimeConfGetter runtimeConfGetter =
        StaticInjectorHolder.injector().instanceOf(RuntimeConfGetter.class);

    // Both master and tserver can be null. But if one is provided, both should be provided.
    if ((masterGFlags == null && tserverGFlags != null)
        || (tserverGFlags == null && masterGFlags != null)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Either none or both master and tserver gflags are required");
    }
    if (masterGFlags != null) {
      // We want this flow to only be enabled for cloud in the first go.
      if (!runtimeConfGetter.getConfForScope(universe, UniverseConfKeys.cloudEnabled)) {
        throw new PlatformServiceException(
            Status.METHOD_NOT_ALLOWED, "Cannot resize with gflag changes.");
      }
      masterGFlags = GFlagsUtil.trimFlags(masterGFlags);
      tserverGFlags = GFlagsUtil.trimFlags(tserverGFlags);
      GFlagsUtil.checkConsistency(masterGFlags, tserverGFlags);
    }

    if (upgradeOption != UpgradeOption.ROLLING_UPGRADE) {
      throw new IllegalArgumentException(
          "Only ROLLING_UPGRADE option is supported for resizing node (changing VM type).");
    }

    boolean hasClustersToResize = false;
    for (Cluster cluster : clusters) {
      Collection<NodeDetails> nodesInCluster = universe.getNodesInCluster(cluster.uuid);
      UserIntent newUserIntent = cluster.userIntent;
      UserIntent currentUserIntent =
          universe.getUniverseDetails().getClusterByUuid(cluster.uuid).userIntent;
      if (!hasResizeChanges(currentUserIntent, newUserIntent, nodesInCluster)) {
        continue;
      }

      String errorStr =
          getResizeIsPossibleError(
              cluster.uuid, currentUserIntent, newUserIntent, universe, runtimeConfGetter, true);
      if (errorStr != null) {
        throw new IllegalArgumentException(errorStr);
      }
      hasClustersToResize = true;
    }
    if (!hasClustersToResize && !forceResizeNode) {
      throw new IllegalArgumentException("No changes!");
    }
    if (flagsProvided(universe)) {
      verifyGFlags(universe);
    }
  }

  private boolean hasResizeChanges(
      UserIntent currentUserIntent,
      UserIntent newUserIntent,
      Collection<NodeDetails> nodesInCluster) {
    if (currentUserIntent == null || newUserIntent == null) {
      return false;
    }
    return nodesInCluster.stream()
        .filter(
            n -> {
              String oldInstanceType = currentUserIntent.getInstanceTypeForNode(n);
              String newInstanceType = newUserIntent.getInstanceTypeForNode(n);

              DeviceInfo oldDevice = currentUserIntent.getDeviceInfoForNode(n);
              DeviceInfo newDevice = newUserIntent.getDeviceInfoForNode(n);

              return !Objects.equals(oldInstanceType, newInstanceType)
                  || !Objects.equals(oldDevice, newDevice);
            })
        .findFirst()
        .isPresent();
  }

  /**
   * Checks if smart resize is available
   *
   * @param clusterUUID cluster UUID
   * @param currentUserIntent current user intent
   * @param newUserIntent desired user intent
   * @param universe current universe
   * @param verifyVolumeSize whether to check volume size
   * @return
   */
  public static boolean checkResizeIsPossible(
      UUID clusterUUID,
      UserIntent currentUserIntent,
      UserIntent newUserIntent,
      Universe universe,
      boolean verifyVolumeSize) {

    RuntimeConfGetter runtimeConfGetter =
        StaticInjectorHolder.injector().instanceOf(RuntimeConfGetter.class);

    return checkResizeIsPossible(
        clusterUUID,
        currentUserIntent,
        newUserIntent,
        universe,
        runtimeConfGetter,
        verifyVolumeSize);
  }

  /**
   * Checks if smart resize is available
   *
   * @param clusterUUID cluster UUID
   * @param currentUserIntent current user intent
   * @param newUserIntent desired user intent
   * @param universe current universe
   * @param runtimeConfGetter config factory
   * @param verifyVolumeSize whether to check volume size
   * @return
   */
  public static boolean checkResizeIsPossible(
      UUID clusterUUID,
      UserIntent currentUserIntent,
      UserIntent newUserIntent,
      Universe universe,
      RuntimeConfGetter runtimeConfGetter,
      boolean verifyVolumeSize) {
    String res =
        getResizeIsPossibleError(
            clusterUUID,
            currentUserIntent,
            newUserIntent,
            universe,
            runtimeConfGetter,
            verifyVolumeSize);
    if (res != null) {
      log.debug("resize is forbidden: " + res);
    }
    return res == null;
  }

  /**
   * Checks if smart resize is available and returns error message
   *
   * @param clusterUUID cluster UUID
   * @param currentUserIntent current user intent
   * @param newUserIntent desired user intent
   * @param universe current universe
   * @param verifyVolumeSize whether to check volume size
   * @return null if available, otherwise returns error message
   */
  private static String getResizeIsPossibleError(
      UUID clusterUUID,
      UserIntent currentUserIntent,
      UserIntent newUserIntent,
      Universe universe,
      RuntimeConfGetter runtimeConfGetter,
      boolean verifyVolumeSize) {
    Provider provider = Provider.getOrBadRequest(UUID.fromString(currentUserIntent.provider));
    boolean allowUnsupportedInstances =
        runtimeConfGetter.getConfForScope(provider, ProviderConfKeys.allowUnsupportedInstances);
    if (currentUserIntent == null || newUserIntent == null) {
      return "Should have both intents, but got: " + currentUserIntent + ", " + newUserIntent;
    }
    // Check valid provider.
    if (!SUPPORTED_CLOUD_TYPES.contains(currentUserIntent.providerType)) {
      return "Smart resizing is only supported for AWS / GCP / K8S/ Azu, It is: "
          + currentUserIntent.providerType.toString();
    }
    if (currentUserIntent.dedicatedNodes != newUserIntent.dedicatedNodes) {
      return "Smart resize is not possible if is dedicated mode changed";
    }
    Collection<NodeDetails> nodes = universe.getUniverseDetails().getNodesInCluster(clusterUUID);
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
            errors::add,
            nodes.stream()
                .filter(n -> n.dedicatedTo != UniverseTaskBase.ServerType.MASTER)
                .collect(Collectors.toList()),
            diskChanged,
            allowUnsupportedInstances);
    boolean masterInstanceTypeChanged =
        newUserIntent.dedicatedNodes
            ? checkInstanceTypeChanged(
                currentUserIntent,
                newUserIntent,
                errors::add,
                nodes.stream()
                    .filter(n -> n.dedicatedTo == UniverseTaskBase.ServerType.MASTER)
                    .collect(Collectors.toList()),
                masterDiskChanged,
                allowUnsupportedInstances)
            : false;

    if (errors.size() > 0) {
      return errors.get(0);
    }
    if ((diskChanged || masterDiskChanged)
        && currentUserIntent.providerType == Common.CloudType.azu) {
      if (diskChanged
          && currentUserIntent.deviceInfo.storageType
              == PublicCloudConstants.StorageType.UltraSSD_LRS) {
        return "UltraSSD doesn't support resizing without downtime";
      }
      if (masterDiskChanged
          && currentUserIntent.masterDeviceInfo.storageType
              == PublicCloudConstants.StorageType.UltraSSD_LRS) {
        return "UltraSSD doesn't support resizing without downtime";
      }
      if (diskChanged
          && currentUserIntent.deviceInfo.volumeSize <= AZU_DISK_LIMIT_NO_DOWNTIME
          && newUserIntent.deviceInfo.volumeSize > AZU_DISK_LIMIT_NO_DOWNTIME) {
        return "Cannot expand from "
            + currentUserIntent.deviceInfo.volumeSize
            + "GB to "
            + newUserIntent.deviceInfo.volumeSize
            + "GB without VM deallocation";
      }
      if (masterDiskChanged
          && currentUserIntent.masterDeviceInfo.volumeSize <= AZU_DISK_LIMIT_NO_DOWNTIME
          && newUserIntent.masterDeviceInfo.volumeSize > AZU_DISK_LIMIT_NO_DOWNTIME) {
        return "Cannot expand from "
            + currentUserIntent.masterDeviceInfo.volumeSize
            + "GB to "
            + newUserIntent.masterDeviceInfo.volumeSize
            + "GB without VM deallocation";
      }
    }
    if ((masterDiskChanged || masterInstanceTypeChanged)
        && hasEphemeralStorage(
            currentUserIntent.providerType,
            currentUserIntent.masterInstanceType,
            currentUserIntent.masterDeviceInfo)) {
      return "ResizeNode operation is not supported for instances with ephemeral drives";
    }
    if (currentUserIntent.providerType == Common.CloudType.aws) {
      int cooldownInHours =
          runtimeConfGetter.getGlobalConf(GlobalConfKeys.awsDiskResizeCooldownHours);
      boolean checkTservers = diskChanged || !verifyVolumeSize;
      if ((checkTservers && isAwsCooldown(universe, clusterUUID, true, cooldownInHours))
          || masterDiskChanged && isAwsCooldown(universe, clusterUUID, false, cooldownInHours)) {
        return String.format("Resize cooldown in aws (%d hours) is still active", cooldownInHours);
      }
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

  private static boolean isAwsCooldown(
      Universe universe, UUID clusterUUID, boolean isTserver, int cooldownInHours) {
    Optional<Date> lastDiskUpdate =
        universe.getUniverseDetails().getNodesInCluster(clusterUUID).stream()
            .filter(n -> n.isTserver == isTserver)
            .map(n -> n.lastVolumeUpdateTime)
            .filter(Objects::nonNull)
            .max(Comparator.naturalOrder());
    return lastDiskUpdate.isPresent()
        && DateUtils.addHours(lastDiskUpdate.get(), cooldownInHours).after(new Date());
  }

  private static boolean checkDiskChanged(
      UserIntent currentUserIntent,
      UserIntent newUserIntent,
      Function<UserIntent, DeviceInfo> getter,
      Consumer<String> errorConsumer,
      boolean verifyVolumeSize) {
    DeviceInfo newDeviceInfo = getter.apply(newUserIntent);
    DeviceInfo currentDeviceInfo = getter.apply(currentUserIntent);

    // Disk will not be resized if the universe has no currently defined device info.
    if (currentDeviceInfo != null && newDeviceInfo != null) {
      DeviceInfo currentDeviceInfoCloned = currentDeviceInfo.clone();
      if (newDeviceInfo.volumeSize != null) {
        if (verifyVolumeSize && currentDeviceInfo.volumeSize > newDeviceInfo.volumeSize) {
          errorConsumer.accept(
              "Disk size cannot be decreased. It was "
                  + currentDeviceInfo.volumeSize
                  + " got "
                  + newDeviceInfo.volumeSize);
          return true;
        }
        currentDeviceInfoCloned.volumeSize = newDeviceInfo.volumeSize;
      }
      if (!Objects.equals(newDeviceInfo.diskIops, currentDeviceInfo.diskIops)) {
        if (newDeviceInfo.diskIops == null) {
          newDeviceInfo.diskIops = currentDeviceInfo.diskIops;
        }
        if (currentUserIntent.providerType != Common.CloudType.aws) {
          errorConsumer.accept("Disk IOPS provisioning is only supported for AWS");
          return true;
        }
        if (!currentDeviceInfo.storageType.isIopsProvisioning()) {
          errorConsumer.accept(
              "Disk IOPS provisioning is not allowed for storage type: "
                  + currentDeviceInfo.storageType);
          return true;
        }
        Pair<Integer, Integer> iopsRange = currentDeviceInfo.storageType.getIopsRange();
        if (newDeviceInfo.diskIops < iopsRange.getFirst()
            || newDeviceInfo.diskIops > iopsRange.getSecond()) {
          errorConsumer.accept(
              String.format(
                  "Disk IOPS value: %d is not in the acceptable range: %d - %d "
                      + "for storage type: %s",
                  newDeviceInfo.diskIops,
                  iopsRange.getFirst(),
                  iopsRange.getSecond(),
                  currentDeviceInfo.storageType));
          return true;
        }
        currentDeviceInfoCloned.diskIops = newDeviceInfo.diskIops;
      }
      if (!Objects.equals(newDeviceInfo.throughput, currentDeviceInfo.throughput)) {
        if (newDeviceInfo.throughput == null) {
          newDeviceInfo.throughput = currentDeviceInfo.throughput;
        }
        if (currentUserIntent.providerType != Common.CloudType.aws) {
          errorConsumer.accept("Disk Throughput provisioning is only supported for AWS");
          return true;
        }
        if (!currentDeviceInfo.storageType.isThroughputProvisioning()) {
          errorConsumer.accept(
              "Disk Throughput provisioning is not allowed for storage type: "
                  + currentDeviceInfo.storageType);
          return true;
        }
        Pair<Integer, Integer> throughputRange = currentDeviceInfo.storageType.getThroughputRange();
        if (newDeviceInfo.throughput < throughputRange.getFirst()
            || newDeviceInfo.throughput > throughputRange.getSecond()) {
          errorConsumer.accept(
              String.format(
                  "Disk Throughput (MiB/s) value: %d is not in the acceptable range: %d - %d"
                      + " for storage type: %s",
                  newDeviceInfo.throughput,
                  throughputRange.getFirst(),
                  throughputRange.getSecond(),
                  currentDeviceInfo.storageType));
          return true;
        }
        currentDeviceInfoCloned.throughput = newDeviceInfo.throughput;
      }

      if (!newDeviceInfo.equals(currentDeviceInfoCloned)) {
        errorConsumer.accept(
            "Smart resize only supports modifying volumeSize, diskIops, throughput");
      }
      return !currentDeviceInfo.equals(currentDeviceInfoCloned);
    }
    return false;
  }

  private static boolean checkInstanceTypeChanged(
      UserIntent currentUserIntent,
      UserIntent newUserIntent,
      Consumer<String> errorConsumer,
      List<NodeDetails> nodes,
      boolean diskChanged,
      boolean allowUnsupportedInstances) {
    Map<String, InstanceType> instanceTypeMap = new HashMap<>();
    boolean result = false;
    boolean checkEphemeral = diskChanged;
    for (NodeDetails node : nodes) {
      String newInstanceTypeCode = newUserIntent.getInstanceTypeForNode(node);
      String currentInstanceTypeCode = currentUserIntent.getInstanceTypeForNode(node);
      if (!Objects.equals(newInstanceTypeCode, currentInstanceTypeCode)) {
        if (!instanceTypeMap.containsKey(newInstanceTypeCode)) {
          InstanceType instanceType =
              getInstanceType(currentUserIntent, newInstanceTypeCode, allowUnsupportedInstances);
          instanceTypeMap.put(newInstanceTypeCode, instanceType);
          if (instanceType == null) {
            errorConsumer.accept(
                String.format(
                    "Provider %s of type %s does not contain the intended instance type '%s'",
                    currentUserIntent.provider,
                    currentUserIntent.providerType,
                    newInstanceTypeCode));
            return true;
          }
          if (currentUserIntent.providerType == Common.CloudType.azu
              && isAzureWithLocalDisk(currentInstanceTypeCode)
                  != isAzureWithLocalDisk(newInstanceTypeCode)) {
            errorConsumer.accept(
                String.format(
                    "Cannot switch between instances with and without local disk (%s and %s)",
                    currentInstanceTypeCode, newInstanceTypeCode));
          }
        }
        checkEphemeral = true;
        result = true;
      }
      DeviceInfo curDeviceInfo = currentUserIntent.getDeviceInfoForNode(node);
      log.debug(
          "Check {} provType {} inst {} device {}",
          checkEphemeral,
          currentUserIntent.providerType,
          currentInstanceTypeCode,
          curDeviceInfo);
      if (checkEphemeral
          && hasEphemeralStorage(
              currentUserIntent.providerType, currentInstanceTypeCode, curDeviceInfo)) {
        errorConsumer.accept(
            "ResizeNode operation is not supported for instances with ephemeral drives");
        return result;
      }
    }
    return result;
  }

  private static InstanceType getInstanceType(
      UserIntent userIntent, String instanceTypeCode, boolean allowUnsupportedInstances) {
    String provider = userIntent.provider;
    List<InstanceType> instanceTypes =
        InstanceType.findByProvider(
            Provider.getOrBadRequest(UUID.fromString(provider)),
            StaticInjectorHolder.injector().instanceOf(Config.class),
            allowUnsupportedInstances);
    return instanceTypes.stream()
        .filter(type -> type.getInstanceTypeCode().equals(instanceTypeCode))
        .findFirst()
        .orElse(null);
  }

  private static boolean isAzureWithLocalDisk(String instanceType) {
    return AZU_NO_LOCAL_DISK.matcher(instanceType).matches();
  }

  public boolean flagsProvided(Universe universe) {
    for (Cluster newCluster : clusters) {
      Cluster oldCluster = universe.getCluster(newCluster.uuid);
      if (!Objects.equals(
          oldCluster.userIntent.specificGFlags, newCluster.userIntent.specificGFlags)) {
        return true;
      }
    }
    // If one is present, we know the other must be present.
    return masterGFlags != null;
  }

  public static class Converter extends BaseConverter<ResizeNodeParams> {}
}
