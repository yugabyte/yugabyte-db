// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.api.v2;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.anyOf;
import static org.hamcrest.Matchers.containsString;
import static org.hamcrest.Matchers.emptyCollectionOf;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yba.v2.client.ApiClient;
import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.Configuration;
import com.yugabyte.yba.v2.client.api.UniverseApi;
import com.yugabyte.yba.v2.client.models.ClusterResizeStorageSpec;
import com.yugabyte.yba.v2.client.models.NodeProxyConfig;
import com.yugabyte.yba.v2.client.models.PerProviderResizeNodesSpec;
import com.yugabyte.yba.v2.client.models.PerProviderUpdateProxyConfigSpec;
import com.yugabyte.yba.v2.client.models.ResizeProviderNodeSpec;
import com.yugabyte.yba.v2.client.models.ResizeProviderRootNodesSpec;
import com.yugabyte.yba.v2.client.models.UniverseCertRotateSpec;
import com.yugabyte.yba.v2.client.models.UniverseEditEncryptionInTransit;
import com.yugabyte.yba.v2.client.models.UniverseEditKubernetesOverrides;
import com.yugabyte.yba.v2.client.models.UniverseResizeNodes;
import com.yugabyte.yba.v2.client.models.UniverseResizeNodesCluster;
import com.yugabyte.yba.v2.client.models.UniverseRollbackUpgradeReq;
import com.yugabyte.yba.v2.client.models.UniverseSoftwareFinalizeImpactedXCluster;
import com.yugabyte.yba.v2.client.models.UniverseSoftwareUpgradeFinalize;
import com.yugabyte.yba.v2.client.models.UniverseSoftwareUpgradeFinalizeInfo;
import com.yugabyte.yba.v2.client.models.UniverseSoftwareUpgradePrecheckReq;
import com.yugabyte.yba.v2.client.models.UniverseSoftwareUpgradePrecheckResp;
import com.yugabyte.yba.v2.client.models.UniverseSoftwareUpgradeStart;
import com.yugabyte.yba.v2.client.models.UniverseSystemdEnableStart;
import com.yugabyte.yba.v2.client.models.UniverseThirdPartySoftwareUpgradeStart;
import com.yugabyte.yba.v2.client.models.UniverseUpdateProxyConfig;
import com.yugabyte.yba.v2.client.models.UniverseUpdateProxyConfigClustersInner;
import com.yugabyte.yba.v2.client.models.UpdateProxyConfigSpec;
import com.yugabyte.yba.v2.client.models.YBATask;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.forms.HierarchicalNodesSpec;
import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import com.yugabyte.yw.forms.ProxyConfigUpdateParams;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.SystemdUpgradeParams;
import com.yugabyte.yw.forms.ThirdpartySoftwareUpgradeParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ProviderSpecification;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.extended.FinalizeUpgradeInfoResponse;
import com.yugabyte.yw.models.extended.SoftwareUpgradeInfoResponse;
import com.yugabyte.yw.models.helpers.ProxyConfig;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;

public class UniverseApiControllerUpgradeTest extends UniverseTestBase {

  // Binds a per-method @Mock (recreated by the MockitoRule each method) into the application, so
  // the
  // application must be rebuilt per method - it cannot be reused across the class' methods.
  @Override
  protected boolean reusableApplication() {
    return false;
  }

  Universe universe;
  Release upgradeRelease;

  ApiClient v2Client;
  UniverseApi apiClient;

  @Mock UpgradeUniverseHandler mockUpgradeUniverseHandler;

