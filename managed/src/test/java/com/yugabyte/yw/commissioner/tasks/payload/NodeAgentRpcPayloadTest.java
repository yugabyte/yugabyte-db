// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.payload;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import java.util.Collections;
import java.util.Map;
import java.util.UUID;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

public class NodeAgentRpcPayloadTest {
  private static final Map<String, String> SECRET_ENV =
      Collections.singletonMap("YB_OTEL_A_DD_API_KEY", "k");

  private String savedYbaVersion;

  @Before
  public void setUp() {
    savedYbaVersion = Util.getYbaVersion();
    Util.setYbaVersion("2026.2.0.0-b10");
  }

  @After
  public void tearDown() {
    Util.setYbaVersion(savedYbaVersion);
  }

  private static NodeAgent nodeAgent(String version) {
    NodeAgent nodeAgent = new NodeAgent();
    nodeAgent.setIp("10.0.0.1");
    nodeAgent.setVersion(version);
    return nodeAgent;
  }

  @Test
  public void oldNodeAgentWithSecretEnvFails() {
    assertThrows(
        IllegalStateException.class,
        () ->
            NodeAgentRpcPayload.checkNodeAgentReadsSecretEnv(
                nodeAgent("2026.1.0.0-b5"), SECRET_ENV));
  }

  @Test
  public void oldNodeAgentWithoutSecretEnvPasses() {
    NodeAgentRpcPayload.checkNodeAgentReadsSecretEnv(
        nodeAgent("2026.1.0.0-b5"), Collections.emptyMap());
  }

  @Test
  public void upgradedNodeAgentWithSecretEnvPasses() {
    NodeAgentRpcPayload.checkNodeAgentReadsSecretEnv(nodeAgent("2026.2.0.0-b10"), SECRET_ENV);
  }

  @Test
  public void unknownExporterFailsWithBadRequest() {
    TelemetryProviderService telemetryProviderService = mock(TelemetryProviderService.class);
    when(telemetryProviderService.getOrBadRequest(any(UUID.class))).thenCallRealMethod();
    NodeAgentRpcPayload payload =
        new NodeAgentRpcPayload(
            null, null, null, null, telemetryProviderService, null, null, null, null);
    UUID exporterUUID = UUID.randomUUID();
    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () ->
                payload.setupInstallOtelCollectorBitsEnv(
                    null, nodeAgent("2026.2.0.0-b10"), "/tmp", exporterUUID, null, null));
    assertEquals("Invalid Telemetry Provider UUID: " + exporterUUID, e.getMessage());
  }
}
