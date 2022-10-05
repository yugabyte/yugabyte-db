// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.NodeAgentForm;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.State;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.Signature;
import java.time.Duration;
import java.util.Date;
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
  @Mock private PlatformScheduler mockPlatformScheduler;
  private NodeAgentHandler nodeAgentHandler;
  private Customer customer;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    nodeAgentHandler = new NodeAgentHandler(mockAppConfig, mockConfigHelper, mockPlatformScheduler);
    nodeAgentHandler.enableConnectionValidation(false);
    when(mockAppConfig.getString(eq("yb.storage.path"))).thenReturn("/tmp");
  }

  private void verifyKeys(UUID nodeAgentUuid) {
    // sign using the private key
    PublicKey publicKey = nodeAgentHandler.getNodeAgentPublicKey(nodeAgentUuid);
    PrivateKey privateKey = nodeAgentHandler.getNodeAgentPrivateKey(nodeAgentUuid);

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
    NodeAgent nodeAgent = nodeAgentHandler.register(customer.uuid, payload);
    assertNotNull(nodeAgent.uuid);
    UUID nodeAgentUuid = nodeAgent.uuid;
    verify(mockAppConfig, times(1)).getString(eq("yb.storage.path"));
    String serverCert = nodeAgent.config.get(NodeAgent.SERVER_CERT_PROPERTY);
    assertNotNull(serverCert);
    Path serverCertPath = nodeAgent.getFilePath(NodeAgent.SERVER_CERT_NAME);
    assertNotNull(serverCertPath);
    assertTrue(Files.exists(serverCertPath));
    String serverKey = nodeAgent.config.get(NodeAgent.SERVER_KEY_PROPERTY);
    assertNotNull(serverKey);
    Path serverKeyPath = nodeAgent.getFilePath(NodeAgent.SERVER_KEY_NAME);
    assertNotNull(serverKeyPath);
    assertTrue(Files.exists(serverCertPath));
    // With a real agent, the files are saved locally and ack is sent to the platform.
    // Complete registration.
    payload.state = State.LIVE;
    nodeAgentHandler.updateState(customer.uuid, nodeAgentUuid, payload);
    verifyKeys(nodeAgentUuid);
  }

  @Test
  public void testUpdateRegistration() {
    NodeAgentForm payload = new NodeAgentForm();
    payload.version = "2.12.0";
    payload.name = "node1";
    payload.ip = "10.20.30.40";
    NodeAgent nodeAgent = nodeAgentHandler.register(customer.uuid, payload);
    String certPath = nodeAgent.config.get(NodeAgent.CERT_DIR_PATH_PROPERTY);
    assertNotNull(nodeAgent.uuid);
    UUID nodeAgentUuid = nodeAgent.uuid;
    verify(mockAppConfig, times(1)).getString(eq("yb.storage.path"));
    String serverCert = nodeAgent.config.get(NodeAgent.SERVER_CERT_PROPERTY);
    assertNotNull(serverCert);
    Path serverCertPath = nodeAgent.getFilePath(NodeAgent.SERVER_CERT_NAME);
    assertNotNull(serverCertPath);
    assertTrue(Files.exists(serverCertPath));
    String serverKey = nodeAgent.config.get(NodeAgent.SERVER_KEY_PROPERTY);
    assertNotNull(serverKey);
    Path serverKeyPath = nodeAgent.getFilePath(NodeAgent.SERVER_KEY_NAME);
    assertNotNull(serverKeyPath);
    assertTrue(Files.exists(serverCertPath));
    // With a real node agent, the files are saved locally and ack is sent to the platform.
    payload.state = State.LIVE;
    nodeAgentHandler.updateState(customer.uuid, nodeAgentUuid, payload);
    assertThrows(
        "Invalid current state LIVE, expected state UPGRADING",
        PlatformServiceException.class,
        () -> nodeAgentHandler.updateRegistration(customer.uuid, nodeAgentUuid));
    // Initiate upgrade.
    nodeAgent = NodeAgent.getOrBadRequest(customer.uuid, nodeAgentUuid);
    nodeAgent.state = State.UPGRADE;
    nodeAgent.save();
    // Start upgrading.
    payload.state = State.UPGRADING;
    nodeAgentHandler.updateState(customer.uuid, nodeAgentUuid, payload);
    nodeAgent = nodeAgentHandler.updateRegistration(customer.uuid, nodeAgentUuid);
    assertEquals(certPath, nodeAgent.config.get(NodeAgent.CERT_DIR_PATH_PROPERTY));
    verifyKeys(nodeAgentUuid);
    // Complete upgrading.
    payload.state = State.UPGRADED;
    nodeAgent = nodeAgentHandler.updateState(customer.uuid, nodeAgentUuid, payload);
    assertNotEquals(certPath, nodeAgent.config.get(NodeAgent.CERT_DIR_PATH_PROPERTY));
    certPath = nodeAgent.config.get(NodeAgent.CERT_DIR_PATH_PROPERTY);
    // Restart the node agent and report live to the server.
    payload.state = State.LIVE;
    nodeAgent = nodeAgentHandler.updateState(customer.uuid, nodeAgentUuid, payload);
    assertEquals(certPath, nodeAgent.config.get(NodeAgent.CERT_DIR_PATH_PROPERTY));
    verifyKeys(nodeAgentUuid);
  }

  @Test
  public void testUpdaterServiceNewVersion() {
    when(mockConfigHelper.getConfig(eq(ConfigType.SoftwareVersion)))
        .thenReturn(ImmutableMap.of("version", "2.13.0"));
    NodeAgentForm payload = new NodeAgentForm();
    payload.version = "2.12.0";
    payload.name = "node1";
    payload.ip = "10.20.30.40";
    NodeAgent nodeAgent = nodeAgentHandler.register(customer.uuid, payload);
    assertNotNull(nodeAgent.uuid);
    UUID nodeAgentUuid = nodeAgent.uuid;
    nodeAgentHandler.updaterService();
    nodeAgent = NodeAgent.getOrBadRequest(customer.uuid, nodeAgentUuid);
    assertEquals(State.REGISTERING, nodeAgent.state);
    // With a real agent, the files are saved locally and ack is sent to the platform.
    // Complete registration.
    payload.state = State.LIVE;
    nodeAgentHandler.updateState(customer.uuid, nodeAgentUuid, payload);
    nodeAgentHandler.updaterService();
    nodeAgent = NodeAgent.getOrBadRequest(customer.uuid, nodeAgentUuid);
    assertEquals(State.UPGRADE, nodeAgent.state);
  }

  @Test
  public void testUpdaterServiceSameVersion() {
    when(mockConfigHelper.getConfig(eq(ConfigType.SoftwareVersion)))
        .thenReturn(ImmutableMap.of("version", "2.12.0"));
    NodeAgentForm payload = new NodeAgentForm();
    payload.version = "2.12.0";
    payload.name = "node1";
    payload.ip = "10.20.30.40";
    NodeAgent nodeAgent = nodeAgentHandler.register(customer.uuid, payload);
    assertNotNull(nodeAgent.uuid);
    UUID nodeAgentUuid = nodeAgent.uuid;
    nodeAgentHandler.updaterService();
    nodeAgent = NodeAgent.getOrBadRequest(customer.uuid, nodeAgentUuid);
    assertEquals(State.REGISTERING, nodeAgent.state);
    // With a real agent, the files are saved locally and ack is sent to the platform.
    // Complete registration.
    payload.state = State.LIVE;
    nodeAgentHandler.updateState(customer.uuid, nodeAgentUuid, payload);
    nodeAgentHandler.updaterService();
    nodeAgent = NodeAgent.getOrBadRequest(customer.uuid, nodeAgentUuid);
    assertEquals(State.LIVE, nodeAgent.state);
  }

  @Test
  public void testCleanerService() throws InterruptedException {
    when(mockAppConfig.getDuration(eq(NodeAgentHandler.CLEANER_RETENTION_DURATION_PROPERTY)))
        .thenReturn(Duration.ofMinutes(10))
        .thenReturn(Duration.ofMillis(100));
    NodeAgentForm payload = new NodeAgentForm();
    payload.version = "2.12.0";
    payload.name = "node1";
    payload.ip = "10.20.30.40";
    NodeAgent nodeAgent = nodeAgentHandler.register(customer.uuid, payload);
    assertNotNull(nodeAgent.uuid);
    UUID nodeAgentUuid = nodeAgent.uuid;
    nodeAgent = NodeAgent.getOrBadRequest(customer.uuid, nodeAgentUuid);
    assertEquals(State.REGISTERING, nodeAgent.state);
    // With a real agent, the files are saved locally and ack is sent to the platform.
    // Complete registration.
    payload.state = State.LIVE;
    nodeAgentHandler.updateState(customer.uuid, nodeAgentUuid, payload);
    Thread.sleep(2000);
    nodeAgentHandler.cleanerService();
    nodeAgent = NodeAgent.getOrBadRequest(customer.uuid, nodeAgentUuid);
    Date time1 = nodeAgent.updatedAt;
    assertEquals(State.LIVE, nodeAgent.state);
    // Update state again like heartbeating.
    nodeAgentHandler.updateState(customer.uuid, nodeAgentUuid, payload);
    nodeAgent = NodeAgent.getOrBadRequest(customer.uuid, nodeAgentUuid);
    Date time2 = nodeAgent.updatedAt;
    assertEquals(State.LIVE, nodeAgent.state);
    // Make sure time is updated.
    assertTrue("Time is not updated", time2.after(time1));
    nodeAgentHandler.cleanerService();
    assertThrows(
        "Invalid current state LIVE, expected state UPGRADING",
        PlatformServiceException.class,
        () -> NodeAgent.getOrBadRequest(customer.uuid, nodeAgentUuid));
  }
}