  @Before
  public void setUpClass() {
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
    try {
      this.universe = Universe.getOrBadRequest(setupUniverse(false));
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
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
  public void testV2UniverseUpgradeRunOnlyPrechecksTrue() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.upgradeDBVersion(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseSoftwareUpgradeStart req = new UniverseSoftwareUpgradeStart();
    req.setAllowRollback(true);
    req.setVersion(upgradeRelease.getVersion());
    req.setRunOnlyPrechecks(true);
    YBATask resp =
        apiClient.startSoftwareUpgrade(customer.getUuid(), universe.getUniverseUUID(), req);
    assertEquals(taskUUID, resp.getTaskUuid());
    ArgumentCaptor<SoftwareUpgradeParams> captor =
        ArgumentCaptor.forClass(SoftwareUpgradeParams.class);
    verify(mockUpgradeUniverseHandler)
        .upgradeDBVersion(captor.capture(), eq(customer), eq(universe));
    SoftwareUpgradeParams params = captor.getValue();
    assertTrue("runOnlyPrechecks should be true", Boolean.TRUE.equals(params.runOnlyPrechecks));
  }

  @Test
  public void testV2UniverseUpgradeRunOnlyPrechecksDefaultFalse() throws ApiException {
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
    assertFalse(
        "runOnlyPrechecks should be false when not set",
        Boolean.TRUE.equals(params.runOnlyPrechecks));
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
  public void testV2ResumeCanarySoftwareUpgradeRawApi() {
    UUID pausedTaskUUID = UUID.randomUUID();
    UUID newTaskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.resumeCanarySoftwareUpgrade(
            eq(customer.getUuid()), eq(universe.getUniverseUUID()), eq(pausedTaskUUID)))
        .thenReturn(newTaskUUID);

    String path =
        String.format(
            "/api/v2/customers/%s/universes/%s/upgrade/software/resume-canary",
            customer.getUuid(), universe.getUniverseUUID());
    ObjectNode body = Json.newObject();
    body.put("task_uuid", pausedTaskUUID.toString());

    Result result = doRequestWithAuthTokenAndBody("POST", path, authToken, body);
    // V2 Play codegen maps YBATask bodies with ok(json) (HTTP 200), not 202, despite OpenAPI 202.
    assertEquals(200, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    // YBATask uses @JsonProperty snake_case in api.v2.models
    assertEquals(newTaskUUID.toString(), json.get("task_uuid").asText());
    assertEquals(universe.getUniverseUUID().toString(), json.get("resource_uuid").asText());

    verify(mockUpgradeUniverseHandler)
        .resumeCanarySoftwareUpgrade(
            eq(customer.getUuid()), eq(universe.getUniverseUUID()), eq(pausedTaskUUID));
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
  public void testV2PrecheckFinalizeAndYsqlMajorUpgradeTrue() throws ApiException {
    SoftwareUpgradeInfoResponse response = new SoftwareUpgradeInfoResponse();
    response.setFinalizeRequired(true);
    response.setYsqlMajorVersionUpgrade(true);
    when(mockUpgradeUniverseHandler.softwareUpgradeInfo(
            eq(customer.getUuid()), eq(universe.getUniverseUUID()), any()))
        .thenReturn(response);
    UniverseSoftwareUpgradePrecheckReq req = new UniverseSoftwareUpgradePrecheckReq();
    req.setYbSoftwareVersion(upgradeRelease.getVersion());
    UniverseSoftwareUpgradePrecheckResp resp =
        apiClient.precheckSoftwareUpgrade(customer.getUuid(), universe.getUniverseUUID(), req);
    assertTrue(resp.getFinalizeRequired());
    assertTrue(resp.getYsqlMajorVersionUpgrade());
  }

  @Test
  public void testV2PrecheckFinalizeAndYsqlMajorUpgradeFalse() throws ApiException {
    SoftwareUpgradeInfoResponse response = new SoftwareUpgradeInfoResponse();
    response.setFinalizeRequired(false);
    response.setYsqlMajorVersionUpgrade(false);
    when(mockUpgradeUniverseHandler.softwareUpgradeInfo(
            eq(customer.getUuid()), eq(universe.getUniverseUUID()), any()))
        .thenReturn(response);
    UniverseSoftwareUpgradePrecheckReq req = new UniverseSoftwareUpgradePrecheckReq();
    req.setYbSoftwareVersion(upgradeRelease.getVersion());
    UniverseSoftwareUpgradePrecheckResp resp =
        apiClient.precheckSoftwareUpgrade(customer.getUuid(), universe.getUniverseUUID(), req);
    assertFalse(resp.getFinalizeRequired());
    assertFalse(resp.getYsqlMajorVersionUpgrade());
  }

  @Test
  public void testV2SystemdEnable() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    Universe cronUniverse = ModelFactory.createUniverse("universe-nosysd", customer.getId(), false);
    when(mockUpgradeUniverseHandler.upgradeSystemd(any(), eq(customer), eq(cronUniverse)))
        .thenReturn(taskUUID);
    UniverseSystemdEnableStart req = new UniverseSystemdEnableStart();
    req.setSleepAfterTserverRestartMillis(10000);
    YBATask resp = apiClient.systemdEnable(customer.getUuid(), cronUniverse.getUniverseUUID(), req);
    ArgumentCaptor<SystemdUpgradeParams> captor =
        ArgumentCaptor.forClass(SystemdUpgradeParams.class);
    verify(mockUpgradeUniverseHandler)
        .upgradeSystemd(captor.capture(), eq(customer), eq(cronUniverse));
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

  @Test
  public void testV2UniverseUpdateProxyConfig() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    String httpProxy = "httpProxy";
    String httpsProxy = "httpsProxy";
    List<String> noProxy = Arrays.asList("1", "2");

    String httpsProxy1 = "httpsProxy1";

    UniverseDefinitionTaskParams.Cluster cluster = universe.getUniverseDetails().clusters.get(0);
    UUID placementUUID = UUID.randomUUID();

    when(mockUpgradeUniverseHandler.updateProxyConfig(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseUpdateProxyConfig req =
        new UniverseUpdateProxyConfig()
            .addClustersItem(
                new UniverseUpdateProxyConfigClustersInner()
                    .uuid(cluster.uuid)
                    .networkingSpec(
                        new UpdateProxyConfigSpec()
                            .proxyConfig(
                                new NodeProxyConfig()
                                    .httpProxy(httpProxy)
                                    .httpsProxy(httpsProxy)
                                    .noProxyList(noProxy))
                            .azNetworking(
                                Map.of(
                                    placementUUID.toString(),
                                    new NodeProxyConfig().httpsProxy(httpsProxy1)))));
    YBATask resp = apiClient.updateProxyConfig(customer.getUuid(), universe.getUniverseUUID(), req);
    assertEquals(taskUUID, resp.getTaskUuid());
    ArgumentCaptor<ProxyConfigUpdateParams> captor =
        ArgumentCaptor.forClass(ProxyConfigUpdateParams.class);
    verify(mockUpgradeUniverseHandler)
        .updateProxyConfig(captor.capture(), eq(customer), eq(universe));
    ProxyConfigUpdateParams params = captor.getValue();
    assertEquals(1, params.clusters.size());
    ProxyConfig proxyConfig = params.clusters.get(0).userIntent.getProxyConfig();
    ProxyConfig expected = new ProxyConfig();
    expected.setNoProxyList(noProxy);
    expected.setHttpsProxy(httpsProxy);
    expected.setHttpProxy(httpProxy);
    assertEquals(expected, proxyConfig);

    ProxyConfig azProxyConfig =
        params.clusters.get(0).userIntent.getAZProxyConfigMap().get(placementUUID);
    ProxyConfig expectedAzProxyConfig = new ProxyConfig();
    expectedAzProxyConfig.setHttpsProxy(httpsProxy1);
    assertEquals(expectedAzProxyConfig, azProxyConfig);
  }

  @Test
  public void testV2UniverseUpdateProxyConfigWithProviderSpecs() throws ApiException, IOException {
    setNewUIEnabled();
    UUID universeUUID = setupUniverse(true);
    Universe multicloudUniverse = Universe.getOrBadRequest(universeUUID);
    UUID providerUUID =
        multicloudUniverse
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .getAllProviderUUIDs()
            .iterator()
            .next();
    String regionCode = "us-west-1";
    String azCode = "r1-az-1";
    AvailabilityZone az =
        AvailabilityZone.getByCode(Provider.getOrBadRequest(providerUUID), azCode);

    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.updateProxyConfig(any(), eq(customer), any(Universe.class)))
        .thenReturn(taskUUID);

    String httpProxy = "http://provider-proxy:8080";
    String httpsProxy = "https://provider-proxy:8443";
    String regionHttpsProxy = "https://region-proxy:8443";
    String azHttpsProxy = "https://az-proxy:8443";

    UniverseUpdateProxyConfig req =
        new UniverseUpdateProxyConfig()
            .addClustersItem(
                new UniverseUpdateProxyConfigClustersInner()
                    .uuid(multicloudUniverse.getUniverseDetails().getPrimaryCluster().uuid)
                    .addProviderProxySpecsItem(
                        new PerProviderUpdateProxyConfigSpec()
                            .provider(providerUUID)
                            .proxyConfig(
                                new NodeProxyConfig().httpProxy(httpProxy).httpsProxy(httpsProxy))
                            .regionNetworking(
                                Map.of(
                                    regionCode, new NodeProxyConfig().httpsProxy(regionHttpsProxy)))
                            .azNetworking(
                                Map.of(azCode, new NodeProxyConfig().httpsProxy(azHttpsProxy)))));

    YBATask resp =
        apiClient.updateProxyConfig(customer.getUuid(), multicloudUniverse.getUniverseUUID(), req);
    assertEquals(taskUUID, resp.getTaskUuid());

    ArgumentCaptor<ProxyConfigUpdateParams> captor =
        ArgumentCaptor.forClass(ProxyConfigUpdateParams.class);
    verify(mockUpgradeUniverseHandler)
        .updateProxyConfig(captor.capture(), eq(customer), any(Universe.class));
    ProxyConfigUpdateParams params = captor.getValue();
    UserIntent updatedIntent = params.getPrimaryCluster().userIntent;
    assertTrue(updatedIntent.isMulticloudSupport());

    ProviderSpecification providerSpec = updatedIntent.getProviderSpecification(providerUUID);
    ProxyConfig rootProxy =
        providerSpec.getNodesSpecs().getTserverSpecification().getBackupProxyConfig();
    assertEquals(httpProxy, rootProxy.getHttpProxy());
    assertEquals(httpsProxy, rootProxy.getHttpsProxy());

    HierarchicalNodesSpec.RegionNodesSpec regionNodesSpec =
        providerSpec.getNodesSpecs().getRegionNodesSpecs().stream()
            .filter(r -> regionCode.equals(r.getRegionCode()))
            .findFirst()
            .orElseThrow();
    assertEquals(
        regionHttpsProxy,
        regionNodesSpec.getTserverSpecification().getBackupProxyConfig().getHttpsProxy());

    HierarchicalNodesSpec.AzNodesSpec azNodesSpec =
        regionNodesSpec.getAzNodesSpecs().stream()
            .filter(a -> azCode.equals(a.getAzCode()))
            .findFirst()
            .orElseThrow();
    assertEquals(
        azHttpsProxy, azNodesSpec.getTserverSpecification().getBackupProxyConfig().getHttpsProxy());

    ProxyConfig resolvedAzProxy = updatedIntent.getProxyConfig(az.getUuid());
    assertEquals(azHttpsProxy, resolvedAzProxy.getHttpsProxy());
  }

  @Test
  public void testV2UniverseUpdateProxyConfigProviderProxySpecsRequiresMulticloud() {
    UUID providerUUID =
        UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider);
    UniverseUpdateProxyConfig req =
        new UniverseUpdateProxyConfig()
            .addClustersItem(
                new UniverseUpdateProxyConfigClustersInner()
                    .uuid(universe.getUniverseDetails().getPrimaryCluster().uuid)
                    .addProviderProxySpecsItem(
                        new PerProviderUpdateProxyConfigSpec()
                            .provider(providerUUID)
                            .proxyConfig(new NodeProxyConfig().httpProxy("http://proxy:1234"))));

    ApiException ex =
        assertThrows(
            ApiException.class,
            () -> apiClient.updateProxyConfig(customer.getUuid(), universe.getUniverseUUID(), req));
    assertEquals(400, ex.getCode());
    assertThat(ex.getResponseBody(), containsString("multicloud"));
  }

  @Test
  public void testV2ResizeNodesWithProviderSpecs() throws ApiException, IOException {
    setNewUIEnabled();
    UUID universeUUID = setupUniverse(true);
    Universe multicloudUniverse = Universe.getOrBadRequest(universeUUID);
    UUID providerUUID =
        multicloudUniverse
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .getAllProviderUUIDs()
            .iterator()
            .next();

    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.resizeNode(any(), eq(customer), any(Universe.class)))
        .thenReturn(taskUUID);

    String newInstanceType = "c5.2xlarge";
    int newVolumeSize = 500;
    PerProviderResizeNodesSpec providerNodesSpec =
        new PerProviderResizeNodesSpec()
            .provider(providerUUID)
            .nodesSpec(
                new ResizeProviderRootNodesSpec()
                    .tserverSpecification(
                        new ResizeProviderNodeSpec()
                            .instanceType(newInstanceType)
                            .storageSpec(
                                new ClusterResizeStorageSpec().volumeSize(newVolumeSize))));

    UniverseResizeNodes req =
        new UniverseResizeNodes()
            .sleepAfterMasterRestartMillis(111)
            .sleepAfterTserverRestartMillis(222)
            .addClustersItem(
                new UniverseResizeNodesCluster()
                    .uuid(multicloudUniverse.getUniverseDetails().getPrimaryCluster().uuid)
                    .addProviderNodesSpecsItem(providerNodesSpec));

    YBATask resp =
        apiClient.resizeNodes(customer.getUuid(), multicloudUniverse.getUniverseUUID(), req);
    assertEquals(taskUUID, resp.getTaskUuid());

    ArgumentCaptor<ResizeNodeParams> captor = ArgumentCaptor.forClass(ResizeNodeParams.class);
    verify(mockUpgradeUniverseHandler)
        .resizeNode(captor.capture(), eq(customer), any(Universe.class));
    ResizeNodeParams params = captor.getValue();
    assertEquals((Object) 111, params.sleepAfterMasterRestartMillis);
    assertEquals((Object) 222, params.sleepAfterTServerRestartMillis);

    UserIntent resizedIntent = params.getPrimaryCluster().userIntent;
    assertTrue(resizedIntent.isMulticloudSupport());
    ProviderSpecification resizedProviderSpec =
        resizedIntent.getProviderSpecification(providerUUID);
    assertEquals(
        newInstanceType,
        resizedProviderSpec.getNodesSpecs().getTserverSpecification().getInstanceType());
    assertEquals(
        (Object) newVolumeSize,
        resizedProviderSpec.getNodesSpecs().getTserverSpecification().getDeviceInfo().volumeSize);
  }

  @Test
  public void testV2ResizeNodesProviderNodesSpecsRequiresMulticloud() {
    UUID providerUUID =
        UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider);
    UniverseResizeNodes req =
        new UniverseResizeNodes()
            .addClustersItem(
                new UniverseResizeNodesCluster()
                    .uuid(universe.getUniverseDetails().getPrimaryCluster().uuid)
                    .addProviderNodesSpecsItem(
                        new PerProviderResizeNodesSpec()
                            .provider(providerUUID)
                            .nodesSpec(
                                new ResizeProviderRootNodesSpec()
                                    .tserverSpecification(
                                        new ResizeProviderNodeSpec()
                                            .instanceType("c5.2xlarge")
                                            .storageSpec(
                                                new ClusterResizeStorageSpec().volumeSize(500))))));

    ApiException ex =
        assertThrows(
            ApiException.class,
            () -> apiClient.resizeNodes(customer.getUuid(), universe.getUniverseUUID(), req));
    assertEquals(422, ex.getCode());
    assertThat(ex.getResponseBody(), containsString("multicloud"));
  }
}
