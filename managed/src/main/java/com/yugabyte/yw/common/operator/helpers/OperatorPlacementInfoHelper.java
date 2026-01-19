// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator.helpers;

import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

/** Helper class to create PlacementInfo from CRD */
@Slf4j
public class OperatorPlacementInfoHelper {

  public interface ZoneInfo {
    String getCode();

    Integer getNumNodes();

    Boolean getPreferred();
  }

  public interface RegionInfo {
    String getCode();

    List<? extends ZoneInfo> getZones();
  }

  public interface PlacementInfoAdapter {
    String getDefaultRegion();

    List<? extends RegionInfo> getRegions();
  }

  /** Adapter for primary cluster placementInfo. */
  public static class PrimaryPlacementAdapter implements PlacementInfoAdapter {
    private final io.yugabyte.operator.v1alpha1.ybuniversespec.PlacementInfo placement;

    public PrimaryPlacementAdapter(
        io.yugabyte.operator.v1alpha1.ybuniversespec.PlacementInfo placement) {
      super();
      this.placement = placement;
    }

    @Override
    public String getDefaultRegion() {
      return placement.getDefaultRegion();
    }

    @Override
    public List<RegionInfo> getRegions() {
      return placement.getRegions().stream()
          .filter(Objects::nonNull)
          .map(PrimaryRegionAdapter::new)
          .collect(Collectors.toList());
    }

    private static class PrimaryRegionAdapter implements RegionInfo {
      private final io.yugabyte.operator.v1alpha1.ybuniversespec.placementinfo.Regions region;

      PrimaryRegionAdapter(
          io.yugabyte.operator.v1alpha1.ybuniversespec.placementinfo.Regions region) {
        super();
        this.region = region;
      }

      @Override
      public String getCode() {
        return region.getCode();
      }

      @Override
      public List<ZoneInfo> getZones() {
        return region.getZones().stream()
            .filter(Objects::nonNull)
            .map(PrimaryZoneAdapter::new)
            .collect(Collectors.toList());
      }

      private static class PrimaryZoneAdapter implements ZoneInfo {
        private final io.yugabyte.operator.v1alpha1.ybuniversespec.placementinfo.regions.Zones zone;

        PrimaryZoneAdapter(
            io.yugabyte.operator.v1alpha1.ybuniversespec.placementinfo.regions.Zones zone) {
          super();
          this.zone = zone;
        }

        @Override
        public String getCode() {
          return zone.getCode();
        }

        @Override
        public Integer getNumNodes() {
          return zone.getNumNodes().intValue();
        }

        @Override
        public Boolean getPreferred() {
          return zone.getPreferred();
        }
      }
    }
  }

  /** Adapter for readReplica cluster placementInfo. */
  public static class ReadReplicaPlacementAdapter implements PlacementInfoAdapter {
    private final io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.PlacementInfo placement;

    public ReadReplicaPlacementAdapter(
        io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.PlacementInfo placement) {
      super();
      this.placement = placement;
    }

    @Override
    public String getDefaultRegion() {
      return null; // Read replicas don't have a default region
    }

    @Override
    public List<RegionInfo> getRegions() {
      return placement.getRegions().stream()
          .filter(Objects::nonNull)
          .map(ReadReplicaRegionAdapter::new)
          .collect(Collectors.toList());
    }

    private static class ReadReplicaRegionAdapter implements RegionInfo {
      private final io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.placementinfo.Regions
          region;

      ReadReplicaRegionAdapter(
          io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.placementinfo.Regions region) {
        super();
        this.region = region;
      }

      @Override
      public String getCode() {
        return region.getCode();
      }

      @Override
      public List<ZoneInfo> getZones() {
        return region.getZones().stream()
            .filter(Objects::nonNull)
            .map(ReadReplicaZoneAdapter::new)
            .collect(Collectors.toList());
      }

      private static class ReadReplicaZoneAdapter implements ZoneInfo {
        private final io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.placementinfo.regions
                .Zones
            zone;

        ReadReplicaZoneAdapter(
            io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.placementinfo.regions.Zones
                zone) {
          super();
          this.zone = zone;
        }

        @Override
        public String getCode() {
          return zone.getCode();
        }

        @Override
        public Integer getNumNodes() {
          return zone.getNumNodes().intValue();
        }

        @Override
        public Boolean getPreferred() {
          return false; /* Read replicas don't have preferred zones */
        }
      }
    }
  }

  public static PlacementInfo createPlacementInfo(
      io.yugabyte.operator.v1alpha1.ybuniversespec.PlacementInfo placement, Provider provider) {
    return createPlacementInfo(
        new OperatorPlacementInfoHelper.PrimaryPlacementAdapter(placement), provider);
  }

  public static PlacementInfo createPlacementInfo(
      io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.PlacementInfo placement,
      Provider provider) {
    return createPlacementInfo(
        new OperatorPlacementInfoHelper.ReadReplicaPlacementAdapter(placement), provider);
  }

