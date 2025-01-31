// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.api.v2;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.anyOf;
import static org.hamcrest.Matchers.emptyCollectionOf;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.yugabyte.yba.v2.client.ApiClient;
import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.Configuration;
import com.yugabyte.yba.v2.client.api.UniverseApi;
import com.yugabyte.yba.v2.client.models.UniverseCertRotateSpec;
import com.yugabyte.yba.v2.client.models.UniverseEditEncryptionInTransit;
import com.yugabyte.yba.v2.client.models.UniverseEditKubernetesOverrides;
import com.yugabyte.yba.v2.client.models.UniverseRollbackUpgradeReq;
import com.yugabyte.yba.v2.client.models.UniverseSoftwareFinalizeImpactedXCluster;
import com.yugabyte.yba.v2.client.models.UniverseSoftwareUpgradeFinalize;
import com.yugabyte.yba.v2.client.models.UniverseSoftwareUpgradeFinalizeInfo;
import com.yugabyte.yba.v2.client.models.UniverseSoftwareUpgradePrecheckReq;
import com.yugabyte.yba.v2.client.models.UniverseSoftwareUpgradePrecheckResp;
import com.yugabyte.yba.v2.client.models.UniverseSoftwareUpgradeStart;
import com.yugabyte.yba.v2.client.models.UniverseSystemdEnableStart;
import com.yugabyte.yba.v2.client.models.UniverseThirdPartySoftwareUpgradeStart;
import com.yugabyte.yba.v2.client.models.YBATask;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.controllers.UniverseControllerTestBase;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.SystemdUpgradeParams;
import com.yugabyte.yw.forms.ThirdpartySoftwareUpgradeParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.extended.FinalizeUpgradeInfoResponse;
import com.yugabyte.yw.models.extended.SoftwareUpgradeInfoResponse;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import play.inject.guice.GuiceApplicationBuilder;

public class UniverseApiControllerUpgradeTest extends UniverseControllerTestBase {
  private Customer customer;
  private Users user;
  private String authToken;
  Universe universe;
  Release upgradeRelease;

  ApiClient v2Client;
  UniverseApi apiClient;

  @Mock UpgradeUniverseHandler mockUpgradeUniverseHandler;

  @Before
  @Override
  public void setUp() {
    this.customer = ModelFactory.testCustomer();
    this.user = ModelFactory.testUser(customer);
    this.authToken = user.createAuthToken();
    this.universe = ModelFactory.createUniverse(customer.getId());
    this.upgradeRelease = Release.create("2.23.0.0-b213", "PREVIEW");
    upgradeRelease.addArtifact(
        ReleaseArtifact.create(
            null,
            ReleaseArtifact.Platform.LINUX,
            Architecture.x86_64,
            "https://download.yugabyte.com/release1"));

    v2Client = Configuration.getDefaultApiClient();
    String basePath = String.format("http://localhost:%d/api/v2", port);
    v2Client = v2Client.setBasePath(basePath).addDefaultHeader("X-AUTH-TOKEN", authToken);
    Configuration.setDefaultApiClient(v2Client);
    apiClient = new UniverseApi();
  }

  @Override
  protected GuiceApplicationBuilder appOverrides(GuiceApplicationBuilder builder) {
    return builder.overrides(
        bind(UpgradeUniverseHandler.class).toInstance(mockUpgradeUniverseHandler));
  }

  @Test
  public void testV2UniverseUpgradeRollbackExplicit() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.upgradeDBVersion(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseSoftwareUpgradeStart req = new UniverseSoftwareUpgradeStart();
    req.setAllowRollback(true);
    req.setVersion(upgradeRelease.getVersion());
    YBATask resp =
        apiClient.startSoftwareUpgrade(customer.getUuid(), universe.getUniverseUUID(), req);
    assertEquals(taskUUID, resp.getTaskUuid());
    ArgumentCaptor<SoftwareUpgradeParams> captor =
        ArgumentCaptor.forClass(SoftwareUpgradeParams.class);
    verify(mockUpgradeUniverseHandler)
        .upgradeDBVersion(captor.capture(), eq(customer), eq(universe));
    SoftwareUpgradeParams params = captor.getValue();
    assertEquals(upgradeRelease.getVersion(), params.ybSoftwareVersion);
  }

