package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;

import api.v2.mappers.UniverseRollbackUpgradeMapper;
import api.v2.models.UniverseRollbackUpgradeReq;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UniverseSoftwareUpgradeRollbackTest {
  @Test
  public void testRollbackUpgradeOptionDefaultMap() {
    UniverseRollbackUpgradeReq req = new UniverseRollbackUpgradeReq();
    RollbackUpgradeParams params = new RollbackUpgradeParams();
    UniverseRollbackUpgradeMapper.INSTANCE.copyToV1RollbackUpgradeParams(req, params);
    assertEquals(RollbackUpgradeParams.UpgradeOption.ROLLING_UPGRADE, params.upgradeOption);
  }

  @Test
  public void testRollbackUpgradeOptionTrueMap() {
    UniverseRollbackUpgradeReq req = new UniverseRollbackUpgradeReq();
    req.setRollingUpgrade(true);
    RollbackUpgradeParams params = new RollbackUpgradeParams();
    UniverseRollbackUpgradeMapper.INSTANCE.copyToV1RollbackUpgradeParams(req, params);
    assertEquals(RollbackUpgradeParams.UpgradeOption.ROLLING_UPGRADE, params.upgradeOption);
  }

  @Test
  public void testRollbackUpgradeOptionFalseMap() {
    UniverseRollbackUpgradeReq req = new UniverseRollbackUpgradeReq();
    req.setRollingUpgrade(false);
    RollbackUpgradeParams params = new RollbackUpgradeParams();
    UniverseRollbackUpgradeMapper.INSTANCE.copyToV1RollbackUpgradeParams(req, params);
    assertEquals(RollbackUpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE, params.upgradeOption);
  }
}
