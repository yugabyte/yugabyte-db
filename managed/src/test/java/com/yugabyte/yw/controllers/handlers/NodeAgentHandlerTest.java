// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.Signature;
import java.util.concurrent.ThreadLocalRandom;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class NodeAgentHandlerTest extends FakeDBApplication {
  @Mock private Config appConfig;
  private NodeAgentHandler nodeAgentHandler;
  private Customer customer;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    nodeAgentHandler = new NodeAgentHandler(appConfig);
    when(appConfig.getString(eq("yb.storage.path"))).thenReturn("/tmp");
  }

  @Test
  public void testRegistrationCert() {
    NodeAgent nodeAgent = new NodeAgent();
    nodeAgent.customerUuid = customer.uuid;
    nodeAgent.version = "2.12.0";
    nodeAgent.ip = "10.20.30.40";
    NodeAgent registeredNodeAgent = nodeAgentHandler.register(nodeAgent);
    verify(appConfig, times(1)).getString(eq("yb.storage.path"));
    String serverCert = registeredNodeAgent.config.get(NodeAgent.SERVER_CERT_PROPERTY);
    assertNotNull(serverCert);
    String serverCertPath = registeredNodeAgent.config.get(NodeAgent.SERVER_CERT_PATH_PROPERTY);
    assertNotNull(serverCertPath);
    assertTrue(Files.exists(Paths.get(serverCertPath)));
    String serverKey = registeredNodeAgent.config.get(NodeAgent.SERVER_KEY_PROPERTY);
    assertNotNull(serverKey);
    String serverKeyPath = registeredNodeAgent.config.get(NodeAgent.SERVER_KEY_PATH_PROPERTY);
    assertNotNull(serverKeyPath);
    assertTrue(Files.exists(Paths.get(serverCertPath)));
    // sign using the private key
    PublicKey publicKey = nodeAgentHandler.getNodeAgentPublicKey(nodeAgent.uuid);
    PrivateKey privateKey = nodeAgentHandler.getNodeAgentPrivateKey(nodeAgent.uuid);

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
}
