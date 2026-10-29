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
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.ManagedLoadBalancer;
import com.yugabyte.yw.models.helpers.ManagedLoadBalancerState;
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
import java.util.TreeSet;
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
   * <p>A region's load balancer is named {@link #getPrivateName(UUID, Region)}, unless YBA took
   * over a load balancer that the user created: then it keeps that name, which the saved state
   * records. Validation keeps lbName out of a cluster with a managed load balancer, except in the
   * edit that turns it on. There, the lbName that the zones of a region set names the load balancer
   * that YBA takes over.
   *
   * @return nothing for a null cluster or one without a managed load balancer.
   */
  public static List<ManagedLoadBalancer> planLoadBalancers(
      @Nullable Cluster cluster, @Nullable ManagedLoadBalancerState state) {
    PlacementInfo placement = cluster == null ? null : cluster.getOverallPlacement();
    // validateNewCluster rejects enablePublic until PLAT-22813 adds public load balancers.
    if (placement == null || !isPrivateEnabled(cluster)) {
      return Collections.emptyList();
    }
    Map<UUID, String> names = new HashMap<>();
    if (state != null) {
      state.getLoadBalancers().stream()
          .filter(lb -> lb.getClusterUuid().equals(cluster.uuid))
          .filter(lb -> lb.getScheme() == ManagedLoadBalancer.Scheme.PRIVATE)
          .forEach(lb -> names.putIfAbsent(lb.getRegionUuid(), lb.getName()));
    }
    names.putAll(getTakeoverNames(cluster));
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
                names.getOrDefault(region.uuid, getPrivateName(cluster.uuid, regionModel)),
                null));
      }
    }
    return lbs;
  }

  /**
   * The load balancers that the zones of the cluster name with lbName, by region UUID. YBA takes
   * them over: it keeps their names, and so their addresses, instead of creating new ones. A region
   * whose zones set no lbName gets no entry.
   *
   * @throws PlatformServiceException when the zones of a region do not all set the same lbName,
   *     because one managed load balancer serves every zone of its region.
   */
  private static Map<UUID, String> getTakeoverNames(Cluster cluster) {
    Map<UUID, Set<String>> namesByRegion = new HashMap<>();
    Map<UUID, String> regionCodes = new HashMap<>();
    getPlacements(cluster)
        .flatMap(placement -> placement.cloudList.stream())
        .flatMap(cloud -> cloud.regionList.stream())
        .forEach(
            region -> {
              regionCodes.put(region.uuid, region.code);
              Set<String> names = namesByRegion.computeIfAbsent(region.uuid, r -> new TreeSet<>());
              region.azList.forEach(az -> names.add(StringUtils.defaultString(az.lbName)));
            });
    Map<UUID, String> takeoverNames = new HashMap<>();
    namesByRegion.forEach(
        (regionUuid, names) -> {
          if (names.size() > 1) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "The zones of region %s set different load balancers: %s. Set the same lbName"
                        + " in every zone of the region, or none.",
                    regionCodes.get(regionUuid),
                    names.stream()
                        .map(name -> name.isEmpty() ? "(none)" : name)
                        .collect(Collectors.joining(", "))));
          }
          names.stream()
              .filter(StringUtils::isNotEmpty)
              .findFirst()
              .ifPresent(name -> takeoverNames.put(regionUuid, name));
        });
    return takeoverNames;
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
    validateCanEnable(cluster, confGetter, cloudAPIFactory);
  }

  private static void validateCanEnable(
      Cluster cluster, RuntimeConfGetter confGetter, CloudAPI.Factory cloudAPIFactory) {
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
   * Rejects an edit that changes the managed load balancer in a way that YBA cannot carry out, or
   * that adds a load balancer name to a universe with managed load balancers. An edit can turn the
   * managed load balancer of the primary cluster on or off. When it turns it on, the zones of each
   * region may set one lbName, which names the load balancer that YBA takes over.
   *
   * @param primaryAfterEdit the primary cluster as the edit leaves it.
   */
  public static void validateEditedCluster(
      Universe universe,
      @Nullable Cluster primaryAfterEdit,
      Cluster current,
      Cluster updated,
      RuntimeConfGetter confGetter,
      CloudAPI.Factory cloudAPIFactory) {
    validateEnablesSomething(updated);
    if (!isEnabled(current) && isEnabled(updated)) {
      validateCanEnable(updated, confGetter, cloudAPIFactory);
      // Only the primary cluster's load balancers can be taken over.
      for (Cluster other : universe.getUniverseDetails().clusters) {
        if (!other.uuid.equals(updated.uuid)) {
          validateNoLbNames(updated, other);
        }
      }
      getTakeoverNames(updated);
      return;
    }
    if (isPublicEnabled(current) != isPublicEnabled(updated)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Public load balancers are not supported yet");
    }
    validateNoLbNames(primaryAfterEdit, updated);
  }

  /** Whether the edit turns the managed load balancer on or off. */
  public static boolean isTurnedOnOrOff(Cluster current, Cluster updated) {
    return isPrivateEnabled(current) != isPrivateEnabled(updated)
        || isPublicEnabled(current) != isPublicEnabled(updated);
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
    getPlacements(cluster)
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

  public static boolean setsLbName(Cluster cluster) {
    return getPlacements(cluster)
        .flatMap(PlacementInfo::azStream)
        .anyMatch(az -> StringUtils.isNotEmpty(az.lbName));
  }

  /**
   * Clears lbName and enableLB, which named the load balancers that YBA took over when the cluster
   * turned on its managed load balancer.
   */
  public static void clearTakenOverNames(Cluster cluster) {
    cluster.userIntent.enableLB = false;
    getPlacements(cluster).flatMap(PlacementInfo::azStream).forEach(az -> az.lbName = null);
  }

  /** Whether a zone of the cluster in the load balancer's region sets the load balancer's name. */
  public static boolean isNamedByZones(@Nullable Cluster cluster, ManagedLoadBalancer lb) {
    return cluster != null
        && getPlacements(cluster)
            .flatMap(placement -> placement.cloudList.stream())
            .flatMap(cloud -> cloud.regionList.stream())
            .filter(region -> lb.getRegionUuid().equals(region.uuid))
            .flatMap(region -> region.azList.stream())
            .anyMatch(az -> lb.getName().equals(az.lbName));
  }

  /** The placement and the partition placements. getOverallPlacement() drops lbName. */
  private static Stream<PlacementInfo> getPlacements(Cluster cluster) {
    return Stream.concat(
            Stream.ofNullable(cluster.placementInfo),
            CollectionUtils.emptyIfNull(cluster.getPartitions()).stream()
                .map(UniverseDefinitionTaskParams.PartitionInfo::getPlacement))
        .filter(Objects::nonNull);
  }
}
