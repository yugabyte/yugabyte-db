package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;

import api.v2.mappers.UniverseSystemdUpgradeMapper;
import api.v2.models.UniverseSystemdEnableStart;
import com.yugabyte.yw.forms.SystemdUpgradeParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UniverseSystemdEnableMapperTest {
  @Test
  public void testBasic() {
    UniverseSystemdEnableStart req = new UniverseSystemdEnableStart();
    req.setSleepAfterMasterRestartMillis(101);
    req.setSleepAfterTserverRestartMillis(202);
    SystemdUpgradeParams v1Params = new SystemdUpgradeParams();
    UniverseSystemdUpgradeMapper.INSTANCE.copToV1SystemdUpgradeParams(req, v1Params);
    assertEquals((Object) 101, (Object) v1Params.sleepAfterMasterRestartMillis);
    assertEquals((Object) 202, (Object) v1Params.sleepAfterTServerRestartMillis);
    assertEquals(UpgradeOption.ROLLING_UPGRADE, v1Params.upgradeOption);
  }
}
