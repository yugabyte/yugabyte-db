// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.VMImageUpgradeParams.ImageBundleUpgradeInfo;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class VMImageUpgradeParamsTest extends FakeDBApplication {

  private Customer customer;
  private Provider provider;
  private Region region1;
  private Region region2;
  private AvailabilityZone az1;
  private AvailabilityZone az2;
  private Universe universe;
  private UUID primaryClusterUuid;
  private UUID readReplicaClusterUuid;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.awsProvider(customer);
    region1 = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    region2 = Region.create(provider, "region-2", "Region 2", "yb-image-2");
    az1 = AvailabilityZone.createOrThrow(region1, "az-1", "AZ 1", "subnet-1");
    az2 = AvailabilityZone.createOrThrow(region2, "az-2", "AZ 2", "subnet-2");

    UserIntent primaryIntent = new UserIntent();
    primaryIntent.ybSoftwareVersion = "2.21.1.1-b1";
    primaryIntent.accessKeyCode = "demo-access";
    primaryIntent.regionList = ImmutableList.of(region1.getUuid());
    primaryIntent.providerType = Common.CloudType.aws;
    primaryIntent.provider = provider.getUuid().toString();
    primaryIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    primaryIntent.numNodes = 1;
    primaryIntent.replicationFactor = 1;

    PlacementInfo primaryPlacement = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), primaryPlacement, 1, 1, true);

    universe = ModelFactory.createUniverse(customer.getId());
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(primaryIntent, primaryPlacement, true));
    primaryClusterUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;

    UserIntent rrIntent = new UserIntent();
    rrIntent.numNodes = 1;
    rrIntent.ybSoftwareVersion = primaryIntent.ybSoftwareVersion;
    rrIntent.accessKeyCode = primaryIntent.accessKeyCode;
    rrIntent.regionList = ImmutableList.of(region2.getUuid());
    rrIntent.providerType = primaryIntent.providerType;
    rrIntent.provider = primaryIntent.provider;
    rrIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);

    PlacementInfo rrPlacement = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), rrPlacement, 1, 1, true);

    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(rrIntent, rrPlacement));
    readReplicaClusterUuid = universe.getUniverseDetails().getReadOnlyClusters().get(0).uuid;

    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              for (NodeDetails node : u.getUniverseDetails().nodeDetailsSet) {
                AvailabilityZone az = AvailabilityZone.getOrBadRequest(node.azUuid);
                node.cloudInfo.region = az.getRegion().getCode();
                node.cloudInfo.az = az.getCode();
                node.cloudInfo.cloud = Common.CloudType.aws.toString();
              }
            });
  }

  @Test
  public void testVerifyParamsValidatesImageBundleOnlyForMatchingCluster() {
    ImageBundle primaryBundle = createImageBundle("primary-ib", "region-1");
    ImageBundle rrBundle = createImageBundle("rr-ib", "region-2");

    VMImageUpgradeParams params = new VMImageUpgradeParams();
    params.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
    params.clusters = universe.getUniverseDetails().clusters;
    params.machineImages = null;
    params.imageBundles =
        Arrays.asList(
            new ImageBundleUpgradeInfo(primaryClusterUuid, primaryBundle.getUuid(), null),
            new ImageBundleUpgradeInfo(readReplicaClusterUuid, rrBundle.getUuid(), null));
    // Should be successfull since we validate bundle per cluster.
    params.verifyParams(universe, true);
  }

  @Test
  public void testVerifyParamsStillValidatesMatchingClusterImageBundle() {
    // Bundle for the primary cluster is missing the primary node's region.
    ImageBundle incompletePrimaryBundle = createImageBundle("primary-ib", "region-2");
    ImageBundle rrBundle = createImageBundle("rr-ib", "region-2");

    VMImageUpgradeParams params = new VMImageUpgradeParams();
    params.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
    params.clusters = universe.getUniverseDetails().clusters;
    params.machineImages = null;
    params.imageBundles =
        Arrays.asList(
            new ImageBundleUpgradeInfo(primaryClusterUuid, incompletePrimaryBundle.getUuid(), null),
            new ImageBundleUpgradeInfo(readReplicaClusterUuid, rrBundle.getUuid(), null));

    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> params.verifyParams(universe, true));
    assertTrue(
        ex.getMessage(),
        ex.getMessage().contains("Image Bundle primary-ib is missing the image for region"));
  }

  @Test
  public void testVerifyParamsAcceptsOciPrimaryUniverse() {
    Universe ociUniverse = createOciUniverse();
    UserIntent ociIntent = ociUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    Provider ociProvider = Provider.getOrBadRequest(UUID.fromString(ociIntent.provider));
    ImageBundle ociBundle = createImageBundle(ociProvider, "oci-ib", "ashburn");
    assertOciUpgradeAccepted(
        ociUniverse, ociUniverse.getUniverseDetails().getPrimaryCluster().uuid, ociBundle);
  }

  @Test
  public void testVerifyParamsRejectsOciMachineImages() {
    Universe ociUniverse = createOciUniverse();
    VMImageUpgradeParams params = new VMImageUpgradeParams();
    params.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
    params.clusters = ociUniverse.getUniverseDetails().clusters;
    params.machineImages =
        Map.of(
            ociUniverse.getUniverseDetails().getPrimaryCluster().userIntent.regionList.get(0),
            "oci-target-image");
    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> params.verifyParams(ociUniverse, true));
    assertTrue(ex.getMessage(), ex.getMessage().contains("Use imageBundles"));
  }

  private Universe createOciUniverse() {
    Provider ociProvider = ModelFactory.ociProvider(customer);
    Region ociRegion = Region.create(ociProvider, "ashburn", "Ashburn", "oci-image");
    AvailabilityZone ociAz =
        AvailabilityZone.createOrThrow(ociRegion, "ad-1", "AD 1", "subnet-oci");

    UserIntent ociIntent = new UserIntent();
    ociIntent.ybSoftwareVersion = "2026.1.2.0-b1";
    ociIntent.accessKeyCode = "demo-access";
    ociIntent.regionList = ImmutableList.of(ociRegion.getUuid());
    ociIntent.providerType = Common.CloudType.oci;
    ociIntent.provider = ociProvider.getUuid().toString();
    ociIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    ociIntent.numNodes = 1;
    ociIntent.replicationFactor = 1;

    PlacementInfo placement = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(ociAz.getUuid(), placement, 1, 1, true);

    Universe ociUniverse = ModelFactory.createUniverse("oci-univ", customer.getId());
    return Universe.saveDetails(
        ociUniverse.getUniverseUUID(), ApiUtils.mockUniverseUpdater(ociIntent, placement, true));
  }

  @Test
  public void testVerifyParamsAcceptsOciInMixedCloudPrimary() {
    Provider ociProvider = ModelFactory.ociProvider(customer);
    Region ociRegion = Region.create(ociProvider, "ashburn", "Ashburn", "oci-image");
    AvailabilityZone ociAz =
        AvailabilityZone.createOrThrow(ociRegion, "ad-1", "AD 1", "subnet-oci");
    ImageBundle ociBundle = createImageBundle(ociProvider, "oci-ib", ociRegion.getCode());
    UUID primaryUuid = primaryClusterUuid;
    UUID ociNodeUuid = UUID.randomUUID();
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UserIntent intent = u.getUniverseDetails().getPrimaryCluster().userIntent;
              UUID providerUUID = intent.maybeGetSingleProviderUUID().get();
              TestUtils.specificationProviderInitializer(intent, providerUUID)
                  .setProviderType(intent.getProviderType(providerUUID))
                  .setAccessCode(intent.accessKeyCode)
                  .setInstanceTags(intent.instanceTags)
                  .setDeviceInfo(intent.deviceInfo)
                  .setInstanceType(intent.instanceType);

              TestUtils.specificationProviderInitializer(intent, ociProvider.getUuid())
                  .setProviderType(Common.CloudType.oci)
                  .setInstanceType("VM.Standard.E4.Flex")
                  .setDeviceInfo(intent.deviceInfo.clone());

              assertEquals(2, intent.providerSpecifications.size());

              NodeDetails ociNode = new NodeDetails();
              ociNode.nodeIdx = intent.numNodes + 1;
              ociNode.nodeName = "host-n" + ociNode.nodeIdx;
              ociNode.nodeUuid = ociNodeUuid;
              ociNode.placementUuid = primaryUuid;
              ociNode.isMaster = true;
              ociNode.isTserver = true;
              ociNode.state = NodeDetails.NodeState.Live;
              ociNode.azUuid = ociAz.getUuid();
              ociNode.cloudInfo = new CloudSpecificInfo();
              ociNode.cloudInfo.cloud = Common.CloudType.oci.toString();
              ociNode.cloudInfo.region = ociRegion.getCode();
              ociNode.cloudInfo.az = ociAz.getCode();
              u.getUniverseDetails().nodeDetailsSet.add(ociNode);
            });

    VMImageUpgradeParams params = new VMImageUpgradeParams();
    params.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
    params.clusters = universe.getUniverseDetails().clusters;
    params.machineImages = null;
    params.imageBundles =
        Arrays.asList(
            new ImageBundleUpgradeInfo(primaryUuid, ociBundle.getUuid(), ociProvider.getUuid()));
    params.verifyParams(universe, true);
    assertEquals(Collections.singleton(ociNodeUuid), params.nodeToRegion.keySet());
  }

  private void assertOciUpgradeAccepted(Universe target, UUID clusterUuid, ImageBundle bundle) {
    VMImageUpgradeParams params = new VMImageUpgradeParams();
    params.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
    params.clusters = target.getUniverseDetails().clusters;
    params.machineImages = null;
    params.imageBundles =
        Arrays.asList(new ImageBundleUpgradeInfo(clusterUuid, bundle.getUuid(), null));
    params.verifyParams(target, true);
    assertTrue(
        target.getUniverseDetails().getNodesInCluster(clusterUuid).stream()
            .allMatch(n -> params.nodeToRegion.containsKey(n.nodeUuid)));
  }

  private ImageBundle createImageBundle(String name, String regionCode) {
    return createImageBundle(provider, name, regionCode);
  }

  private ImageBundle createImageBundle(Provider bundleProvider, String name, String regionCode) {
    ImageBundleDetails ibDetails = new ImageBundleDetails();
    ibDetails.setArch(Architecture.x86_64);
    ImageBundleDetails.BundleInfo bundleInfo = new ImageBundleDetails.BundleInfo();
    bundleInfo.setYbImage(regionCode + "-yb-image");
    Map<String, ImageBundleDetails.BundleInfo> regions = new HashMap<>();
    regions.put(regionCode, bundleInfo);
    ibDetails.setRegions(regions);
    return ImageBundle.create(bundleProvider, name, ibDetails, true);
  }
}