  /**
   * Creates a PlacementInfo object from CRD placement information and provider details.
   *
   * @param crPlacementInfo The placement information from the CRD
   * @param provider The provider containing region and zone definitions
   * @return A PlacementInfo object ready for universe creation
   * @throws IllegalArgumentException if validation fails
   */
  private static PlacementInfo createPlacementInfo(
      PlacementInfoAdapter crPlacementInfo, Provider provider) {
    PlacementInfo placementInfo = new PlacementInfo();
    PlacementInfo.PlacementCloud placementCloud = new PlacementInfo.PlacementCloud();
    placementCloud.uuid = provider.getUuid();
    placementCloud.code = provider.getCode();

    // Handle default region
    String defaultRegion = crPlacementInfo.getDefaultRegion();
    if (defaultRegion != null) {
      provider.getRegions().stream()
          .filter(r -> r.getCode().equals(defaultRegion))
          .findFirst()
          .ifPresent(r -> placementCloud.defaultRegion = r.getUuid());
    }

    // Process regions with validation
    List<? extends RegionInfo> crRegions = crPlacementInfo.getRegions();
    if (crRegions == null || crRegions.isEmpty()) {
      String errorMsg = "No regions specified in CR placement info";
      log.error(errorMsg);
      throw new IllegalArgumentException(errorMsg);
    }

    // Process regions
    for (RegionInfo crRegion : crPlacementInfo.getRegions()) {
      PlacementInfo.PlacementRegion placementRegion = new PlacementInfo.PlacementRegion();
      placementRegion.code = crRegion.getCode();

      // Find matching provider region
      Region providerRegion =
          provider.getRegions().stream()
              .filter(r -> crRegion.getCode().equals(r.getCode()))
              .findFirst()
              .orElseThrow(
                  () -> {
                    String errorMsg =
                        String.format(
                            "Region '%s' not found in provider '%s'",
                            crRegion.getCode(), provider.getName());
                    log.error(errorMsg);
                    return new IllegalArgumentException(errorMsg);
                  });

      placementRegion.uuid = providerRegion.getUuid();
      placementRegion.name = providerRegion.getName();

      // Process zones
      for (ZoneInfo crZone : crRegion.getZones()) {
        PlacementInfo.PlacementAZ placementAZ = new PlacementInfo.PlacementAZ();
        placementAZ.name = crZone.getCode();
        placementAZ.numNodesInAZ = crZone.getNumNodes();
        placementAZ.isAffinitized = crZone.getPreferred() != null ? crZone.getPreferred() : true;
        placementAZ.leaderPreference = placementAZ.isAffinitized ? 1 : 0;

        // Find matching provider zone
        AvailabilityZone providerZone =
            providerRegion.getZones().stream()
                .filter(z -> crZone.getCode().equals(z.getCode()))
                .findFirst()
                .orElseThrow(
                    () -> {
                      String errorMsg =
                          String.format(
                              "Zone '%s' not found in region '%s'",
                              crZone.getCode(), crRegion.getCode());
                      log.error(errorMsg);
                      return new IllegalArgumentException(errorMsg);
                    });

        placementAZ.uuid = providerZone.getUuid();
        placementRegion.azList.add(placementAZ);
      }

      // Validate region has valid zones
      if (placementRegion.azList.isEmpty()) {
        String errorMsg =
            String.format("Region '%s' has no valid zones after processing", placementRegion.code);
        log.error(errorMsg);
        throw new IllegalArgumentException(errorMsg);
      }
      placementCloud.regionList.add(placementRegion);
    }
    if (placementCloud.regionList.isEmpty()) {
      String errorMsg = "No valid regions found after processing CR placement info";
      log.error(errorMsg);
      throw new IllegalArgumentException(errorMsg);
    }
    placementInfo.cloudList.add(placementCloud);
    return placementInfo;
  }

  /*
   * Validate that total nodes in placementInfo matches the number of nodes in the CR
   */
  public static void verifyPlacementInfo(PlacementInfo placementInfo, int crNumNodes) {
    // Validate total nodes across zones matches the CR specification
    int totalNodesInPlacement =
        placementInfo.cloudList.stream()
            .flatMap(cloud -> cloud.regionList.stream())
            .flatMap(region -> region.azList.stream())
            .mapToInt(zone -> zone.numNodesInAZ)
            .sum();
    if (totalNodesInPlacement != crNumNodes) {
      String errorMsg =
          String.format(
              "Total nodes in placementInfo (%d) does not match numNodes in CR (%d)",
              totalNodesInPlacement, crNumNodes);
      log.error(errorMsg);
      throw new IllegalArgumentException(errorMsg);
    }
  }

