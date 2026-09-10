package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;

import api.v2.mappers.UniverseVMImageUpgradeMapper;
import api.v2.models.UniverseVMImageUpgradeSpec;
import api.v2.models.VMImageClusterSpec;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.VMImageUpgradeParams;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UniverseVMImageUpgradeMapperTest {

  @Test
  public void testBasicConversion() {
    UniverseVMImageUpgradeSpec req = new UniverseVMImageUpgradeSpec();
    req.setSleepAfterMasterRestartMillis(500);
    req.setSleepAfterTserverRestartMillis(600);
    UUID clusterUUID1 = UUID.randomUUID();
    UUID clusterUUID2 = UUID.randomUUID();
    UUID providerUUID1 = UUID.randomUUID();
    UUID providerUUID2 = UUID.randomUUID();
    UUID imageBundleUUID1 = UUID.randomUUID();
    UUID imageBundleUUID2 = UUID.randomUUID();

    req.addImageBundlesItem(
        new VMImageClusterSpec()
            .imageBundleUuid(imageBundleUUID1)
            .clusterUuid(clusterUUID1)
            .providerUuid(providerUUID1));
    req.addImageBundlesItem(
        new VMImageClusterSpec()
            .imageBundleUuid(imageBundleUUID2)
            .clusterUuid(clusterUUID2)
            .providerUuid(providerUUID2));
    req.setForceUpgrade(true);

    VMImageUpgradeParams params = new VMImageUpgradeParams();
    UniverseVMImageUpgradeMapper.INSTANCE.copyToV1VMImageUpgradeParams(req, params);

    assertEquals(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, params.upgradeOption);
    assertEquals(500, params.sleepAfterMasterRestartMillis.intValue());
    assertEquals(600, params.sleepAfterTServerRestartMillis.intValue());
    assertEquals(2, params.imageBundles.size());
    assertEquals(clusterUUID1, params.imageBundles.get(0).getClusterUuid());
    assertEquals(providerUUID1, params.imageBundles.get(0).getProviderUuid());
    assertEquals(imageBundleUUID1, params.imageBundles.get(0).getImageBundleUuid());
    assertEquals(clusterUUID2, params.imageBundles.get(1).getClusterUuid());
    assertEquals(providerUUID2, params.imageBundles.get(1).getProviderUuid());
    assertEquals(imageBundleUUID2, params.imageBundles.get(1).getImageBundleUuid());
  }
}
