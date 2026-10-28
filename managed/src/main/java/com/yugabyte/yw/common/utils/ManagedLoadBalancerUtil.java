// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.utils;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.ManagedLoadBalancerConfig;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.ManagedLoadBalancer;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.annotation.Nullable;
import org.apache.commons.codec.binary.Base32;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

public class ManagedLoadBalancerUtil {

  // "lb-" is reserved for public load balancers.
  public static final String PRIVATE_NAME_PREFIX = "lbi-";

  /**
   * Returns the lowercase, unpadded base32 form of the UUID (26 characters). YBM's CommonUtil uses
   * the same scheme.
   */
  public static String base32Id(UUID uuid) {
    ByteBuffer bytes = ByteBuffer.allocate(16);
    bytes.putLong(uuid.getMostSignificantBits());
    bytes.putLong(uuid.getLeastSignificantBits());
    return new Base32().encodeToString(bytes.array()).toLowerCase().replace("=", "");
  }

  /**
   * The name of the cluster's load balancer in each region; the cloud scopes names to a region. It
   * is under the AWS limit of 32 characters, and it uses the cluster UUID so that a read replica
   * cluster gets its own load balancer. Several load balancers in one region need a name that also
   * tells their zones apart.
   */
  public static String getPrivateName(UUID clusterUUID) {
    return PRIVATE_NAME_PREFIX + base32Id(clusterUUID);
  }

  /**
   * {@link #getPrivateName(UUID)}, with the region code added on Azure. Azure scopes names to a
   * resource group, and one resource group can hold every region of the cluster.
   */
  public static String getPrivateName(UUID clusterUUID, Region region) {
    String name = getPrivateName(clusterUUID);
    return region.getProviderCloudCode() == CloudType.azu ? name + "-" + region.getCode() : name;
  }

  /** The first zone that has a subnet, so that a retry picks the same one. */
  public static AvailabilityZone getFirstZoneWithSubnet(
      List<AvailabilityZone> zones, String regionCode, String lbName) {
    return zones.stream()
        .filter(zone -> StringUtils.isNotBlank(zone.getSubnet()))
        .findFirst()
        .orElseThrow(
            () ->
                new PlatformServiceException(
                    BAD_REQUEST,
                    "No zone of region "
                        + regionCode
                        + " has a subnet for load balancer "
                        + lbName));
  }

  /** Whether the cluster asks for any load balancer that YBA manages. */
  public static boolean isEnabled(Cluster cluster) {
    return cluster.userIntent.isManagedLoadBalancerEnabled();
  }

  public static boolean isPrivateEnabled(Cluster cluster) {
    ManagedLoadBalancerConfig config = cluster.userIntent.getManagedLoadBalancer();
    return config != null && config.isEnablePrivate();
  }

  public static boolean isPublicEnabled(Cluster cluster) {
    ManagedLoadBalancerConfig config = cluster.userIntent.getManagedLoadBalancer();
    return config != null && config.isEnablePublic();
  }

  /**
   * The load balancers that the cluster calls for: one private load balancer in each region of the
   * placement when enablePrivate is set, serving every active zone of the region. The tasks create,
   * reconcile and delete what this returns, and a node registers with the load balancer that serves
   * its zone. A public load balancer (PLAT-22813) adds its entries here and its cloud resources to
   * the CloudAPI implementations. A load balancer for each zone, or several for a region that each
   * serve some of its zones, changes this method and {@link #getPrivateName}, and nothing else.
   *
   * @return nothing for a null cluster or one without a managed load balancer.
   */
  public static List<ManagedLoadBalancer> planLoadBalancers(@Nullable Cluster cluster) {
    PlacementInfo placement = cluster == null ? null : cluster.getOverallPlacement();
    // validateNewCluster rejects enablePublic until PLAT-22813 adds public load balancers.
    if (placement == null || !isPrivateEnabled(cluster)) {
      return Collections.emptyList();
    }
    List<ManagedLoadBalancer> lbs = new ArrayList<>();
    Set<UUID> regionUuids = new HashSet<>();
    for (PlacementInfo.PlacementCloud cloud : placement.cloudList) {
      for (PlacementInfo.PlacementRegion region : cloud.regionList) {
        if (!regionUuids.add(region.uuid)) {
          continue;
        }
        Region regionModel = Region.getOrBadRequest(region.uuid);
        // getZones() leaves out inactive zones. Sorted, so that the saved state is stable.
        List<UUID> azUuids =
            regionModel.getZones().stream()
                .sorted(Comparator.comparing(AvailabilityZone::getCode))
                .map(AvailabilityZone::getUuid)
                .collect(Collectors.toList());
        lbs.add(
            new ManagedLoadBalancer(
                cluster.uuid,
                region.uuid,
                ManagedLoadBalancer.Scheme.PRIVATE,
                azUuids,
                getPrivateName(cluster.uuid, regionModel),
                null));
      }
    }
    return lbs;
  }

