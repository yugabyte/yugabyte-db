// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.NodeAgentForm;
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
import java.util.UUID;
import java.util.concurrent.ThreadLocalRandom;
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
    certificateHelper = new CertificateHelper(app.injector().instanceOf(RuntimeConfGetter.class));
    nodeAgentManager = new NodeAgentManager(mockAppConfig, mockConfigHelper, certificateHelper);
    nodeAgentHandler =
        new NodeAgentHandler(mockCommissioner, nodeAgentManager, mockNodeAgentClient);
    nodeAgentHandler.enableConnectionValidation(false);
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
    NodeAgentForm payload = new NodeAgentForm();
    payload.version = "2.12.0";
    payload.name = "node1";
    payload.ip = "10.20.30.40";
    payload.osType = OSType.LINUX.name();
    payload.archType = ArchType.AMD64.name();
    payload.home = "/home/yugabyte/node-agent";
    NodeAgent nodeAgent = nodeAgentHandler.register(customer.getUuid(), payload);
    assertNotNull(nodeAgent.getUuid());
    UUID nodeAgentUuid = nodeAgent.getUuid();
    String serverCert = nodeAgent.getConfig().getServerCert();
    assertNotNull(serverCert);
    Path serverCertPath = nodeAgent.getServerCertFilePath();
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
}