  @Test
  public void testV2UniverseUpgradeRollbackImplicit() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.upgradeDBVersion(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseSoftwareUpgradeStart req = new UniverseSoftwareUpgradeStart();
    req.setVersion(upgradeRelease.getVersion());
    YBATask resp =
        apiClient.startSoftwareUpgrade(customer.getUuid(), universe.getUniverseUUID(), req);
    assertEquals(taskUUID, resp.getTaskUuid());
    ArgumentCaptor<SoftwareUpgradeParams> captor =
        ArgumentCaptor.forClass(SoftwareUpgradeParams.class);
    verify(mockUpgradeUniverseHandler)
        .upgradeDBVersion(captor.capture(), eq(customer), eq(universe));
    SoftwareUpgradeParams params = captor.getValue();
    assertEquals(upgradeRelease.getVersion(), params.ybSoftwareVersion);
  }

  @Test
  public void testV2UniverseUpgradeNoRollback() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.upgradeSoftware(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseSoftwareUpgradeStart req = new UniverseSoftwareUpgradeStart();
    req.setAllowRollback(false);
    req.setVersion(upgradeRelease.getVersion());
    YBATask resp =
        apiClient.startSoftwareUpgrade(customer.getUuid(), universe.getUniverseUUID(), req);
    assertEquals(taskUUID, resp.getTaskUuid());
    ArgumentCaptor<SoftwareUpgradeParams> captor =
        ArgumentCaptor.forClass(SoftwareUpgradeParams.class);
    verify(mockUpgradeUniverseHandler)
        .upgradeSoftware(captor.capture(), eq(customer), eq(universe));
    SoftwareUpgradeParams params = captor.getValue();
    assertEquals(upgradeRelease.getVersion(), params.ybSoftwareVersion);
  }

  @Test
  public void testV2UniverseFinalizeInfoNoXCluster() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    FinalizeUpgradeInfoResponse response = new FinalizeUpgradeInfoResponse();
    when(mockUpgradeUniverseHandler.finalizeUpgradeInfo(
            customer.getUuid(), universe.getUniverseUUID()))
        .thenReturn(response);

