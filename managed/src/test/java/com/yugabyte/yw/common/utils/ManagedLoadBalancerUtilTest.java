// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.utils;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.ManagedLoadBalancerConfig;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.ManagedLoadBalancer;
import com.yugabyte.yw.models.helpers.ManagedLoadBalancerState;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.Test;

public class ManagedLoadBalancerUtilTest extends FakeDBApplication {

  private static Cluster cluster(ClusterType clusterType, boolean managed, String lbName) {
    UserIntent userIntent = new UserIntent();
    if (managed) {
      ManagedLoadBalancerConfig lbConfig = new ManagedLoadBalancerConfig();
      lbConfig.setEnablePrivate(true);
      userIntent.setManagedLoadBalancer(lbConfig);
    }
    Cluster cluster = new Cluster(clusterType, userIntent);
    PlacementInfo.PlacementAZ az = new PlacementInfo.PlacementAZ();
    az.name = "az-1";
    az.lbName = lbName;
    PlacementInfo.PlacementRegion region = new PlacementInfo.PlacementRegion();
    region.azList.add(az);
    PlacementInfo.PlacementCloud cloud = new PlacementInfo.PlacementCloud();
    cloud.regionList.add(region);
    cluster.placementInfo = new PlacementInfo();
    cluster.placementInfo.cloudList.add(cloud);
    return cluster;
  }

  @Test
  public void testPrivateNameMatchesYbmName() {
    // A changed name orphans the load balancers of existing universes.
    // YBM's CommonUtil.uuidToBase32Unpadded gives h6uf6zcxc5cwfm74fsld6zvpuy for this UUID.
    assertEquals(
        "lbi-h6uf6zcxc5cwfm74fsld6zvpuy",
        ManagedLoadBalancerUtil.getPrivateName(
            UUID.fromString("3fa85f64-5717-4562-b3fc-2c963f66afa6")));
  }

