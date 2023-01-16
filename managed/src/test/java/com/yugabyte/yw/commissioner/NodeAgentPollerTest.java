// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.NodeAgentPoller.PollerTask;
import com.yugabyte.yw.commissioner.NodeAgentPoller.PollerTaskParam;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.controllers.handlers.NodeAgentHandler;
import com.yugabyte.yw.forms.NodeAgentForm;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.nodeagent.Server.PingResponse;
import com.yugabyte.yw.nodeagent.Server.ServerInfo;
import java.io.IOException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.Date;
import java.util.UUID;
import org.apache.commons.io.FileUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class NodeAgentPollerTest extends FakeDBApplication {
  @Mock private Config mockAppConfig;
  @Mock private ConfigHelper mockConfigHelper;
  @Mock private PlatformExecutorFactory mockPlatformExecutorFactory;
  @Mock private PlatformScheduler mockPlatformScheduler;
  @Mock private NodeAgentClient mockNodeAgentClient;
  private NodeAgentManager nodeAgentManager;
  private NodeAgentHandler nodeAgentHandler;
  private NodeAgentPoller nodeAgentPoller;
  private Customer customer;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    nodeAgentManager = new NodeAgentManager(mockAppConfig, mockConfigHelper);
    nodeAgentHandler = new NodeAgentHandler(mockAppConfig, nodeAgentManager, mockNodeAgentClient);
    nodeAgentPoller =
        new NodeAgentPoller(
            mockAppConfig,
            mockConfigHelper,
            mockPlatformExecutorFactory,
            mockPlatformScheduler,
            nodeAgentManager,
            mockNodeAgentClient);
    nodeAgentHandler.enableConnectionValidation(false);
    when(mockAppConfig.getString(eq("yb.storage.path"))).thenReturn("/tmp");
  }

  private NodeAgent register(NodeAgentForm payload) {
    NodeAgent nodeAgent = nodeAgentHandler.register(customer.uuid, payload);
    assertNotNull(nodeAgent.uuid);
    nodeAgent = NodeAgent.getOrBadRequest(customer.uuid, nodeAgent.uuid);
    assertEquals(State.REGISTERING, nodeAgent.state);
    payload.state = State.READY.name();
    return nodeAgentHandler.updateState(customer.uuid, nodeAgent.uuid, payload);
  }

  @Test
  public void testExpiry() throws Exception {
    when(mockNodeAgentClient.waitForServerReady(any(), any())).thenThrow(RuntimeException.class);
    NodeAgentForm payload = new NodeAgentForm();
    payload.version = "2.12.0.0";
    payload.name = "node1";
    payload.ip = "10.20.30.40";
    payload.osType = OSType.LINUX.name();
    payload.archType = ArchType.AMD64.name();
    NodeAgent nodeAgent = register(payload);
    UUID nodeAgentUuid = nodeAgent.uuid;
    Date time1 = nodeAgent.updatedAt;
    PollerTaskParam param =
        PollerTaskParam.builder()
            .nodeAgentUuid(nodeAgentUuid)
            .softwareVersion(payload.version)
            .lifetime(Duration.ofMinutes(10))
            .build();
    Thread.sleep(1000);
    nodeAgentPoller.createPollerTask(param).run();
    nodeAgent = NodeAgent.getOrBadRequest(customer.uuid, nodeAgentUuid);
    Date time2 = nodeAgent.updatedAt;
    assertEquals(State.READY, nodeAgent.state);
    // Make sure time is updated.
    assertTrue("Time is updated", time2.equals(time1));
    param =
        PollerTaskParam.builder()
            .nodeAgentUuid(nodeAgentUuid)
            .softwareVersion(payload.version)
            .lifetime(Duration.ofMillis(100))
            .build();
    // Sleep to run after the expiry time.
    Thread.sleep(1000);
    nodeAgentPoller.createPollerTask(param).run();
    assertThrows(
        "Cannot find node agent",
        PlatformServiceException.class,
        () -> NodeAgent.getOrBadRequest(customer.uuid, nodeAgentUuid));
  }

  @Test
  public void testHeartbeat() throws Exception {
    NodeAgentForm payload = new NodeAgentForm();
    payload.version = "2.12.0.0";
    payload.name = "node1";
    payload.ip = "10.20.30.40";
    payload.osType = OSType.LINUX.name();
    payload.archType = ArchType.AMD64.name();
    NodeAgent nodeAgent = register(payload);
    UUID nodeAgentUuid = nodeAgent.uuid;
    Date time1 = nodeAgent.updatedAt;
    PollerTaskParam param =
        PollerTaskParam.builder()
            .nodeAgentUuid(nodeAgentUuid)
            .softwareVersion(payload.version)
            .lifetime(Duration.ofMinutes(5))
            .build();

    PollerTask pollerTask = nodeAgentPoller.createPollerTask(param);
    Thread.sleep(1000);
    // Run to just heartbeat.
    pollerTask.run();
    nodeAgent = NodeAgent.getOrBadRequest(customer.uuid, nodeAgentUuid);
    Date time2 = nodeAgent.updatedAt;
    assertEquals(State.READY, nodeAgent.state);
    // Make sure time is updated.
    assertTrue("Time is not updated " + time1, time2.after(time1));
  }

  @Test
  public void testUpgrade() throws IOException {
    PingResponse pingResponse1 = mock(PingResponse.class);
    PingResponse pingResponse2 = mock(PingResponse.class);
    Path nodeAgentPackage = Paths.get("/tmp/node_agent-2.13.0.0-b12-linux-amd64.tar.gz");
    FileUtils.touch(nodeAgentPackage.toFile());
    ServerInfo serverInfo1 = ServerInfo.newBuilder().setRestartNeeded(true).build();
    ServerInfo serverInfo2 = ServerInfo.newBuilder().setRestartNeeded(false).build();
    when(pingResponse1.getServerInfo()).thenReturn(serverInfo1);
    when(pingResponse2.getServerInfo()).thenReturn(serverInfo2);
    when(mockNodeAgentClient.waitForServerReady(any(), any()))
        .thenReturn(pingResponse2 /* heartbeat call */)
        .thenReturn(pingResponse1 /* after upgrade */)
        .thenReturn(pingResponse2 /* after restart */);
    when(mockConfigHelper.getConfig(eq(ConfigType.SoftwareVersion)))
        .thenReturn(ImmutableMap.of("version", "2.13.0.0"));
    when(mockAppConfig.getString(eq(NodeAgentManager.NODE_AGENT_RELEASES_PATH_PROPERTY)))
        .thenReturn(nodeAgentPackage.getParent().toString());
    NodeAgentForm payload = new NodeAgentForm();
    payload.version = "2.12.0.0";
    payload.name = "node1";
    payload.ip = "10.20.30.40";
    payload.osType = OSType.LINUX.name();
    payload.archType = ArchType.AMD64.name();
    NodeAgent nodeAgent = register(payload);
    Path certDir = nodeAgent.getCertDirPath();
    PollerTaskParam param =
        PollerTaskParam.builder()
            .nodeAgentUuid(nodeAgent.uuid)
            .softwareVersion("2.13.0.0")
            .lifetime(Duration.ofMinutes(5))
            .build();
    PollerTask pollerTask = nodeAgentPoller.createPollerTask(param);
    pollerTask.run();
    nodeAgent = NodeAgent.getOrBadRequest(customer.uuid, nodeAgent.uuid);
    Path newCertDirPath = nodeAgent.getCertDirPath();
    Path mergedCertFile = nodeAgent.getMergedCaCertFilePath();
    // Restart was set, it is not live yet.
    assertEquals(State.UPGRADED, nodeAgent.state);
    assertTrue("Merged cert file does not exist", mergedCertFile.toFile().exists());
    pollerTask.run();
    nodeAgent = NodeAgent.getOrBadRequest(customer.uuid, nodeAgent.uuid);
    // Restart done after running again.
    assertEquals(State.READY, nodeAgent.state);
    assertFalse("Merged cert file still exists", mergedCertFile.toFile().exists());
    assertFalse("Cert dir is not updated", certDir.equals(newCertDirPath));
    verify(mockNodeAgentClient, times(3)).uploadFile(any(), any(), any());
    verify(mockNodeAgentClient, times(1)).startUpgrade(any(), any());
    verify(mockNodeAgentClient, times(2)).finalizeUpgrade(any());
  }
}
