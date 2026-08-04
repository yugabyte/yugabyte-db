package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;

import api.v2.mappers.UniverseThirdPartySoftwareUpgradeMapper;
import api.v2.models.UniverseThirdPartySoftwareUpgradeStart;
import com.yugabyte.yw.forms.ThirdpartySoftwareUpgradeParams;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UniverseThirdpartyUpgradeMapperTest {
  @Test
  public void testThirdpartyUpgradeUpgradeOptionMap() {
    UniverseThirdPartySoftwareUpgradeStart req = new UniverseThirdPartySoftwareUpgradeStart();
    ThirdpartySoftwareUpgradeParams params = new ThirdpartySoftwareUpgradeParams();
    UniverseThirdPartySoftwareUpgradeMapper.INSTANCE.copyToV1ThirdpartySoftwareUpgradeParams(
        req, params);
    assertEquals(
        ThirdpartySoftwareUpgradeParams.UpgradeOption.ROLLING_UPGRADE, params.upgradeOption);
  }
}
