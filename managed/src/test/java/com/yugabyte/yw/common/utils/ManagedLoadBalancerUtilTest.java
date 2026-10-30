// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.utils;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.ManagedLoadBalancerConfig;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.ManagedLoadBalancer;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.List;
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

    List<ManagedLoadBalancer> lbs = ManagedLoadBalancerUtil.planLoadBalancers(cluster);

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
        ManagedLoadBalancerUtil.planLoadBalancers(cluster(ClusterType.PRIMARY, false, null)));
    assertEquals(List.of(), ManagedLoadBalancerUtil.planLoadBalancers(null));
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
    assertEquals(List.of(), ManagedLoadBalancerUtil.planLoadBalancers(cluster));
  }

  @Test
  public void testEditCannotChangeWhichLoadBalancersAreManaged() {
    Cluster current = cluster(ClusterType.PRIMARY, true, null);
    Cluster withPublic = cluster(ClusterType.PRIMARY, true, null);
    withPublic.userIntent.getManagedLoadBalancer().setEnablePublic(true);

    ManagedLoadBalancerUtil.validateEditedCluster(
        current, current, cluster(ClusterType.PRIMARY, true, null));
    assertThrows(
        PlatformServiceException.class,
        () -> ManagedLoadBalancerUtil.validateEditedCluster(current, current, withPublic));
    assertThrows(
        PlatformServiceException.class,
        () ->
            ManagedLoadBalancerUtil.validateEditedCluster(
                current, current, cluster(ClusterType.PRIMARY, false, null)));
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