    UniverseSoftwareUpgradeFinalizeInfo resp =
        apiClient.getFinalizeSoftwareUpgradeInfo(customer.getUuid(), universe.getUniverseUUID());
    assertThat(
        resp.getImpactedXclusters(),
        anyOf(nullValue(), emptyCollectionOf(UniverseSoftwareFinalizeImpactedXCluster.class)));
  }

  @Test
  public void testV2UniverseFinalizeInfoXCluster() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    FinalizeUpgradeInfoResponse response = new FinalizeUpgradeInfoResponse();
    FinalizeUpgradeInfoResponse.ImpactedXClusterConnectedUniverse xCluster =
        new FinalizeUpgradeInfoResponse.ImpactedXClusterConnectedUniverse();
    xCluster.universeUUID = UUID.randomUUID();
    xCluster.universeName = "xCluster";
    xCluster.ybSoftwareVersion = "2024.2.0.0-b123";
    ArrayList<FinalizeUpgradeInfoResponse.ImpactedXClusterConnectedUniverse> xClusterList =
        new ArrayList<>();
    xClusterList.add(xCluster);
    response.setImpactedXClusterConnectedUniverse(xClusterList);
    when(mockUpgradeUniverseHandler.finalizeUpgradeInfo(
            customer.getUuid(), universe.getUniverseUUID()))
        .thenReturn(response);

    UniverseSoftwareUpgradeFinalizeInfo resp =
        apiClient.getFinalizeSoftwareUpgradeInfo(customer.getUuid(), universe.getUniverseUUID());
    assertEquals(1, resp.getImpactedXclusters().size());
    assertEquals(xCluster.universeUUID, resp.getImpactedXclusters().get(0).getUniverseUuid());
    assertEquals(xCluster.universeName, resp.getImpactedXclusters().get(0).getUniverseName());
    assertEquals(
        xCluster.ybSoftwareVersion, resp.getImpactedXclusters().get(0).getUniverseVersion());
  }

  @Test
  public void testV2UniverseFinalizeStart() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.finalizeUpgrade(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseSoftwareUpgradeFinalize req = new UniverseSoftwareUpgradeFinalize();
    YBATask resp =
        apiClient.finalizeSoftwareUpgrade(customer.getUuid(), universe.getUniverseUUID(), req);
    assertEquals(taskUUID, resp.getTaskUuid());
    ArgumentCaptor<FinalizeUpgradeParams> captor =
        ArgumentCaptor.forClass(FinalizeUpgradeParams.class);
    verify(mockUpgradeUniverseHandler)
        .finalizeUpgrade(captor.capture(), eq(customer), eq(universe));
    FinalizeUpgradeParams params = captor.getValue();
    assertEquals(req.getUpgradeSystemCatalog(), params.upgradeSystemCatalog);
  }

  @Test
  public void testV2UniverseFinalizeStartNoSysCatalog() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.finalizeUpgrade(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseSoftwareUpgradeFinalize req = new UniverseSoftwareUpgradeFinalize();
    req.setUpgradeSystemCatalog(false);
    YBATask resp =
        apiClient.finalizeSoftwareUpgrade(customer.getUuid(), universe.getUniverseUUID(), req);
    assertEquals(taskUUID, resp.getTaskUuid());
    ArgumentCaptor<FinalizeUpgradeParams> captor =
        ArgumentCaptor.forClass(FinalizeUpgradeParams.class);
    verify(mockUpgradeUniverseHandler)
        .finalizeUpgrade(captor.capture(), eq(customer), eq(universe));
    FinalizeUpgradeParams params = captor.getValue();
    assertEquals(req.getUpgradeSystemCatalog(), params.upgradeSystemCatalog);
  }

  @Test
  public void testV2UniverseThirdPartyUpgrade() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.thirdpartySoftwareUpgrade(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseThirdPartySoftwareUpgradeStart req = new UniverseThirdPartySoftwareUpgradeStart();
    req.setSleepAfterMasterRestartMillis(1234);
    req.setSleepAfterTserverRestartMillis(4321);
    req.setForceAll(false);
    YBATask resp =
        apiClient.startThirdPartySoftwareUpgrade(
            customer.getUuid(), universe.getUniverseUUID(), req);
    assertEquals(taskUUID, resp.getTaskUuid());
    ArgumentCaptor<ThirdpartySoftwareUpgradeParams> captor =
        ArgumentCaptor.forClass(ThirdpartySoftwareUpgradeParams.class);
    verify(mockUpgradeUniverseHandler)
        .thirdpartySoftwareUpgrade(captor.capture(), eq(customer), eq(universe));
    ThirdpartySoftwareUpgradeParams params = captor.getValue();
    assertTrue(4321L == params.sleepAfterTServerRestartMillis);
    assertTrue(1234L == params.sleepAfterMasterRestartMillis);
  }

  @Test
  public void testV2RollbackNoParams() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.rollbackUpgrade(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    YBATask resp =
        apiClient.rollbackSoftwareUpgrade(customer.getUuid(), universe.getUniverseUUID(), null);
    ArgumentCaptor<RollbackUpgradeParams> captor =
        ArgumentCaptor.forClass(RollbackUpgradeParams.class);
    verify(mockUpgradeUniverseHandler)
        .rollbackUpgrade(captor.capture(), eq(customer), eq(universe));
    RollbackUpgradeParams params = captor.getValue();
    assertEquals(RollbackUpgradeParams.UpgradeOption.ROLLING_UPGRADE, params.upgradeOption);
    assertEquals(taskUUID, resp.getTaskUuid());
  }

  @Test
  public void testV2RollbackTServerRestartParams() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.rollbackUpgrade(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseRollbackUpgradeReq req = new UniverseRollbackUpgradeReq();
    req.setSleepAfterTserverRestartMillis(10000);
    req.setRollingUpgrade(false);
    YBATask resp =
        apiClient.rollbackSoftwareUpgrade(customer.getUuid(), universe.getUniverseUUID(), req);
    ArgumentCaptor<RollbackUpgradeParams> captor =
        ArgumentCaptor.forClass(RollbackUpgradeParams.class);
    verify(mockUpgradeUniverseHandler)
        .rollbackUpgrade(captor.capture(), eq(customer), eq(universe));
    RollbackUpgradeParams params = captor.getValue();
    assertTrue(10000 == params.sleepAfterTServerRestartMillis);
    assertEquals(RollbackUpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE, params.upgradeOption);
    assertEquals(taskUUID, resp.getTaskUuid());
  }

  @Test
  public void testV2PrecheckBadRelease() throws ApiException {
    UniverseSoftwareUpgradePrecheckReq req = new UniverseSoftwareUpgradePrecheckReq();
    req.setYbSoftwareVersion("1.2.3.4-b76543");
    try {
      apiClient.precheckSoftwareUpgrade(customer.getUuid(), universe.getUniverseUUID(), req);
    } catch (ApiException e) {
      assertEquals(400, e.getCode());
      assertTrue(e.getResponseBody().contains("Invalid Release Version: 1.2.3.4-b76543"));
    }
  }

  @Test
  public void testV2PrecheckFinalize() throws ApiException {
    SoftwareUpgradeInfoResponse response = new SoftwareUpgradeInfoResponse();
    response.setFinalizeRequired(true);
    when(mockUpgradeUniverseHandler.softwareUpgradeInfo(
            eq(customer.getUuid()), eq(universe.getUniverseUUID()), any()))
        .thenReturn(response);
    UniverseSoftwareUpgradePrecheckReq req = new UniverseSoftwareUpgradePrecheckReq();
    req.setYbSoftwareVersion(upgradeRelease.getVersion());
    UniverseSoftwareUpgradePrecheckResp resp =
        apiClient.precheckSoftwareUpgrade(customer.getUuid(), universe.getUniverseUUID(), req);
    assertTrue(resp.getFinalizeRequired());
  }

  @Test
  public void testV2PrecheckNoFinalize() throws ApiException {
    SoftwareUpgradeInfoResponse response = new SoftwareUpgradeInfoResponse();
    response.setFinalizeRequired(false);
    when(mockUpgradeUniverseHandler.softwareUpgradeInfo(
            eq(customer.getUuid()), eq(universe.getUniverseUUID()), any()))
        .thenReturn(response);
    UniverseSoftwareUpgradePrecheckReq req = new UniverseSoftwareUpgradePrecheckReq();
    req.setYbSoftwareVersion(upgradeRelease.getVersion());
    UniverseSoftwareUpgradePrecheckResp resp =
        apiClient.precheckSoftwareUpgrade(customer.getUuid(), universe.getUniverseUUID(), req);
    assertFalse(resp.getFinalizeRequired());
  }

  @Test
  public void testV2SystemdEnable() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.upgradeSystemd(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseSystemdEnableStart req = new UniverseSystemdEnableStart();
    req.setSleepAfterTserverRestartMillis(10000);
    YBATask resp = apiClient.systemdEnable(customer.getUuid(), universe.getUniverseUUID(), req);
    ArgumentCaptor<SystemdUpgradeParams> captor =
        ArgumentCaptor.forClass(SystemdUpgradeParams.class);
    verify(mockUpgradeUniverseHandler).upgradeSystemd(captor.capture(), eq(customer), eq(universe));
    SystemdUpgradeParams params = captor.getValue();
    assertTrue(10000 == params.sleepAfterTServerRestartMillis);
    assertEquals(taskUUID, resp.getTaskUuid());
  }

  @Test
  public void testV2TlsToggleAll() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.toggleTls(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseEditEncryptionInTransit req = new UniverseEditEncryptionInTransit();
    req.setSleepAfterTserverRestartMillis(10000);
    req.setSleepAfterMasterRestartMillis(90000);
    req.setNodeToNode(true);
    req.setClientToNode(false);
    req.setRollingUpgrade(true);
    UUID nodeCert = UUID.randomUUID();
    req.setRootCa(nodeCert);
    ;
    YBATask resp =
        apiClient.encryptionInTransitToggle(customer.getUuid(), universe.getUniverseUUID(), req);
    ArgumentCaptor<TlsToggleParams> captor = ArgumentCaptor.forClass(TlsToggleParams.class);
    verify(mockUpgradeUniverseHandler).toggleTls(captor.capture(), eq(customer), eq(universe));
    TlsToggleParams params = captor.getValue();
    assertTrue(10000 == params.sleepAfterTServerRestartMillis);
    assertTrue(90000 == params.sleepAfterMasterRestartMillis);
    assertEquals(nodeCert, params.rootCA);
    assertTrue(params.enableNodeToNodeEncrypt);
    assertFalse(params.enableClientToNodeEncrypt);
    assertFalse(params.rootAndClientRootCASame);
    assertEquals(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, params.upgradeOption);
    assertEquals(taskUUID, resp.getTaskUuid());
  }

  @Test
  public void testV2CertRotation() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.rotateCerts(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseCertRotateSpec req = new UniverseCertRotateSpec();
    req.setRollingUpgrade(true);
    UUID clientCert = UUID.randomUUID();
    UUID nodeCert = UUID.randomUUID();
    req.setRootCa(nodeCert);
    req.setClientRootCa(clientCert);
    YBATask resp =
        apiClient.encryptionInTransitCertRotate(
            customer.getUuid(), universe.getUniverseUUID(), req);
    ArgumentCaptor<CertsRotateParams> captor = ArgumentCaptor.forClass(CertsRotateParams.class);
    verify(mockUpgradeUniverseHandler).rotateCerts(captor.capture(), eq(customer), eq(universe));
    CertsRotateParams params = captor.getValue();
    assertEquals(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, params.upgradeOption);
    assertEquals(taskUUID, resp.getTaskUuid());
    assertEquals(clientCert, params.getClientRootCA());
    assertEquals(nodeCert, params.rootCA);
    assertFalse(params.rootAndClientRootCASame);
  }

  @Test
  public void testV2KubernetesOverrides() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.upgradeKubernetesOverrides(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseEditKubernetesOverrides req = new UniverseEditKubernetesOverrides();
    req.setOverrides("my_overrides");
    Map<String, String> azOverrides = new HashMap<String, String>();
    azOverrides.put("az1", "az1_overrides");
    req.setAzOverrides(azOverrides);
    YBATask resp =
        apiClient.editKubernetesOverrides(customer.getUuid(), universe.getUniverseUUID(), req);
    ArgumentCaptor<KubernetesOverridesUpgradeParams> captor =
        ArgumentCaptor.forClass(KubernetesOverridesUpgradeParams.class);
    verify(mockUpgradeUniverseHandler)
        .upgradeKubernetesOverrides(captor.capture(), eq(customer), eq(universe));
    KubernetesOverridesUpgradeParams params = captor.getValue();
    assertEquals(taskUUID, resp.getTaskUuid());
    assertEquals("my_overrides", params.universeOverrides);
    assertTrue(params.azOverrides.containsKey("az1"));
    assertEquals("az1_overrides", params.azOverrides.get("az1"));
  }
}
