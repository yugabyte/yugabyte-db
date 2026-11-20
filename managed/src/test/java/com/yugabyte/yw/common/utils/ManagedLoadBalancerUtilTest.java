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
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.bouncycastle.asn1.x509.GeneralName;
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
        assertThrows(
            PlatformServiceException.class,
            () ->
                ManagedLoadBalancerUtil.validateNewCluster(
                    cluster,
                    cluster,
                    app.injector().instanceOf(RuntimeConfGetter.class),
                    mockCloudAPIFactory));

    assertEquals("Public load balancers are not supported yet", e.getMessage());
    cluster.userIntent.getManagedLoadBalancer().setEnablePrivate(false);
    assertEquals(List.of(), ManagedLoadBalancerUtil.planLoadBalancers(cluster, null));
  }

  private Universe universe;

  private void validateEdit(Cluster current, Cluster updated) {
    if (universe == null) {
      universe = ModelFactory.createUniverse(ModelFactory.testCustomer().getId());
    }
    ManagedLoadBalancerUtil.validateEditedCluster(
        universe,
        updated,
        current,
        updated,
        app.injector().instanceOf(RuntimeConfGetter.class),
        mockCloudAPIFactory);
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
  public void testEditThatLeavesTheDnsNamesOutKeepsThem() {
    ManagedLoadBalancerConfig named =
        cluster(ClusterType.PRIMARY, true, null).userIntent.getManagedLoadBalancer();
    named.setDnsNames(List.of("db.example.com"));
    ManagedLoadBalancerConfig withoutNames =
        cluster(ClusterType.PRIMARY, true, null).userIntent.getManagedLoadBalancer();
    ManagedLoadBalancerConfig cleared =
        cluster(ClusterType.PRIMARY, true, null).userIntent.getManagedLoadBalancer();
    cleared.setDnsNames(List.of());

    // A client that sends only enablePrivate must not wipe the names and restart the tservers.
    assertEquals(
        List.of("db.example.com"),
        ManagedLoadBalancerUtil.getEditedSetting(named, withoutNames).getDnsNames());
    assertEquals(List.of(), ManagedLoadBalancerUtil.getEditedSetting(named, cleared).getDnsNames());
  }

  @Test
  public void testTurnOffMayRepeatTheSavedDnsNamesButNotChangeThem() {
    ManagedLoadBalancerConfig named =
        cluster(ClusterType.PRIMARY, true, null).userIntent.getManagedLoadBalancer();
    named.setDnsNames(List.of("db.example.com"));
    // A client that read the setting and cleared enablePrivate.
    ManagedLoadBalancerConfig readThenTurnedOff = named.clone();
    readThenTurnedOff.setEnablePrivate(false);
    // v1 reads a missing enablePrivate as false, so this may mean "add a name", not "turn off".
    ManagedLoadBalancerConfig onlyNewNames = new ManagedLoadBalancerConfig();
    onlyNewNames.setDnsNames(List.of("db.example.com", "new.example.com"));

    assertEquals(null, ManagedLoadBalancerUtil.getEditedSetting(named, readThenTurnedOff));
    assertThrows(
        PlatformServiceException.class,
        () -> ManagedLoadBalancerUtil.getEditedSetting(named, onlyNewNames));
  }

  @Test
  public void testDnsNamesAcceptDnsNamesAndWildcardsButNotIpAddresses() {
    Cluster on = cluster(ClusterType.PRIMARY, true, null);
    for (String good : List.of("db.example.com", "*.db.example.com", "db.lbtest.internal")) {
      Cluster updated = cluster(ClusterType.PRIMARY, true, null);
      updated.userIntent.getManagedLoadBalancer().setDnsNames(List.of(good));
      validateEdit(on, updated);
    }
    // An IP in a dNSName SAN fails ycqlsh and Java clients; YBA adds the LB IP itself.
    for (String bad :
        List.of(
            "10.0.0.5",
            "10.0.0.256",
            "*.10.0.0.5",
            "2001:db8::5",
            "*.com",
            "a.*.b.com",
            "http://db.example.com",
            "a..b.com",
            "db example.com",
            "a".repeat(64) + ".example.com")) {
      Cluster updated = cluster(ClusterType.PRIMARY, true, null);
      updated.userIntent.getManagedLoadBalancer().setDnsNames(List.of(bad));
      PlatformServiceException e =
          assertThrows(bad, PlatformServiceException.class, () -> validateEdit(on, updated));
      assertEquals(bad, 400, e.getHttpStatus());
    }
  }

  @Test
  public void testCertNamesAreTheLoadBalancerOfTheNodesZoneAndTheClusterDnsNames() {
    Cluster primary = cluster(ClusterType.PRIMARY, true, null);
    primary.uuid = UUID.randomUUID();
    primary.userIntent.getManagedLoadBalancer().setDnsNames(List.of("*.db.example.com"));
    Cluster readReplica = cluster(ClusterType.ASYNC, false, null);
    readReplica.uuid = UUID.randomUUID();
    UUID westZone = UUID.randomUUID();
    UUID eastZone = UUID.randomUUID();
    ManagedLoadBalancerState state = new ManagedLoadBalancerState();
    state.put(
        new ManagedLoadBalancer(
            primary.uuid,
            UUID.randomUUID(),
            ManagedLoadBalancer.Scheme.PRIVATE,
            List.of(westZone),
            "lbi-west",
            "10.150.0.5"));
    state.put(
        new ManagedLoadBalancer(
            primary.uuid,
            UUID.randomUUID(),
            ManagedLoadBalancer.Scheme.PRIVATE,
            List.of(eastZone),
            "lbi-east",
            "lbi-east-0123456789.elb.us-east-1.amazonaws.com"));
    UniverseDefinitionTaskParams details = new UniverseDefinitionTaskParams();
    details.clusters = List.of(primary, readReplica);
    details.setManagedLoadBalancerState(state);

    // A load balancer serves only its own region, so a node answers only for that one.
    assertEquals(
        Map.of("10.150.0.5", GeneralName.iPAddress, "*.db.example.com", GeneralName.dNSName),
        ManagedLoadBalancerUtil.getCertNames(details, node(primary, westZone, true)));
    assertEquals(
        Map.of(
            "lbi-east-0123456789.elb.us-east-1.amazonaws.com",
            GeneralName.dNSName,
            "*.db.example.com",
            GeneralName.dNSName),
        ManagedLoadBalancerUtil.getCertNames(details, node(primary, eastZone, true)));
    // The load balancer sends traffic only to tservers.
    assertEquals(
        Map.of(), ManagedLoadBalancerUtil.getCertNames(details, node(primary, westZone, false)));
    assertEquals(
        Map.of(), ManagedLoadBalancerUtil.getCertNames(details, node(readReplica, westZone, true)));
  }

  private static NodeDetails node(Cluster cluster, UUID azUuid, boolean isTserver) {
    NodeDetails node = new NodeDetails();
    node.placementUuid = cluster.uuid;
    node.azUuid = azUuid;
    node.isTserver = isTserver;
    return node;
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
  public void testUniverseWithManagedLoadBalancerRejectsLbNameOnEveryCluster() {
    Cluster managedPrimary = cluster(ClusterType.PRIMARY, true, null);
    Cluster primaryWithLbName = cluster(ClusterType.PRIMARY, true, "byo-lb");
    Cluster readReplica = cluster(ClusterType.ASYNC, false, "rr-lb");

    assertThrows(
        PlatformServiceException.class,
        () -> ManagedLoadBalancerUtil.validateNoLbNames(primaryWithLbName, primaryWithLbName));
    assertThrows(
        PlatformServiceException.class,
        () -> ManagedLoadBalancerUtil.validateNoLbNames(managedPrimary, readReplica));
    // A universe without managed load balancers keeps using its own.
    ManagedLoadBalancerUtil.validateNoLbNames(
        cluster(ClusterType.PRIMARY, false, null), readReplica);
  }
}