  public static Map<String, String> getTags(
      UUID universeUUID, String universeName, UUID customerUUID, Map<String, String> instanceTags) {
    Map<String, String> tags = new HashMap<>();
    if (instanceTags != null) {
      tags.putAll(instanceTags);
    }
    tags.put("universe-uuid", universeUUID.toString());
    tags.put("universe-name", universeName);
    tags.put("customer-uuid", customerUUID.toString());
    return tags;
  }

  /** Rejects a new cluster that asks for a managed load balancer that YBA cannot create. */
  public static void validateNewCluster(
      Cluster cluster,
      @Nullable Cluster primaryCluster,
      RuntimeConfGetter confGetter,
      CloudAPI.Factory cloudAPIFactory) {
    validateNoLbNames(primaryCluster, cluster);
    validateEnablesSomething(cluster);
    if (!isEnabled(cluster)) {
      return;
    }
    if (cluster.clusterType != ClusterType.PRIMARY) {
      throw new PlatformServiceException(
          BAD_REQUEST, "A managed load balancer is available only for the primary cluster");
    }
    // A flag that YBA accepted and ignored would mislead.
    if (isPublicEnabled(cluster)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Public load balancers are not supported yet");
    }
    for (UUID providerUUID : cluster.userIntent.getAllProviderUUIDs()) {
      Provider provider = Provider.getOrBadRequest(providerUUID);
      if (!confGetter.getConfForScope(provider, ProviderConfKeys.managedLoadBalancerEnabled)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Managed load balancers are in preview. Set the runtime flag '%s' to true for"
                    + " provider '%s' to use them.",
                ProviderConfKeys.managedLoadBalancerEnabled.getKey(), provider.getName()));
      }
      CloudAPI cloudAPI = cloudAPIFactory.get(provider.getCloudCode().name());
      if (cloudAPI == null || !cloudAPI.supportsManagedLoadBalancer()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Managed load balancers are not supported for "
                + provider.getCloudCode()
                + " providers");
      }
    }
    if (!cluster.userIntent.enableYSQL && !cluster.userIntent.enableYCQL) {
      throw new PlatformServiceException(
          BAD_REQUEST, "A managed load balancer needs YSQL or YCQL to be enabled");
    }
  }

  /**
   * Rejects an edit that changes which load balancers YBA manages, or that adds a load balancer
   * name to a universe with managed load balancers.
   */
  public static void validateEditedCluster(
      @Nullable Cluster primaryCluster, Cluster current, Cluster updated) {
    validateEnablesSomething(updated);
    if (isPrivateEnabled(current) != isPrivateEnabled(updated)
        || isPublicEnabled(current) != isPublicEnabled(updated)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "The managed load balancer settings cannot be changed after the cluster is created");
    }
    validateNoLbNames(primaryCluster, updated);
  }

  /** An empty managedLoadBalancer is a mistake, so YBA rejects it rather than ignores it. */
  private static void validateEnablesSomething(Cluster cluster) {
    ManagedLoadBalancerConfig config = cluster.userIntent.getManagedLoadBalancer();
    if (config != null && !config.isEnablePrivate() && !config.isEnablePublic()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "managedLoadBalancer enables no load balancer. Set enablePrivate or enablePublic, or"
              + " leave it out.");
    }
  }

  /**
   * A universe uses load balancers that YBA manages or load balancers that the user creates, never
   * both. The primary cluster decides which, because only it can have a managed load balancer.
   */
  public static void validateNoLbNames(@Nullable Cluster primaryCluster, Cluster cluster) {
    if (primaryCluster == null || !isEnabled(primaryCluster)) {
      return;
    }
    // getOverallPlacement() drops lbName, so read the placements directly.
    Stream.concat(
            Stream.ofNullable(cluster.placementInfo),
            CollectionUtils.emptyIfNull(cluster.getPartitions()).stream()
                .map(UniverseDefinitionTaskParams.PartitionInfo::getPlacement))
        .filter(Objects::nonNull)
        .flatMap(PlacementInfo::azStream)
        .filter(az -> StringUtils.isNotEmpty(az.lbName))
        .findFirst()
        .ifPresent(
            az -> {
              throw new PlatformServiceException(
                  BAD_REQUEST,
                  String.format(
                      "Availability zone %s sets load balancer %s. A universe with a managed"
                          + " load balancer cannot also use its own load balancers.",
                      az.name, az.lbName));
            });
  }
}
