package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

import api.v2.mappers.UniverseRestartParamsMapper;
import api.v2.models.RollMaxBatchSize;
import api.v2.models.UniverseRestart;
import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import java.math.BigDecimal;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UniverseRestartParamsMapperTest {
  @Test
  public void testRollingRestartWithBatchSize() {
    UniverseRestart req = new UniverseRestart();
    req.setRollingRestart(true);
    RollMaxBatchSize batchSize = new RollMaxBatchSize();
    batchSize.setPrimaryBatchSize(new BigDecimal(2));
    batchSize.setReadReplicaBatchSize(new BigDecimal(3));
    req.setRollMaxBatchSize(batchSize);

    RestartTaskParams params = new RestartTaskParams();
    UniverseRestartParamsMapper.INSTANCE.copyToV1RestartTaskParams(req, params);

    assertEquals(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, params.upgradeOption);
    assertNotNull(params.rollMaxBatchSize);
    assertEquals(Integer.valueOf(2), params.rollMaxBatchSize.getPrimaryBatchSize());
    assertEquals(Integer.valueOf(3), params.rollMaxBatchSize.getReadReplicaBatchSize());
  }

  @Test
  public void testDefaultRollingWhenUnset() {
    UniverseRestart req = new UniverseRestart();
    RestartTaskParams params = new RestartTaskParams();
    UniverseRestartParamsMapper.INSTANCE.copyToV1RestartTaskParams(req, params);
    assertEquals(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, params.upgradeOption);
    assertNull(params.rollMaxBatchSize);
  }

  @Test
  public void testNonRollingRestart() {
    UniverseRestart req = new UniverseRestart();
    req.setRollingRestart(false);
    RestartTaskParams params = new RestartTaskParams();
    UniverseRestartParamsMapper.INSTANCE.copyToV1RestartTaskParams(req, params);
    assertEquals(UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE, params.upgradeOption);
  }
}