  @Test
  public void testPlanGivesEachRegionOneLoadBalancerThatServesEveryActiveZone() {
    Customer customer = ModelFactory.testCustomer();
    Provider provider = ModelFactory.awsProvider(customer);
    Region r1 = Region.create(provider, "r1", "r1", "image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r1, "r1a", "r1a", "subnet-1a");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r1, "r1b", "r1b", "subnet-1b");
    AvailabilityZone inactive = AvailabilityZone.createOrThrow(r1, "r1c", "r1c", "subnet-1c");
    inactive.setActive(false);
    inactive.save();
    Region r2 = Region.create(provider, "r2", "r2", "image");
    AvailabilityZone az3 = AvailabilityZone.createOrThrow(r2, "r2a", "r2a", "subnet-2a");
    Cluster cluster = cluster(ClusterType.PRIMARY, true, null);
    cluster.uuid = UUID.randomUUID();
    // Nodes in az1 only: the load balancer still serves the other active zone of r1.
    cluster.placementInfo = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), cluster.placementInfo);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), cluster.placementInfo);

    List<ManagedLoadBalancer> lbs = ManagedLoadBalancerUtil.planLoadBalancers(cluster, null);

    String name = ManagedLoadBalancerUtil.getPrivateName(cluster.uuid);
    assertEquals(
        List.of(
            new ManagedLoadBalancer(
                cluster.uuid,
                r1.getUuid(),
                ManagedLoadBalancer.Scheme.PRIVATE,
                List.of(az1.getUuid(), az2.getUuid()),
                name,
                null),
            new ManagedLoadBalancer(
                cluster.uuid,
                r2.getUuid(),
                ManagedLoadBalancer.Scheme.PRIVATE,
                List.of(az3.getUuid()),
                name,
                null)),
        lbs);
    assertEquals(
        List.of(),
        ManagedLoadBalancerUtil.planLoadBalancers(cluster(ClusterType.PRIMARY, false, null), null));
    assertEquals(List.of(), ManagedLoadBalancerUtil.planLoadBalancers(null, null));
  }

  @Test
  public void testPublicLoadBalancerIsRejectedUntilSupported() {
    Cluster cluster = cluster(ClusterType.PRIMARY, true, null);
    cluster.userIntent.getManagedLoadBalancer().setEnablePublic(true);

    PlatformServiceException e =
        assertThrows(PlatformServiceException.class, () -> validateNew(cluster, List.of(cluster)));

    assertEquals("Public load balancers are not supported yet", e.getMessage());
    cluster.userIntent.getManagedLoadBalancer().setEnablePrivate(false);
    assertEquals(List.of(), ManagedLoadBalancerUtil.planLoadBalancers(cluster, null));
  }

  @Test
  public void testReadReplicaCanHaveAManagedLoadBalancerButAnAddonClusterCannot() {
    Cluster readReplica = cluster(ClusterType.ASYNC, true, null);
    Cluster addon = cluster(ClusterType.ADDON, true, null);

    validateNew(readReplica, List.of(cluster(ClusterType.PRIMARY, false, null), readReplica));
    PlatformServiceException e =
        assertThrows(PlatformServiceException.class, () -> validateNew(addon, List.of(addon)));

    assertEquals(
        "A managed load balancer is available only for the primary and read replica clusters",
        e.getMessage());
  }

  private void validateNew(Cluster cluster, List<Cluster> clusters) {
    ManagedLoadBalancerUtil.validateNewCluster(
        cluster, clusters, app.injector().instanceOf(RuntimeConfGetter.class), mockCloudAPIFactory);
  }

  private Universe universe;

  // The universe holds the saved clusters; the edit changes some of them into the edited ones.
  private void validateEdit(List<Cluster> saved, List<Cluster> edited) {
    if (universe == null) {
      universe = ModelFactory.createUniverse(ModelFactory.testCustomer().getId());
    }
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.clusters = new ArrayList<>(saved);
              u.setUniverseDetails(details);
            });
    Map<UUID, Cluster> clustersAfterEdit = new LinkedHashMap<>();
    saved.forEach(c -> clustersAfterEdit.put(c.uuid, c));
    edited.forEach(c -> clustersAfterEdit.put(c.uuid, c));
    for (Cluster updated : edited) {
      ManagedLoadBalancerUtil.validateEditedCluster(
          universe,
          clustersAfterEdit.values(),
          universe.getCluster(updated.uuid),
          updated,
          app.injector().instanceOf(RuntimeConfGetter.class),
          mockCloudAPIFactory);
    }
  }

  private void validateEdit(Cluster current, Cluster updated) {
    updated.uuid = current.uuid;
    validateEdit(List.of(current), List.of(updated));
  }

  @Test
  public void testEditTurnsTheManagedLoadBalancerOnOrOffButCannotAddPublic() {
    Cluster on = cluster(ClusterType.PRIMARY, true, null);
    Cluster withPublic = cluster(ClusterType.PRIMARY, true, null);
    withPublic.userIntent.getManagedLoadBalancer().setEnablePublic(true);

    validateEdit(on, cluster(ClusterType.PRIMARY, false, null));
    validateEdit(cluster(ClusterType.PRIMARY, false, null), on);
    PlatformServiceException e =
        assertThrows(PlatformServiceException.class, () -> validateEdit(on, withPublic));
    assertEquals("Public load balancers are not supported yet", e.getMessage());
  }

  @Test
  public void testEditThatLeavesTheSettingOutKeepsIt() {
    ManagedLoadBalancerConfig on =
        cluster(ClusterType.PRIMARY, true, null).userIntent.getManagedLoadBalancer();
    ManagedLoadBalancerConfig off = new ManagedLoadBalancerConfig();

    // A client that predates the field must not turn the load balancer off.
    assertEquals(on, ManagedLoadBalancerUtil.getEditedSetting(on, null));
    assertEquals(null, ManagedLoadBalancerUtil.getEditedSetting(on, off));
    assertEquals(on, ManagedLoadBalancerUtil.getEditedSetting(null, on));
  }

  @Test
  public void testTurningOnTakesOverOneLbNamePerRegionThatTheUniverseHas() {
    Cluster current = cluster(ClusterType.PRIMARY, false, "byo-lb");
    UUID regionUuid = UUID.randomUUID();
    current.placementInfo.cloudList.get(0).regionList.get(0).uuid = regionUuid;
    // az-2 of the same region names no load balancer.
    Cluster updated = cluster(ClusterType.PRIMARY, true, "byo-lb");
    PlacementInfo.PlacementRegion region = updated.placementInfo.cloudList.get(0).regionList.get(0);
    region.uuid = regionUuid;
    region.code = "r1";
    PlacementInfo.PlacementAZ az2 = new PlacementInfo.PlacementAZ();
    az2.name = "az-2";
    region.azList.add(az2);

    PlatformServiceException mixed =
        assertThrows(PlatformServiceException.class, () -> validateEdit(current, updated));
    az2.lbName = "byo-lb";
    validateEdit(current, updated);
    // Nothing on the saved universe could name it until the takeover ends.
    region.uuid = UUID.randomUUID();
    PlatformServiceException newRegion =
        assertThrows(PlatformServiceException.class, () -> validateEdit(current, updated));

    assertEquals(
        "The zones of region r1 set different load balancers: (none), byo-lb. Set the same lbName"
            + " in every zone of the region, or none.",
        mixed.getMessage());
    assertEquals(
        "Region r1 is new in this edit. Take over its load balancer in a later edit.",
        newRegion.getMessage());
  }

  @Test
  public void testAnyManagedClusterKeepsLbNameOffEveryCluster() {
    Cluster managedPrimary = cluster(ClusterType.PRIMARY, true, null);
    Cluster managedPrimaryWithLbName = cluster(ClusterType.PRIMARY, true, "byo-lb");
    Cluster primaryWithLbName = cluster(ClusterType.PRIMARY, false, "byo-lb");
    Cluster managedReadReplica = cluster(ClusterType.ASYNC, true, null);
    Cluster readReplicaWithLbName = cluster(ClusterType.ASYNC, false, "rr-lb");

    // Create: a managed cluster's own zones, and the other cluster's.
    assertThrows(
        PlatformServiceException.class,
        () -> validateNew(managedPrimaryWithLbName, List.of(managedPrimaryWithLbName)));
    assertThrows(
        PlatformServiceException.class,
        () -> validateNew(managedReadReplica, List.of(primaryWithLbName, managedReadReplica)));
    assertThrows(
        PlatformServiceException.class,
        () -> validateNew(managedPrimary, List.of(managedPrimary, readReplicaWithLbName)));
    // UpdateLoadBalancerConfig on a universe whose read replica is managed.
    assertThrows(
        PlatformServiceException.class,
        () ->
            ManagedLoadBalancerUtil.validateNoLbNames(
                List.of(cluster(ClusterType.PRIMARY, false, null), managedReadReplica),
                primaryWithLbName));
    // An edit that names a load balancer on the primary of a universe with a managed read replica.
    Cluster primary = cluster(ClusterType.PRIMARY, false, null);
    Cluster editedPrimary = cluster(ClusterType.PRIMARY, false, "byo-lb");
    editedPrimary.uuid = primary.uuid;
    assertThrows(
        PlatformServiceException.class,
        () -> validateEdit(List.of(primary, managedReadReplica), List.of(editedPrimary)));
    // A universe without managed load balancers keeps using its own.
    validateNew(readReplicaWithLbName, List.of(primaryWithLbName, readReplicaWithLbName));
  }

  @Test
  public void testTakeoverOfBothClustersNeedsOneEditThatTurnsOnBoth() {
    Cluster primary = cluster(ClusterType.PRIMARY, false, "byo-lb");
    Cluster readReplica = cluster(ClusterType.ASYNC, false, "rr-lb");
    Cluster managedPrimary = cluster(ClusterType.PRIMARY, true, "byo-lb");
    managedPrimary.uuid = primary.uuid;
    Cluster managedReadReplica = cluster(ClusterType.ASYNC, true, "rr-lb");
    managedReadReplica.uuid = readReplica.uuid;

    validateEdit(List.of(primary, readReplica), List.of(managedPrimary, managedReadReplica));
    // Out of scope: one cluster at a time, as v1 edits do, mixes the two kinds.
    assertThrows(
        PlatformServiceException.class,
        () -> validateEdit(List.of(primary, readReplica), List.of(managedPrimary)));
  }

  private void saveLoadBalancer(Cluster cluster, UUID regionUuid, String name) {
    universe = ModelFactory.createUniverse(ModelFactory.testCustomer().getId());
    ManagedLoadBalancerState state = new ManagedLoadBalancerState();
    state.put(
        new ManagedLoadBalancer(
            cluster.uuid, regionUuid, ManagedLoadBalancer.Scheme.PRIVATE, List.of(), name, null));
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.setManagedLoadBalancerState(state);
              u.setUniverseDetails(details);
            });
  }

  // Deleting either cluster would delete a load balancer that both have.
  @Test
  public void testTwoClustersCannotTakeOverOneLoadBalancer() {
    Cluster primary = cluster(ClusterType.PRIMARY, false, "shared-lb");
    Cluster readReplica = cluster(ClusterType.ASYNC, false, "shared-lb");
    Cluster managedPrimary = cluster(ClusterType.PRIMARY, true, "shared-lb");
    managedPrimary.uuid = primary.uuid;
    Cluster managedReadReplica = cluster(ClusterType.ASYNC, true, "shared-lb");
    managedReadReplica.uuid = readReplica.uuid;
    Cluster readReplicaWithoutLb = cluster(ClusterType.ASYNC, false, null);
    readReplicaWithoutLb.uuid = readReplica.uuid;

    PlatformServiceException shared =
        assertThrows(
            PlatformServiceException.class,
            () ->
                validateEdit(
                    List.of(primary, readReplica), List.of(managedPrimary, managedReadReplica)));
    // One cluster takes it over, and the other leaves it in the same edit.
    validateEdit(List.of(primary, readReplica), List.of(managedPrimary, readReplicaWithoutLb));

    assertEquals(
        "Another cluster of the universe has load balancer shared-lb, so this cluster can't take"
            + " it over",
        shared.getMessage());
  }

  @Test
  public void testReadReplicaCannotTakeOverThePrimarysManagedLoadBalancer() {
    Cluster managedPrimary = cluster(ClusterType.PRIMARY, true, null);
    UUID regionUuid = UUID.randomUUID();
    Cluster readReplica = cluster(ClusterType.ASYNC, false, null);
    readReplica.placementInfo.cloudList.get(0).regionList.get(0).uuid = regionUuid;
    Cluster takeover = cluster(ClusterType.ASYNC, true, "lbi-primary");
    takeover.uuid = readReplica.uuid;
    takeover.placementInfo.cloudList.get(0).regionList.get(0).uuid = regionUuid;
    saveLoadBalancer(managedPrimary, regionUuid, "lbi-primary");

    assertThrows(
        PlatformServiceException.class,
        () -> validateEdit(List.of(managedPrimary, readReplica), List.of(takeover)));
  }

  @Test
  public void testNoClusterNamesALoadBalancerThatTurningOffDeletes() {
    Cluster primary = cluster(ClusterType.PRIMARY, false, null);
    Cluster managedReadReplica = cluster(ClusterType.ASYNC, true, null);
    saveLoadBalancer(managedReadReplica, UUID.randomUUID(), "lbi-rr");
    Cluster primaryWithLbName = cluster(ClusterType.PRIMARY, false, "lbi-rr");
    primaryWithLbName.uuid = primary.uuid;
    Cluster readReplicaOff = cluster(ClusterType.ASYNC, false, null);
    readReplicaOff.uuid = managedReadReplica.uuid;

    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () ->
                validateEdit(
                    List.of(primary, managedReadReplica),
                    List.of(primaryWithLbName, readReplicaOff)));

    assertEquals(
        "Turning off the managed load balancer deletes lbi-rr, so lbName can't name it",
        e.getMessage());
  }
}
