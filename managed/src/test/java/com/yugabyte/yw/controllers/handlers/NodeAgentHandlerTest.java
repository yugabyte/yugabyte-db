// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers.handlers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.lenient;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.CertificateParams.CustomCertInfo;
import com.yugabyte.yw.forms.NodeAgentForm;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.Signature;
import java.util.Calendar;
import java.util.Date;
import java.util.UUID;
import java.util.concurrent.ThreadLocalRandom;
import org.apache.commons.lang3.StringUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class NodeAgentHandlerTest extends FakeDBApplication {
  @Mock private Config mockAppConfig;
  @Mock private ConfigHelper mockConfigHelper;
  @Mock private PlatformExecutorFactory mockPlatformExecutorFactory;
  @Mock private PlatformScheduler mockPlatformScheduler;
  @Mock private NodeAgentClient mockNodeAgentClient;

  private CertificateHelper certificateHelper;

  private NodeAgentManager nodeAgentManager;
  private NodeAgentHandler nodeAgentHandler;
  private Customer customer;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    lenient()
        .when(mockAppConfig.getString(eq(AppConfigHelper.YB_STORAGE_PATH)))
        .thenReturn(TestHelper.TMP_PATH);
    lenient().when(mockAppConfig.getInt(eq("yb.tlsCertificate.root.expiryInYears"))).thenReturn(1);
    lenient()
        .when(mockAppConfig.getInt(eq("yb.tlsCertificate.server.maxLifetimeInYears")))
        .thenReturn(1);
    certificateHelper = new CertificateHelper(app.injector().instanceOf(RuntimeConfGetter.class));
    nodeAgentManager =
        new NodeAgentManager(
            mockAppConfig, mockConfigHelper, certificateHelper, mockFileHelperService);
    nodeAgentHandler =
        new NodeAgentHandler(mockCommissioner, nodeAgentManager, mockNodeAgentClient);
    nodeAgentHandler.enableConnectionValidation(false);
  }

  private NodeAgentForm newPayload(String ip) {
    NodeAgentForm payload = new NodeAgentForm();
    payload.version = "2.12.0";
    payload.name = "node1";
    payload.ip = ip;
    payload.osType = OSType.LINUX.name();
    payload.archType = ArchType.AMD64.name();
    payload.home = "/home/yugabyte/node-agent";
    return payload;
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

  private void verifyKeys(UUID nodeAgentUuid) {
    // sign using the private key
    PublicKey publicKey = NodeAgentManager.getNodeAgentPublicKey(nodeAgentUuid);
    PrivateKey privateKey = NodeAgentManager.getNodeAgentPrivateKey(nodeAgentUuid);

    try {
      Signature sig = Signature.getInstance("SHA256withRSA");
      sig.initSign(privateKey);
      byte[] challenge = new byte[10000];
      ThreadLocalRandom.current().nextBytes(challenge);
      sig.update(challenge);
      // Sign the challenge with the private key.
      byte[] signature = sig.sign();
      // verify signature using the public key
      sig.initVerify(publicKey);
      sig.update(challenge);
      assertTrue(sig.verify(signature));
    } catch (Exception e) {
      fail();
    }
  }

  @Test
  public void testRegistrationCert() {
    NodeAgentForm payload = newPayload("10.20.30.40");
    NodeAgent nodeAgent = nodeAgentHandler.register(customer.getUuid(), payload);
    assertNotNull(nodeAgent.getUuid());
    assertNull(nodeAgent.getCertificateUuid());
    UUID nodeAgentUuid = nodeAgent.getUuid();
    String serverCert = nodeAgent.getConfig().getServerCert();
    assertNotNull(serverCert);
    Path serverCertPath = nodeAgent.getCertDirPath().resolve(NodeAgent.SERVER_CERT_NAME);
    assertNotNull(serverCertPath);
    assertTrue(Files.exists(serverCertPath));
    String serverKey = nodeAgent.getConfig().getServerKey();
    assertNotNull(serverKey);
    Path serverKeyPath = nodeAgent.getServerKeyFilePath();
    assertNotNull(serverKeyPath);
    assertTrue(Files.exists(serverCertPath));
    // With a real agent, the files are saved locally and ack is sent to the platform.
    // Complete registration.
    payload.state = State.READY.name();
    nodeAgentHandler.updateState(customer.getUuid(), nodeAgentUuid, payload);
    verifyKeys(nodeAgentUuid);
    nodeAgentHandler.unregister(nodeAgentUuid);
    Path certPath = nodeAgentManager.getNodeAgentBaseCertDirectory(nodeAgent);
    assertTrue(!certPath.toFile().exists());
  }

  @Test
  public void testRegistrationWithSelfSignedCustomCert() {
    CertificateInfo certificateInfo = createSelfSignedCertificate();
    NodeAgentForm payload = newPayload("10.20.30.41");
    payload.certificateName = certificateInfo.getLabel();

    NodeAgent nodeAgent = nodeAgentHandler.register(customer.getUuid(), payload);
    assertEquals(certificateInfo.getUuid(), nodeAgent.getCertificateUuid());
    assertTrue(StringUtils.isNotBlank(nodeAgent.getConfig().getServerCert()));
    assertTrue(StringUtils.isNotBlank(nodeAgent.getConfig().getServerKey()));
    assertNull(nodeAgent.getConfig().getServerCertLocalPath());
    assertNull(nodeAgent.getConfig().getServerKeyLocalPath());
    assertTrue(StringUtils.isNotBlank(nodeAgent.getConfig().getSignerPublicKey()));
    assertTrue(StringUtils.isNotBlank(nodeAgent.getConfig().getSignerPrivateKey()));
    assertTrue(Files.exists(nodeAgent.getCertDirPath().resolve(NodeAgent.SERVER_CERT_NAME)));
    assertTrue(Files.exists(nodeAgent.getCertDirPath().resolve(NodeAgent.SERVER_KEY_NAME)));
  }

  @Test
  public void testRegistrationWithCustomCertHostPath() throws Exception {
    CertificateInfo certificateInfo = createCustomCertHostPathCertificate();
    CustomCertInfo customCertInfo = certificateInfo.getCustomCertPathParams();
    NodeAgentForm payload = newPayload("10.20.30.42");
    payload.certificateName = certificateInfo.getLabel();

    NodeAgent nodeAgent = nodeAgentHandler.register(customer.getUuid(), payload);
    assertEquals(certificateInfo.getUuid(), nodeAgent.getCertificateUuid());
    assertTrue(StringUtils.isBlank(nodeAgent.getConfig().getServerCert()));
    assertTrue(StringUtils.isBlank(nodeAgent.getConfig().getServerKey()));
    assertEquals(customCertInfo.nodeCertPath, nodeAgent.getConfig().getServerCertLocalPath());
    assertEquals(customCertInfo.nodeKeyPath, nodeAgent.getConfig().getServerKeyLocalPath());
    assertTrue(StringUtils.isNotBlank(nodeAgent.getConfig().getSignerPublicKey()));
    assertTrue(StringUtils.isNotBlank(nodeAgent.getConfig().getSignerPrivateKey()));
  }

  @Test
  public void testRegistrationWithUnknownCertificateNameFails() {
    NodeAgentForm payload = newPayload("10.20.30.43");
    payload.certificateName = "missing-cert-label";
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> nodeAgentHandler.register(customer.getUuid(), payload));
    assertTrue(exception.getMessage().contains("No certificate with label"));
  }
}
