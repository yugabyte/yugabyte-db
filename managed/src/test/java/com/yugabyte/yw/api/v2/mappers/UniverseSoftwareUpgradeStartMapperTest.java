// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import api.v2.mappers.UniverseSoftwareUpgradeStartMapper;
import api.v2.models.CanaryUpgradeConfigSpec;
import api.v2.models.SoftwareUpgradeAZStep;
import api.v2.models.UniverseSoftwareUpgradeStart;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

/** Tests for {@link UniverseSoftwareUpgradeStartMapper}. */
@RunWith(MockitoJUnitRunner.class)
public class UniverseSoftwareUpgradeStartMapperTest {

  @Test
  public void testCopyToV1SoftwareUpgradeParamsWithoutCanaryLeavesCanaryConfigNull() {
    UniverseSoftwareUpgradeStart source = new UniverseSoftwareUpgradeStart();
    source.setVersion("2.20.0.0-b2");
    source.setRollingUpgrade(true);

    SoftwareUpgradeParams target = new SoftwareUpgradeParams();

    UniverseSoftwareUpgradeStartMapper.INSTANCE.copyToV1SoftwareUpgradeParams(source, target);

    assertNull(
        "When v2 request has no canary config, target.canaryUpgradeConfig must remain null",
        target.canaryUpgradeConfig);
  }

  @Test
  public void testCopyToV1SoftwareUpgradeParamsWithCanaryConfigMapsToV1() {
    UUID azUuid = UUID.randomUUID();
    SoftwareUpgradeAZStep azStep = new SoftwareUpgradeAZStep();
    azStep.setAzUuid(azUuid);
    azStep.setPauseAfterTserverUpgrade(true);

    CanaryUpgradeConfigSpec canarySpec = new CanaryUpgradeConfigSpec();
    canarySpec.setPauseAfterMasters(true);
    canarySpec.setPrimaryClusterAzSteps(java.util.Collections.singletonList(azStep));

    UniverseSoftwareUpgradeStart source = new UniverseSoftwareUpgradeStart();
    source.setVersion("2.20.0.0-b2");
    source.setRollingUpgrade(true);
    source.setCanaryUpgradeConfig(canarySpec);

    SoftwareUpgradeParams target = new SoftwareUpgradeParams();

    UniverseSoftwareUpgradeStartMapper.INSTANCE.copyToV1SoftwareUpgradeParams(source, target);

    assertNotNull(
        "Canary config must be set when present in v2 request", target.canaryUpgradeConfig);
    assertTrue(target.canaryUpgradeConfig.pauseAfterMasters);
    assertNotNull(target.canaryUpgradeConfig.primaryClusterAZSteps);
    assertEquals(1, target.canaryUpgradeConfig.primaryClusterAZSteps.size());
    assertEquals(azUuid, target.canaryUpgradeConfig.primaryClusterAZSteps.get(0).azUUID);
    assertTrue(target.canaryUpgradeConfig.primaryClusterAZSteps.get(0).pauseAfterTserverUpgrade);
  }
}
