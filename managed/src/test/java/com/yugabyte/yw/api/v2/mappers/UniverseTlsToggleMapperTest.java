package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import api.v2.mappers.UniverseTlsToggleParamsMapper;
import api.v2.models.UniverseEditEncryptionInTransit;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UniverseTlsToggleMapperTest {
  @Test
  public void testNodeToNodeOnly() {
    UniverseEditEncryptionInTransit req = new UniverseEditEncryptionInTransit();
    req.setNodeToNode(true);
    req.setClientToNode(false);
    TlsToggleParams v1Params = new TlsToggleParams();
    UniverseTlsToggleParamsMapper.INSTANCE.copyToV1TlsToggleParams(req, v1Params);
    assertTrue(v1Params.enableNodeToNodeEncrypt);
    assertFalse(v1Params.enableClientToNodeEncrypt);
  }

  @Test
  public void testClientToNodeOnly() {
    UniverseEditEncryptionInTransit req = new UniverseEditEncryptionInTransit();
    req.setNodeToNode(false);
    req.setClientToNode(true);
    TlsToggleParams v1Params = new TlsToggleParams();
    UniverseTlsToggleParamsMapper.INSTANCE.copyToV1TlsToggleParams(req, v1Params);
    assertTrue(v1Params.enableClientToNodeEncrypt);
    assertFalse(v1Params.enableNodeToNodeEncrypt);
  }

  @Test
  public void testClientAndNode() {
    UniverseEditEncryptionInTransit req = new UniverseEditEncryptionInTransit();
    req.setNodeToNode(true);
    req.setClientToNode(true);
    TlsToggleParams v1Params = new TlsToggleParams();
    UniverseTlsToggleParamsMapper.INSTANCE.copyToV1TlsToggleParams(req, v1Params);
    assertTrue(v1Params.enableClientToNodeEncrypt);
    assertTrue(v1Params.enableNodeToNodeEncrypt);
  }

  @Test
  public void testEnableWith2Certs() {
    UniverseEditEncryptionInTransit req = new UniverseEditEncryptionInTransit();
    req.setNodeToNode(true);
    req.setClientToNode(true);
    req.setRootCa(UUID.randomUUID());
    req.setClientRootCa(UUID.randomUUID());
    TlsToggleParams v1Params = new TlsToggleParams();
    UniverseTlsToggleParamsMapper.INSTANCE.copyToV1TlsToggleParams(req, v1Params);
    assertTrue(v1Params.enableClientToNodeEncrypt);
    assertTrue(v1Params.enableNodeToNodeEncrypt);
    assertEquals(req.getRootCa(), v1Params.rootCA);
    assertEquals(req.getClientRootCa(), v1Params.getClientRootCA());
    assertFalse(v1Params.rootAndClientRootCASame);
  }

  @Test
  public void testEnableSameCerts() {
    UniverseEditEncryptionInTransit req = new UniverseEditEncryptionInTransit();
    req.setNodeToNode(true);
    req.setClientToNode(true);
    UUID cert = UUID.randomUUID();
    req.setRootCa(cert);
    req.setClientRootCa(cert);
    TlsToggleParams v1Params = new TlsToggleParams();
    UniverseTlsToggleParamsMapper.INSTANCE.copyToV1TlsToggleParams(req, v1Params);
    assertTrue(v1Params.enableClientToNodeEncrypt);
    assertTrue(v1Params.enableNodeToNodeEncrypt);
    assertEquals(req.getRootCa(), v1Params.rootCA);
    assertEquals(req.getClientRootCa(), v1Params.getClientRootCA());
    assertTrue(v1Params.rootAndClientRootCASame);
  }

  @Test
  public void testRolling() {
    UniverseEditEncryptionInTransit req = new UniverseEditEncryptionInTransit();
    req.setRollingUpgrade(true);
    TlsToggleParams v1Params = new TlsToggleParams();
    UniverseTlsToggleParamsMapper.INSTANCE.copyToV1TlsToggleParams(req, v1Params);
    assertEquals(SoftwareUpgradeParams.UpgradeOption.ROLLING_UPGRADE, v1Params.upgradeOption);
  }

  @Test
  public void testNonRolling() {
    UniverseEditEncryptionInTransit req = new UniverseEditEncryptionInTransit();
    req.setRollingUpgrade(false);
    TlsToggleParams v1Params = new TlsToggleParams();
    UniverseTlsToggleParamsMapper.INSTANCE.copyToV1TlsToggleParams(req, v1Params);
    assertEquals(SoftwareUpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE, v1Params.upgradeOption);
  }
}
