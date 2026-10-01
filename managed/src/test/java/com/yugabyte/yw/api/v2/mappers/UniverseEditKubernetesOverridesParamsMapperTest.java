package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

import api.v2.mappers.UniverseEditKubernetesOverridesParamsMapper;
import api.v2.models.RollMaxBatchSize;
import api.v2.models.UniverseEditKubernetesOverrides;
import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import java.math.BigDecimal;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UniverseEditKubernetesOverridesParamsMapperTest {
  @Test
  public void testRollingUpgradeWithBatchSize() {
    UniverseEditKubernetesOverrides req = new UniverseEditKubernetesOverrides();
    req.setOverrides("tserver:\n  podLabels:\n    env: test");
    req.setRollingUpgrade(true);
    req.setSleepAfterTserverRestartMillis(18000);
    RollMaxBatchSize batchSize = new RollMaxBatchSize();
    batchSize.setPrimaryBatchSize(new BigDecimal(2));
    batchSize.setReadReplicaBatchSize(new BigDecimal(2));
    req.setRollMaxBatchSize(batchSize);

    KubernetesOverridesUpgradeParams params = new KubernetesOverridesUpgradeParams();
    UniverseEditKubernetesOverridesParamsMapper.INSTANCE.copyToV1KubernetesOverridesParams(
        req, params);

    assertEquals("tserver:\n  podLabels:\n    env: test", params.universeOverrides);
    assertEquals(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, params.upgradeOption);
    assertEquals((Object) 18000, (Object) params.sleepAfterTServerRestartMillis);
    assertNotNull(params.rollMaxBatchSize);
    assertEquals(Integer.valueOf(2), params.rollMaxBatchSize.getPrimaryBatchSize());
    assertEquals(Integer.valueOf(2), params.rollMaxBatchSize.getReadReplicaBatchSize());
  }

  @Test
  public void testDefaultRollingWhenUnset() {
    UniverseEditKubernetesOverrides req = new UniverseEditKubernetesOverrides();
    KubernetesOverridesUpgradeParams params = new KubernetesOverridesUpgradeParams();
    UniverseEditKubernetesOverridesParamsMapper.INSTANCE.copyToV1KubernetesOverridesParams(
        req, params);
    assertEquals(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, params.upgradeOption);
    assertNull(params.rollMaxBatchSize);
  }

  @Test
  public void testNonRollingUpgrade() {
    UniverseEditKubernetesOverrides req = new UniverseEditKubernetesOverrides();
    req.setRollingUpgrade(false);
    KubernetesOverridesUpgradeParams params = new KubernetesOverridesUpgradeParams();
    UniverseEditKubernetesOverridesParamsMapper.INSTANCE.copyToV1KubernetesOverridesParams(
        req, params);
    assertEquals(UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE, params.upgradeOption);
  }
}
