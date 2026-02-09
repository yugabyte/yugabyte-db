// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementCloud;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementRegion;
import java.util.ArrayList;
import java.util.Collections;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class SoftwareUpgradeParamsCanaryTest extends FakeDBApplication {

  private Customer customer;
  private Universe universeWithPlacement;
  private UUID validPrimaryAzUuid;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    validPrimaryAzUuid = UUID.randomUUID();

    PlacementInfo placementInfo = new PlacementInfo();
    PlacementCloud cloud = new PlacementCloud();
    cloud.uuid = UUID.randomUUID();
    cloud.code = "aws";
    PlacementRegion region = new PlacementRegion();
    region.uuid = UUID.randomUUID();
    region.code = "region-1";
    PlacementAZ az = new PlacementAZ();
    az.uuid = validPrimaryAzUuid;
    az.name = "az-1";
    az.replicationFactor = 1;
    region.azList = new ArrayList<>();
    region.azList.add(az);
    cloud.regionList = new ArrayList<>();
    cloud.regionList.add(region);
    placementInfo.cloudList = new ArrayList<>();
    placementInfo.cloudList.add(cloud);

    universeWithPlacement =
        ModelFactory.createUniverse(
            "CanaryTestUniverse",
            UUID.randomUUID(),
            customer.getId(),
            com.yugabyte.yw.commissioner.Common.CloudType.aws,
            placementInfo);
    universeWithPlacement.getUniverseDetails().softwareUpgradeState =
        UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready;
    universeWithPlacement.save();
  }

  // Use preview version (2.19.x) to match universe's 2.17.0.0-b1 and avoid preview/stable check
  private static final String UPGRADE_VERSION = "2.19.0.0-b1";

  @Test
  public void testVerifyParamsWithCanaryConfigNull() {
    SoftwareUpgradeParams params = new SoftwareUpgradeParams();
    params.ybSoftwareVersion = UPGRADE_VERSION;
    params.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
    params.canaryUpgradeConfig = null;
    Universe universe = Universe.getOrBadRequest(universeWithPlacement.getUniverseUUID());
    params.verifyParams(universe, true);
    // no exception
  }

  @Test
  public void testVerifyParamsWithValidCanaryConfig() {
    SoftwareUpgradeParams params = new SoftwareUpgradeParams();
    params.ybSoftwareVersion = UPGRADE_VERSION;
    params.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
    params.canaryUpgradeConfig = new CanaryUpgradeConfig();
    params.canaryUpgradeConfig.pauseAfterMasters = true;
    AZUpgradeStep step = new AZUpgradeStep();
    step.azUUID = validPrimaryAzUuid;
    step.pauseAfterTserverUpgrade = true;
    params.canaryUpgradeConfig.primaryClusterAZSteps = Collections.singletonList(step);

    Universe universe = Universe.getOrBadRequest(universeWithPlacement.getUniverseUUID());
    params.verifyParams(universe, true);
    // no exception
  }

  @Test
  public void testVerifyParamsWithInvalidAzInPrimaryClusterSteps() {
    SoftwareUpgradeParams params = new SoftwareUpgradeParams();
    params.ybSoftwareVersion = UPGRADE_VERSION;
    params.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
    params.canaryUpgradeConfig = new CanaryUpgradeConfig();
    AZUpgradeStep step = new AZUpgradeStep();
    step.azUUID = UUID.randomUUID(); // AZ not in universe placement
    step.pauseAfterTserverUpgrade = false;
    params.canaryUpgradeConfig.primaryClusterAZSteps = Collections.singletonList(step);

    Universe universe = Universe.getOrBadRequest(universeWithPlacement.getUniverseUUID());
    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> params.verifyParams(universe, true));
    assertTrue(
        ex.getMessage().contains("is not in the primary cluster placement")
            || ex.getMessage().contains("AZ"));
  }

  @Test
  public void testVerifyParamsWithReadReplicaStepsButNoReadReplicaCluster() {
    SoftwareUpgradeParams params = new SoftwareUpgradeParams();
    params.ybSoftwareVersion = UPGRADE_VERSION;
    params.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
    params.canaryUpgradeConfig = new CanaryUpgradeConfig();
    AZUpgradeStep step = new AZUpgradeStep();
    step.azUUID = UUID.randomUUID();
    step.pauseAfterTserverUpgrade = false;
    params.canaryUpgradeConfig.readReplicaClusterAZSteps = Collections.singletonList(step);

    Universe universe = Universe.getOrBadRequest(universeWithPlacement.getUniverseUUID());
    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> params.verifyParams(universe, true));
    assertTrue(
        ex.getMessage().contains("read replica") && ex.getMessage().contains("no read replica"));
  }
}
