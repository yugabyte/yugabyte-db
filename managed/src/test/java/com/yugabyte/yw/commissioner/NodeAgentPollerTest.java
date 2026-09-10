// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.NodeAgentPoller.PollerTask;
import com.yugabyte.yw.commissioner.NodeAgentPoller.PollerTaskParam;
import com.yugabyte.yw.commissioner.NodeAgentPoller.PollerTaskState;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.controllers.handlers.NodeAgentHandler;
import com.yugabyte.yw.forms.CertificateParams.CustomCertInfo;
import com.yugabyte.yw.forms.NodeAgentForm;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.DeployContext;
import com.yugabyte.yw.models.NodeAgent.DeployType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.nodeagent.PingResponse;
import com.yugabyte.yw.nodeagent.ServerInfo;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.time.Instant;
import java.util.Calendar;
import java.util.Date;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;
import org.apache.commons.io.FileUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class NodeAgentPollerTest extends FakeDBApplication {
  @Mock private Config mockAppConfig;
  @Mock private ConfigHelper mockConfigHelper;
  @Mock private RuntimeConfGetter mockConfGetter;
  @Mock private PlatformExecutorFactory mockPlatformExecutorFactory;
  @Mock private PlatformScheduler mockPlatformScheduler;
  @Mock private NodeAgentClient mockNodeAgentClient;

  private CertificateHelper certificateHelper;

  private NodeAgentManager nodeAgentManager;
  private NodeAgentHandler nodeAgentHandler;
  private NodeAgentPoller nodeAgentPoller;
  private Customer customer;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.nodeAgentPollerInterval)))
        .thenReturn(Duration.ofSeconds(3));
    lenient()
        .when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.nodeAgentServerCertExpiryNotice)))
        .thenReturn(Duration.ofDays(30));
    // Zero so waitForServerRestart returns on the first restartNeeded ping instead of looping
    // into the next stub (which would complete the upgrade in a single poller cycle).
    lenient()
        .when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.nodeAgentUpgradeRestartWaitTime)))
        .thenReturn(Duration.ZERO);
    // Defensive setUp stub only exercised by a subset of the tests; declared lenient so
    // MockitoJUnitRunner's strict-stub check does not fail on runs whose ordering leaves it unused.
    lenient()
        .when(mockFileHelperService.createTempFile(anyString(), anyString()))
        .thenReturn(Paths.get("/tmp/tmpfile.sh"));
    lenient()
        .when(mockAppConfig.getString(eq(AppConfigHelper.YB_STORAGE_PATH)))
        .thenReturn(TestHelper.TMP_PATH);
    lenient().when(mockAppConfig.getInt(eq("yb.tlsCertificate.root.expiryInYears"))).thenReturn(1);
    lenient()
        .when(mockAppConfig.getInt(eq("yb.tlsCertificate.server.maxLifetimeInYears")))
        .thenReturn(1);
    certificateHelper = new CertificateHelper(app.injector().instanceOf(RuntimeConfGetter.class));
    nodeAgentManager =
        spy(
            new NodeAgentManager(
                mockAppConfig, mockConfigHelper, certificateHelper, mockFileHelperService));
    doReturn(new byte[1]).when(nodeAgentManager).getInstallerScript();
    nodeAgentHandler =
        new NodeAgentHandler(mockCommissioner, nodeAgentManager, mockNodeAgentClient);
    nodeAgentPoller =
        new NodeAgentPoller(
            mockConfGetter,
            mockPlatformExecutorFactory,
            mockPlatformScheduler,
            nodeAgentManager,
            mockNodeAgentClient,
            mockSwamperHelper);
    nodeAgentPoller.init();
    nodeAgentHandler.enableConnectionValidation(false);
  }

  private NodeAgentForm newPayload(String version) {
    NodeAgentForm payload = new NodeAgentForm();
    payload.version = version;
    payload.name = "node1";
    payload.ip = "10.20.30.40";
    payload.osType = OSType.LINUX.name();
    payload.archType = ArchType.AMD64.name();
    payload.home = "/home/yugabyte/node-agent";
    return payload;
  }

  private NodeAgent register(NodeAgentForm payload) {
    NodeAgent nodeAgent = nodeAgentHandler.register(customer.getUuid(), payload);
    assertNotNull(nodeAgent.getUuid());
    nodeAgent = NodeAgent.getOrBadRequest(customer.getUuid(), nodeAgent.getUuid());
    assertEquals(State.REGISTERING, nodeAgent.getState());
    payload.state = State.READY.name();
    return nodeAgentHandler.updateState(customer.getUuid(), nodeAgent.getUuid(), payload);
  }

  private CertificateInfo createSelfSignedCertificate() {
    UUID certUuid =
        certificateHelper.createRootCA(mockAppConfig, "na-custom-ca", customer.getUuid());
    assertNotNull(certUuid);
    return CertificateInfo.getOrBadRequest(certUuid);
  }

  private CertificateInfo createCustomCertHostPathCertificate() throws Exception {
    String certificate = TestHelper.createTempFile("na_custom_ca.crt", "test-ca-cert");
    CustomCertInfo customCertInfo = new CustomCertInfo();
    customCertInfo.rootCertPath = "/var/yugabyte/certs/ca.crt";
    customCertInfo.nodeCertPath = "/var/yugabyte/certs/node.crt";
    customCertInfo.nodeKeyPath = "/var/yugabyte/certs/node.key";
    Calendar cal = Calendar.getInstance();
    Date today = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    return CertificateInfo.create(
        UUID.randomUUID(),
        customer.getUuid(),
        "na-custom-hostpath-ca",
        today,
        cal.getTime(),
        certificate,
        customCertInfo);
  }

  private AtomicBoolean mockUpgradeClientResponses() throws Exception {
    AtomicBoolean restartNeeded = new AtomicBoolean(true);
    ShellResponse shellResponse = ShellResponse.create(0, "Done!");
    Path nodeAgentPackage = Paths.get("/tmp/node_agent-2.13.0.0-b12-linux-amd64.tar.gz");
    FileUtils.touch(nodeAgentPackage.toFile());
    when(mockNodeAgentClient.executeCommand(any(), any())).thenReturn(shellResponse);
    when(mockNodeAgentClient.waitForServerReady(any(), any()))
        .thenAnswer(
            inv ->
                PingResponse.newBuilder()
                    .setServerInfo(
                        ServerInfo.newBuilder().setRestartNeeded(restartNeeded.get()).build())
                    .build());
    when(mockNodeAgentClient.finalizeUpgrade(any())).thenReturn("/home/yugabyte/node-agent");
    when(mockConfigHelper.getConfig(eq(ConfigType.SoftwareVersion)))
        .thenReturn(ImmutableMap.of("version", "2.13.0.0"));
    when(mockAppConfig.getString(eq(NodeAgentManager.NODE_AGENT_RELEASES_PATH_PROPERTY)))
        .thenReturn(nodeAgentPackage.getParent().toString());
    // Poller compares node-agent version against Util.getYbaVersion(), not ConfigHelper.
    Util.setYbaVersion("2.13.0.0");
    return restartNeeded;
  }

  private void setCertExpiringSoon(NodeAgent nodeAgent) {
    nodeAgent
        .getConfig()
        .setServerCertExpirySecs(Instant.now().plus(Duration.ofHours(1)).getEpochSecond());
    nodeAgent.update();
  }

  private DeployType captureDeployType(NodeAgent nodeAgent) throws Exception {
    mockUpgradeClientResponses();
    ExecutorService upgrader = Executors.newSingleThreadExecutor();
    nodeAgentPoller.setUpgradeExecutor(upgrader);
    PollerTaskParam param =
        PollerTaskParam.builder()
            .nodeAgentUuid(nodeAgent.getUuid())
            .lifetime(Duration.ofMinutes(5))
            .build();
    PollerTask pollerTask = nodeAgentPoller.createPollerTask(param);
    pollerTask.setState(PollerTaskState.SCHEDULED);
    pollerTask.run();
    pollerTask.waitForUpgrade();
    ArgumentCaptor<DeployContext> captor = ArgumentCaptor.forClass(DeployContext.class);
    verify(nodeAgentManager, atLeastOnce()).getInstallerFiles(any(), captor.capture());
    return captor.getValue().getDeployType();
  }

  private NodeAgent runUpgradeToReady(NodeAgent nodeAgent) throws Exception {
    AtomicBoolean restartNeeded = mockUpgradeClientResponses();
    ExecutorService upgrader = Executors.newSingleThreadExecutor();
    nodeAgentPoller.setUpgradeExecutor(upgrader);
    Path certDir = nodeAgent.getCertDirPath();
    PollerTaskParam param =
        PollerTaskParam.builder()
            .nodeAgentUuid(nodeAgent.getUuid())
            .lifetime(Duration.ofMinutes(5))
            .build();
    PollerTask pollerTask = nodeAgentPoller.createPollerTask(param);
    pollerTask.setState(PollerTaskState.SCHEDULED);
    pollerTask.run();
    pollerTask.waitForUpgrade();
    nodeAgent = NodeAgent.getOrBadRequest(customer.getUuid(), nodeAgent.getUuid());
    Path newCertDirPath = nodeAgent.getCertDirPath();
    Path mergedCertFile = nodeAgent.getMergedCaCertFilePath();
    assertEquals(State.UPGRADED, nodeAgent.getState());
    boolean certsRolledOver = !certDir.equals(newCertDirPath);
    if (certsRolledOver) {
      assertTrue("Merged cert file does not exist", mergedCertFile.toFile().exists());
    }
    restartNeeded.set(false);
    pollerTask.setState(PollerTaskState.SCHEDULED);
    pollerTask.run();
    pollerTask.waitForUpgrade();
    nodeAgent = NodeAgent.getOrBadRequest(customer.getUuid(), nodeAgent.getUuid());
    assertEquals(State.READY, nodeAgent.getState());
    assertFalse("Merged cert file still exists", mergedCertFile.toFile().exists());
    if (certsRolledOver) {
      assertFalse("Cert dir is not updated", certDir.equals(newCertDirPath));
    }
    return nodeAgent;
  }

  @Test
  public void testExpiry() throws Exception {
    when(mockNodeAgentClient.waitForServerReady(any(), any())).thenThrow(RuntimeException.class);
    NodeAgent nodeAgent = register(newPayload("2.12.0.0"));
    UUID nodeAgentUuid = nodeAgent.getUuid();
    Date time1 = nodeAgent.getUpdatedAt();
    PollerTaskParam param =
        PollerTaskParam.builder()
            .nodeAgentUuid(nodeAgentUuid)
            .lifetime(Duration.ofMinutes(10))
            .build();
    Thread.sleep(1000);
    nodeAgentPoller.createPollerTask(param).run();
    nodeAgent = NodeAgent.getOrBadRequest(customer.getUuid(), nodeAgentUuid);
    Date time2 = nodeAgent.getUpdatedAt();
    assertEquals(State.READY, nodeAgent.getState());
    // Make sure time is updated.
    assertTrue("Time is updated", time2.equals(time1));
    param =
        PollerTaskParam.builder()
            .nodeAgentUuid(nodeAgentUuid)
            .lifetime(Duration.ofMillis(100))
            .build();
    // Sleep to run after the expiry time.
    Thread.sleep(1000);
    PollerTask pollerTask = nodeAgentPoller.createPollerTask(param);
    pollerTask.setState(PollerTaskState.SCHEDULED);
    pollerTask.run();
    assertThrows(
        "Cannot find node agent",
        PlatformServiceException.class,
        () -> NodeAgent.getOrBadRequest(customer.getUuid(), nodeAgentUuid));
  }

  @Test
  public void testHeartbeat() throws Exception {
    NodeAgent nodeAgent = register(newPayload("2.12.0.0"));
    UUID nodeAgentUuid = nodeAgent.getUuid();
    Date time1 = nodeAgent.getUpdatedAt();
    PollerTaskParam param =
        PollerTaskParam.builder()
            .nodeAgentUuid(nodeAgentUuid)
            .lifetime(Duration.ofMinutes(5))
            .build();

    PollerTask pollerTask = nodeAgentPoller.createPollerTask(param);
    pollerTask.setState(PollerTaskState.SCHEDULED);
    Thread.sleep(1000);
    // Run to just heartbeat.
    pollerTask.run();
    nodeAgent = NodeAgent.getOrBadRequest(customer.getUuid(), nodeAgentUuid);
    Date time2 = nodeAgent.getUpdatedAt();
    assertEquals(State.READY, nodeAgent.getState());
    // Make sure time is updated.
    assertTrue("Time is not updated " + time1, time2.after(time1));
  }

  @Test
  public void testUpgrade() throws Exception {
    NodeAgent nodeAgent = register(newPayload("2.12.0.0"));
    runUpgradeToReady(nodeAgent);
    // Version mismatch only: package and installer script.
    verify(mockNodeAgentClient, times(2)).uploadFile(any(), any(), any(), any(), anyInt(), any());
    verify(mockNodeAgentClient, times(1)).startUpgrade(any(), any());
    verify(mockNodeAgentClient, times(2)).finalizeUpgrade(any());
  }

  @Test
  public void testUpgradeFull() throws Exception {
    NodeAgent nodeAgent = register(newPayload("2.12.0.0"));
    setCertExpiringSoon(nodeAgent);
    runUpgradeToReady(nodeAgent);
    // Package, installer, server cert, server key, signer public, signer private.
    verify(mockNodeAgentClient, times(6)).uploadFile(any(), any(), any(), any(), anyInt(), any());
    verify(mockNodeAgentClient, times(1)).startUpgrade(any(), any());
    verify(mockNodeAgentClient, times(2)).finalizeUpgrade(any());
  }

  @Test
  public void testDeployTypeBinaryOnly() throws Exception {
    NodeAgent nodeAgent = register(newPayload("2.12.0.0"));
    assertEquals(DeployType.BINARY_ONLY, captureDeployType(nodeAgent));
  }

  @Test
  public void testDeployTypeCertsOnly() throws Exception {
    NodeAgent nodeAgent = register(newPayload("2.13.0.0"));
    setCertExpiringSoon(nodeAgent);
    assertEquals(DeployType.CERTS_ONLY, captureDeployType(nodeAgent));
  }

  @Test
  public void testDeployTypeFull() throws Exception {
    NodeAgent nodeAgent = register(newPayload("2.12.0.0"));
    setCertExpiringSoon(nodeAgent);
    assertEquals(DeployType.FULL, captureDeployType(nodeAgent));
  }

  @Test
  public void testUpgradeWithSelfSignedCustomCert() throws Exception {
    CertificateInfo certificateInfo = createSelfSignedCertificate();
    NodeAgentForm payload = newPayload("2.12.0.0");
    payload.certificateName = certificateInfo.getLabel();

    // Registration response includes server cert/key for SelfSigned custom certs (not persisted in
    // DB config JSON).
    NodeAgent registering = nodeAgentHandler.register(customer.getUuid(), payload);
    assertEquals(certificateInfo.getUuid(), registering.getCertificateUuid());
    assertNotNull(registering.getConfig().getServerCert());
    assertNotNull(registering.getConfig().getServerKey());
    assertTrue(registering.getCertDirPath().resolve(NodeAgent.SERVER_CERT_NAME).toFile().exists());
    assertTrue(registering.getCertDirPath().resolve(NodeAgent.SERVER_KEY_NAME).toFile().exists());

    payload.state = State.READY.name();
    NodeAgent nodeAgent =
        nodeAgentHandler.updateState(customer.getUuid(), registering.getUuid(), payload);
    assertEquals(certificateInfo.getUuid(), nodeAgent.getCertificateUuid());

    nodeAgent = runUpgradeToReady(nodeAgent);
    assertEquals(certificateInfo.getUuid(), nodeAgent.getCertificateUuid());
    verify(mockNodeAgentClient, times(2)).uploadFile(any(), any(), any(), any(), anyInt(), any());
    verify(mockNodeAgentClient, times(1)).startUpgrade(any(), any());
    verify(mockNodeAgentClient, times(2)).finalizeUpgrade(any());
  }

  @Test
  public void testUpgradeWithCustomCertHostPath() throws Exception {
    CertificateInfo certificateInfo = createCustomCertHostPathCertificate();
    NodeAgentForm payload = newPayload("2.12.0.0");
    payload.certificateName = certificateInfo.getLabel();

    // Host-path certs omit server cert/key from the registration response.
    NodeAgent registering = nodeAgentHandler.register(customer.getUuid(), payload);
    assertEquals(certificateInfo.getUuid(), registering.getCertificateUuid());
    assertTrue(
        registering.getConfig().getServerCert() == null
            || registering.getConfig().getServerCert().isEmpty());
    assertTrue(
        registering.getConfig().getServerKey() == null
            || registering.getConfig().getServerKey().isEmpty());

    payload.state = State.READY.name();
    NodeAgent nodeAgent =
        nodeAgentHandler.updateState(customer.getUuid(), registering.getUuid(), payload);
    assertEquals(certificateInfo.getUuid(), nodeAgent.getCertificateUuid());

    nodeAgent = runUpgradeToReady(nodeAgent);
    assertEquals(certificateInfo.getUuid(), nodeAgent.getCertificateUuid());
    verify(mockNodeAgentClient, times(2)).uploadFile(any(), any(), any(), any(), anyInt(), any());
    verify(mockNodeAgentClient, times(1)).startUpgrade(any(), any());
    verify(mockNodeAgentClient, times(2)).finalizeUpgrade(any());
  }
}
