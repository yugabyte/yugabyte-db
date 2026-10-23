// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.nodeagent.ConfigureNodeExporterInput;
import com.yugabyte.yw.nodeagent.ConfigureNodeExporterOutput;
import java.util.Optional;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;

public class ConfigureNodeExporterTlsTest extends CommissionerBaseTest {

  private static final ConfigureNodeExporterOutput OK =
      ConfigureNodeExporterOutput.getDefaultInstance();

  private Universe universe;

  @Before
  public void setUp() {
    universe = ModelFactory.createUniverse(defaultCustomer.getId());
    universe = Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UserIntent intent = u.getUniverseDetails().getPrimaryCluster().userIntent;
              intent.provider = defaultProvider.getUuid().toString();
              intent.enableNodeToNodeEncrypt = true;
              u.getUniverseDetails()
                  .nodeDetailsSet
                  .forEach(
                      n -> n.nodeExporterSystemdLevel = NodeDetails.NodeExporterSystemdLevel.USER);
            });
    when(mockNodeAgentClient.runConfigureNodeExporter(any(), any(), any())).thenReturn(OK);
    setTlsKey(true);
  }

  private void setTlsKey(boolean enabled) {
    app.injector()
        .instanceOf(SettableRuntimeConfigFactory.class)
        .globalRuntimeConf()
        .setValue("yb.node_exporter.tls.enabled", Boolean.toString(enabled));
  }

  private void updateScheme() {
    UniverseTaskParams params = new UniverseTaskParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    ConfigureNodeExporterTls task = AbstractTaskBase.createTask(ConfigureNodeExporterTls.class);
    task.initialize(params);
    task.run();
  }

  private boolean flag() {
    return Universe.getOrBadRequest(universe.getUniverseUUID()).isNodeExporterHttps();
  }

  private ConfigureNodeExporterInput lastInput() {
    ArgumentCaptor<ConfigureNodeExporterInput> input =
        ArgumentCaptor.forClass(ConfigureNodeExporterInput.class);
    verify(mockNodeAgentClient, atLeastOnce())
        .runConfigureNodeExporter(any(), input.capture(), eq("yugabyte"));
    return input.getValue();
  }

  private void verifyCalls(int count) {
    verify(mockNodeAgentClient, times(count)).runConfigureNodeExporter(any(), any(), any());
  }

  @Test
  public void update_allNodesUser_turnsTlsOnEverywhere() {
    updateScheme();
    assertTrue(flag());
    verifyCalls(universe.getNodes().size());
    assertTrue(lastInput().getTlsEnabled());
    assertTrue(lastInput().getCertFile().endsWith(".crt"));
    verify(mockSwamperHelper, atLeastOnce()).writeUniverseTargetJson(any(Universe.class));
  }

  @Test
  public void update_anyNodeNotUser_keepsHttp() {
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u ->
                u.getUniverseDetails().nodeDetailsSet.iterator().next().nodeExporterSystemdLevel =
                    null);
    updateScheme();
    assertFalse(flag());
    verify(mockNodeAgentClient, never()).runConfigureNodeExporter(any(), any(), any());
  }

  @Test
  public void update_unrecordedNode_isReadAgain() {
    // A client that drops this INTERNAL field resets it on edit; the decision reads it back.
    NodeDetails node = universe.getNodes().iterator().next();
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> u.getNode(node.nodeName).nodeExporterSystemdLevel = null);
    NodeAgent nodeAgent = new NodeAgent();
    nodeAgent.setIp(node.cloudInfo.private_ip);
    nodeAgent.setName(node.nodeName);
    nodeAgent.setPort(9070);
    nodeAgent.setCustomerUuid(defaultCustomer.getUuid());
    nodeAgent.setOsType(OSType.LINUX);
    nodeAgent.setArchType(ArchType.AMD64);
    nodeAgent.setVersion("2026.2.0.0");
    nodeAgent.setHome("/home/yugabyte");
    nodeAgent.setState(State.READY);
    nodeAgent.save();
    when(mockNodeAgentClient.detectNodeExporterSystemdLevel(any()))
        .thenReturn(Optional.of(NodeDetails.NodeExporterSystemdLevel.USER));
    updateScheme();
    assertTrue(flag());
    assertEquals(
        NodeDetails.NodeExporterSystemdLevel.USER,
        Universe.getOrBadRequest(universe.getUniverseUUID())
            .getNode(node.nodeName)
            .nodeExporterSystemdLevel);
  }

  @Test
  public void update_breakGlassKeyOff_keepsHttp() {
    setTlsKey(false);
    updateScheme();
    assertFalse(flag());
    verify(mockNodeAgentClient, never()).runConfigureNodeExporter(any(), any(), any());
  }

  @Test
  public void update_cloudMode_keepsHttp() {
    app.injector()
        .instanceOf(SettableRuntimeConfigFactory.class)
        .forCustomer(defaultCustomer)
        .setValue("yb.cloud.enabled", "true");
    updateScheme();
    assertFalse(flag());
    verify(mockNodeAgentClient, never()).runConfigureNodeExporter(any(), any(), any());
  }

  @Test
  public void update_encryptionOff_turnsTlsOff() {
    updateScheme();
    assertTrue(flag());
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u ->
                u.getUniverseDetails().getPrimaryCluster().userIntent.enableNodeToNodeEncrypt =
                    false);
    updateScheme();
    assertFalse(flag());
    assertFalse(lastInput().getTlsEnabled());
  }

  @Test
  public void update_failedChange_putsNodesBack() {
    when(mockNodeAgentClient.runConfigureNodeExporter(any(), any(), any()))
        .thenReturn(OK)
        .thenThrow(new RuntimeException("node down"))
        .thenReturn(OK);
    assertThrows(RuntimeException.class, this::updateScheme);
    assertFalse(flag());
    // One node changed, one failed, then the changed one was put back on plain HTTP.
    verifyCalls(3);
    assertFalse(lastInput().getTlsEnabled());
    verify(mockSwamperHelper, never()).writeUniverseTargetJson(any(Universe.class));
  }

  @Test
  public void update_failedScraperRefresh_retryRedoesIt() {
    doThrow(new RuntimeException("targets not written"))
        .doNothing()
        .when(mockSwamperHelper)
        .writeUniverseTargetJson(any(Universe.class));
    assertThrows(RuntimeException.class, this::updateScheme);
    assertTrue(flag());
    // The retry finds the nodes already switched and only redoes the refresh.
    updateScheme();
    verifyCalls(universe.getNodes().size());
    verify(mockSwamperHelper, times(2)).writeUniverseTargetJson(any(Universe.class));
    // Once the refresh is done, a later decision with nothing to change touches nothing.
    updateScheme();
    verify(mockSwamperHelper, times(2)).writeUniverseTargetJson(any(Universe.class));
  }
}
