package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import api.v2.mappers.UniverseCertsRotateParamsMapper;
import api.v2.models.UniverseCertRotateSpec;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UniverseCertRotateMapperTest {
  @Test
  public void testRotate2Certs() {
    UniverseCertRotateSpec req = new UniverseCertRotateSpec();
    req.setClientRootCa(UUID.randomUUID());
    req.setRootCa(UUID.randomUUID());
    req.setRollingUpgrade(true);
    CertsRotateParams v1Params = new CertsRotateParams();
    UniverseCertsRotateParamsMapper.INSTANCE.copyToV1CertsRotateParams(req, v1Params);
    assertEquals(req.getRootCa(), v1Params.rootCA);
    assertEquals(req.getClientRootCa(), v1Params.getClientRootCA());
    assertFalse(v1Params.rootAndClientRootCASame);
    assertEquals(SoftwareUpgradeParams.UpgradeOption.ROLLING_UPGRADE, v1Params.upgradeOption);
  }

  @Test
  public void testRotateSameCerts() {
    UniverseCertRotateSpec req = new UniverseCertRotateSpec();
    UUID cert = UUID.randomUUID();
    req.setClientRootCa(cert);
    req.setRootCa(cert);
    req.setRollingUpgrade(false);
    CertsRotateParams v1Params = new CertsRotateParams();
    UniverseCertsRotateParamsMapper.INSTANCE.copyToV1CertsRotateParams(req, v1Params);
    assertEquals(req.getRootCa(), v1Params.rootCA);
    assertEquals(req.getClientRootCa(), v1Params.getClientRootCA());
    assertTrue(v1Params.rootAndClientRootCASame);
    assertEquals(SoftwareUpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE, v1Params.upgradeOption);
  }
}
