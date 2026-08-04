package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;

import api.v2.mappers.UniverseEditGFlagsMapper;
import api.v2.models.UniverseEditGFlags;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UniverseEditGflagsMapperTest {
  @Test
  public void testTserverRestartMillisMap() {
    UniverseEditGFlags req = new UniverseEditGFlags();
    req.setSleepAfterTserverRestartMillis(1234567);
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    UniverseEditGFlagsMapper.INSTANCE.copyToV1GFlagsUpgradeParams(req, params);
    assertEquals((Object) 1234567, (Object) params.sleepAfterTServerRestartMillis);
  }

  @Test
  public void testUpgradeOptionMapRolling() {
    UniverseEditGFlags req = new UniverseEditGFlags();
    req.setUpgradeOption(UniverseEditGFlags.UpgradeOptionEnum.ROLLING);
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    UniverseEditGFlagsMapper.INSTANCE.copyToV1GFlagsUpgradeParams(req, params);
    assertEquals(GFlagsUpgradeParams.UpgradeOption.ROLLING_UPGRADE, params.upgradeOption);
  }

  @Test
  public void testUpgradeOptionMapDefault() {
    UniverseEditGFlags req = new UniverseEditGFlags();
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    UniverseEditGFlagsMapper.INSTANCE.copyToV1GFlagsUpgradeParams(req, params);
    assertEquals(GFlagsUpgradeParams.UpgradeOption.ROLLING_UPGRADE, params.upgradeOption);
  }

  @Test
  public void testUpgradeOptionMapNonRolling() {
    UniverseEditGFlags req = new UniverseEditGFlags();
    req.setUpgradeOption(UniverseEditGFlags.UpgradeOptionEnum.NON_ROLLING);
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    UniverseEditGFlagsMapper.INSTANCE.copyToV1GFlagsUpgradeParams(req, params);
    assertEquals(GFlagsUpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE, params.upgradeOption);
  }

  @Test
  public void testUpgradeOptionMapNonRestart() {
    UniverseEditGFlags req = new UniverseEditGFlags();
    req.setUpgradeOption(UniverseEditGFlags.UpgradeOptionEnum.NON_RESTART);
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    UniverseEditGFlagsMapper.INSTANCE.copyToV1GFlagsUpgradeParams(req, params);
    assertEquals(GFlagsUpgradeParams.UpgradeOption.NON_RESTART_UPGRADE, params.upgradeOption);
  }
}