  /*
   * Check if placement info has changed between old and new placement info
   */
  public static boolean checkIfPlacementInfoChanged(
      PlacementInfo oldPlacementInfo, YBUniverse ybUniverse, boolean isReadOnlyCluster) {
    // If placement info is not set in the CR, return false
    if (isReadOnlyCluster
        ? ybUniverse.getSpec().getReadReplica().getPlacementInfo() == null
        : ybUniverse.getSpec().getPlacementInfo() == null) {
      return false;
    }

    PlacementInfoAdapter newPlacementAdapter =
        isReadOnlyCluster
            ? new ReadReplicaPlacementAdapter(
                ybUniverse.getSpec().getReadReplica().getPlacementInfo())
            : new PrimaryPlacementAdapter(ybUniverse.getSpec().getPlacementInfo());

    return !arePlacementInfosEqual(oldPlacementInfo, newPlacementAdapter);
  }

  /** Compare old PlacementInfo with new PlacementInfoAdapter to detect changes */
  private static boolean arePlacementInfosEqual(
      PlacementInfo oldPlacementInfo, PlacementInfoAdapter newPlacementAdapter) {
    log.debug("oldPlacementInfo: {}", oldPlacementInfo.toString());
    if (oldPlacementInfo == null || oldPlacementInfo.cloudList.isEmpty()) {
      log.debug("oldPlacementInfo is null or empty");
      return false;
    }

    PlacementInfo.PlacementCloud oldCloud = oldPlacementInfo.cloudList.get(0);

    // Compare regions
    List<? extends RegionInfo> newRegions = newPlacementAdapter.getRegions();
    if (newRegions == null || newRegions.isEmpty()) {
      log.debug("newRegions is null or empty");
      return oldCloud.regionList.isEmpty();
    }

    // Create maps for comparison
    Map<String, PlacementInfo.PlacementRegion> oldRegionMap =
        oldCloud.regionList.stream().collect(Collectors.toMap(r -> r.code, r -> r));

    Map<String, RegionInfo> newRegionMap =
        newRegions.stream().collect(Collectors.toMap(RegionInfo::getCode, r -> r));

    // Check if region sets are equal
    if (!oldRegionMap.keySet().equals(newRegionMap.keySet())) {
      log.debug(
          "oldRegionMap keys: {}, newRegionMap keys: {}",
          oldRegionMap.keySet(),
          newRegionMap.keySet());
      return false;
    }

    // Compare each region's zones
    for (String regionCode : oldRegionMap.keySet()) {
      PlacementInfo.PlacementRegion oldRegion = oldRegionMap.get(regionCode);
      RegionInfo newRegion = newRegionMap.get(regionCode);

      if (!areRegionsEqual(oldRegion, newRegion)) {
        log.debug("oldRegion: {}, newRegion: {}", oldRegion, newRegion);
        return false;
      }
    }

    return true;
  }

  /** Compare old PlacementRegion with new RegionInfo */
  private static boolean areRegionsEqual(
      PlacementInfo.PlacementRegion oldRegion, RegionInfo newRegion) {
    List<? extends ZoneInfo> newZones = newRegion.getZones();
    if (newZones == null || newZones.isEmpty()) {
      return oldRegion.azList.isEmpty();
    }

    // Create maps for comparison
    Map<String, PlacementInfo.PlacementAZ> oldZoneMap =
        oldRegion.azList.stream().collect(Collectors.toMap(z -> z.name, z -> z));

    Map<String, ZoneInfo> newZoneMap =
        newZones.stream().collect(Collectors.toMap(ZoneInfo::getCode, z -> z));

    // Check if zone sets are equal
    if (!oldZoneMap.keySet().equals(newZoneMap.keySet())) {
      log.debug(
          "oldZoneMap keys: {}, newZoneMap keys: {}", oldZoneMap.keySet(), newZoneMap.keySet());
      return false;
    }

    // Compare each zone's properties
    for (String zoneCode : oldZoneMap.keySet()) {
      PlacementInfo.PlacementAZ oldZone = oldZoneMap.get(zoneCode);
      ZoneInfo newZone = newZoneMap.get(zoneCode);

      if (!areZonesEqual(oldZone, newZone)) {
        log.debug("oldZone: {}, newZone: {}", oldZone, newZone);
        return false;
      }
    }
    return true;
  }

  /** Compare old PlacementAZ with new ZoneInfo */
  private static boolean areZonesEqual(PlacementInfo.PlacementAZ oldZone, ZoneInfo newZone) {
    // Compare number of nodes
    if (oldZone.numNodesInAZ != newZone.getNumNodes()) {
      log.debug(
          "oldZone.numNodesInAZ: {}, newZone.getNumNodes: {}",
          oldZone.numNodesInAZ,
          newZone.getNumNodes());
      return false;
    }

    // Compare preferred/affinitized status
    boolean oldPreferred = oldZone.isAffinitized;
    boolean newPreferred = newZone.getPreferred() != null ? newZone.getPreferred() : true;

    log.debug("oldPreferred: {}, newPreferred: {}", oldPreferred, newPreferred);
    return oldPreferred == newPreferred;
  }
}
