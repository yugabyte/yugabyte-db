// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class CheckDbNodePortConnectivityTest extends CommissionerBaseTest {
  private Universe universe;
  private NodeDetails sourceNode;
  private NodeDetails targetNode;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse();
    sourceNode = new NodeDetails();
    sourceNode.nodeName = "source-node";
    sourceNode.cloudInfo = new CloudSpecificInfo();
    sourceNode.cloudInfo.private_ip = "10.10.10.1";
    sourceNode.isMaster = true;
    sourceNode.isTserver = true;
    sourceNode.masterRpcPort = 7100;
    sourceNode.tserverRpcPort = 9100;

    targetNode = new NodeDetails();
    targetNode.nodeName = "target-node";
    targetNode.cloudInfo = new CloudSpecificInfo();
    targetNode.cloudInfo.private_ip = "10.10.10.2";
    targetNode.isMaster = true;
    targetNode.isTserver = true;
    targetNode.masterRpcPort = 7100;
    targetNode.tserverRpcPort = 9100;

    // Task resolves nodes from universe; add them so getNode() succeeds
    universe.getUniverseDetails().nodeDetailsSet.add(sourceNode);
    universe.getUniverseDetails().nodeDetailsSet.add(targetNode);
    universe.setUniverseDetails(universe.getUniverseDetails());
    universe.save();
  }

  @Test
  public void testSuccess() {
    CheckDbNodePortConnectivity.Params params = new CheckDbNodePortConnectivity.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = sourceNode.nodeName;
    params.nodeDetailsSet = new HashSet<>(Collections.singleton(sourceNode));
    params.sourceNode = sourceNode;
    params.targetNodes = Collections.singletonList(targetNode);

    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenReturn(ShellResponse.create(0, "ok"));

    CheckDbNodePortConnectivity task =
        AbstractTaskBase.createTask(CheckDbNodePortConnectivity.class);
    task.initialize(params);
    task.run();

    verify(mockNodeUniverseManager, atLeastOnce()).runCommand(any(), any(), anyList(), any());
  }

  @Test
  public void testFailure() {
    CheckDbNodePortConnectivity.Params params = new CheckDbNodePortConnectivity.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = sourceNode.nodeName;
    params.nodeDetailsSet = new HashSet<>(Collections.singleton(sourceNode));
    params.sourceNode = sourceNode;
    params.targetNodes = Collections.singletonList(targetNode);

    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenReturn(ShellResponse.create(1, "failure"));

    CheckDbNodePortConnectivity task =
        AbstractTaskBase.createTask(CheckDbNodePortConnectivity.class);
    task.initialize(params);
    RuntimeException error = assertThrows(RuntimeException.class, task::run);
    assertTrue(error.getMessage().contains("Port connectivity check failed (forward)"));
    assertTrue(error.getMessage().contains("10.10.10.2"));
    assertTrue(error.getMessage().contains("7100") || error.getMessage().contains("9100"));
    verify(mockNodeUniverseManager, atLeastOnce()).runCommand(any(), any(), anyList(), any());
  }

  @Test
  public void testRejectsInvalidTargetIp() {
    NodeDetails badTarget = new NodeDetails();
    badTarget.nodeName = "target-node";
    badTarget.cloudInfo = new CloudSpecificInfo();
    badTarget.cloudInfo.private_ip = "10.10.10.2; touch /tmp/pwned";

    // Task resolves nodes from universe; use badTarget so invalid IP is validated
    universe.getUniverseDetails().nodeDetailsSet.remove(targetNode);
    universe.getUniverseDetails().nodeDetailsSet.add(badTarget);
    universe.setUniverseDetails(universe.getUniverseDetails());
    universe.save();

    CheckDbNodePortConnectivity.Params params = new CheckDbNodePortConnectivity.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = sourceNode.nodeName;
    params.nodeDetailsSet = new HashSet<>(Arrays.asList(sourceNode, badTarget));
    params.sourceNode = sourceNode;
    params.targetNodes = Collections.singletonList(badTarget);

    CheckDbNodePortConnectivity task =
        AbstractTaskBase.createTask(CheckDbNodePortConnectivity.class);
    task.initialize(params);
    IllegalArgumentException error = assertThrows(IllegalArgumentException.class, task::run);
    assertEquals(
        "Invalid target address for connectivity check from "
            + sourceNode.nodeName
            + " to target-node: 10.10.10.2; touch /tmp/pwned",
        error.getMessage());
    verify(mockNodeUniverseManager, never()).runCommand(any(), any(), anyList(), any());
  }

  @Test
  public void testRejectsNullSourceNode() {
    CheckDbNodePortConnectivity.Params params = new CheckDbNodePortConnectivity.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = sourceNode.nodeName;
    params.sourceNode = null;
    params.targetNodes = Collections.singletonList(targetNode);

    CheckDbNodePortConnectivity task =
        AbstractTaskBase.createTask(CheckDbNodePortConnectivity.class);
    task.initialize(params);
    IllegalArgumentException error = assertThrows(IllegalArgumentException.class, task::run);
    assertTrue(error.getMessage().contains("requires sourceNode to be set"));
    verify(mockNodeUniverseManager, never()).runCommand(any(), any(), anyList(), any());
  }

  @Test
  public void testRejectsEmptyTargetNodes() {
    CheckDbNodePortConnectivity.Params params = new CheckDbNodePortConnectivity.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = sourceNode.nodeName;
    params.sourceNode = sourceNode;
    params.targetNodes = Collections.emptyList();

    CheckDbNodePortConnectivity task =
        AbstractTaskBase.createTask(CheckDbNodePortConnectivity.class);
    task.initialize(params);
    IllegalArgumentException error = assertThrows(IllegalArgumentException.class, task::run);
    assertTrue(error.getMessage().contains("requires targetNodes to be non-empty"));
    verify(mockNodeUniverseManager, never()).runCommand(any(), any(), anyList(), any());
  }

  @Test
  public void testSuccessWithMultipleTargetNodes() {
    NodeDetails targetNode2 = new NodeDetails();
    targetNode2.nodeName = "target-node-2";
    targetNode2.cloudInfo = new CloudSpecificInfo();
    targetNode2.cloudInfo.private_ip = "10.10.10.3";
    targetNode2.isMaster = true;
    targetNode2.isTserver = true;
    targetNode2.masterRpcPort = 7100;
    targetNode2.tserverRpcPort = 9100;
    universe.getUniverseDetails().nodeDetailsSet.add(targetNode2);
    universe.setUniverseDetails(universe.getUniverseDetails());
    universe.save();

    CheckDbNodePortConnectivity.Params params = new CheckDbNodePortConnectivity.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = sourceNode.nodeName;
    params.nodeDetailsSet = new HashSet<>(Arrays.asList(sourceNode, targetNode, targetNode2));
    params.sourceNode = sourceNode;
    params.targetNodes = Arrays.asList(targetNode, targetNode2);

    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenReturn(ShellResponse.create(0, "ok"));

    CheckDbNodePortConnectivity task =
        AbstractTaskBase.createTask(CheckDbNodePortConnectivity.class);
    task.initialize(params);
    task.run();

    // Per target: 2 ports forward (source->target) + 2 ports reverse (target->source) = 4; 2
    // targets = 8
    verify(mockNodeUniverseManager, times(8)).runCommand(any(), any(), anyList(), any());
  }

  @Test
  public void testSuccessWithBidirectionalCheck() {
    CheckDbNodePortConnectivity.Params params = new CheckDbNodePortConnectivity.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = sourceNode.nodeName;
    params.nodeDetailsSet = new HashSet<>(Arrays.asList(sourceNode, targetNode));
    params.sourceNode = sourceNode;
    params.targetNodes = Collections.singletonList(targetNode);

    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenReturn(ShellResponse.create(0, "ok"));

    CheckDbNodePortConnectivity task =
        AbstractTaskBase.createTask(CheckDbNodePortConnectivity.class);
    task.initialize(params);
    task.run();

    // 2 ports forward (source->target) + 2 ports reverse (target->source) = 4
    verify(mockNodeUniverseManager, times(4)).runCommand(any(), any(), anyList(), any());
  }

  /**
   * Verifies the connectivity check succeeds for a cloud-style node (e.g. AWS/GCP/Azure). The
   * subtask is provider-agnostic and works for both on-prem and cloud.
   */
  @Test
  public void testSuccessWithCloudProviderStyleNode() {
    NodeDetails cloudStyleNode = new NodeDetails();
    cloudStyleNode.nodeName = "yb-aws-us-west-1-n1";
    cloudStyleNode.cloudInfo = new CloudSpecificInfo();
    cloudStyleNode.cloudInfo.private_ip = "172.16.1.10";
    cloudStyleNode.isMaster = true;
    cloudStyleNode.isTserver = true;
    cloudStyleNode.masterRpcPort = 7100;
    cloudStyleNode.tserverRpcPort = 9100;

    NodeDetails cloudTargetNode = new NodeDetails();
    cloudTargetNode.nodeName = "yb-aws-us-west-1-n2";
    cloudTargetNode.cloudInfo = new CloudSpecificInfo();
    cloudTargetNode.cloudInfo.private_ip = "172.16.1.11";
    cloudTargetNode.isMaster = true;
    cloudTargetNode.isTserver = true;
    cloudTargetNode.masterRpcPort = 7100;
    cloudTargetNode.tserverRpcPort = 9100;

    universe.getUniverseDetails().nodeDetailsSet.add(cloudStyleNode);
    universe.getUniverseDetails().nodeDetailsSet.add(cloudTargetNode);
    universe.setUniverseDetails(universe.getUniverseDetails());
    universe.save();

    CheckDbNodePortConnectivity.Params params = new CheckDbNodePortConnectivity.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = cloudStyleNode.nodeName;
    params.nodeDetailsSet = new HashSet<>(Arrays.asList(cloudStyleNode, cloudTargetNode));
    params.sourceNode = cloudStyleNode;
    params.targetNodes = Collections.singletonList(cloudTargetNode);

    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenReturn(ShellResponse.create(0, "ok"));

    CheckDbNodePortConnectivity task =
        AbstractTaskBase.createTask(CheckDbNodePortConnectivity.class);
    task.initialize(params);
    task.run();

    verify(mockNodeUniverseManager, atLeastOnce()).runCommand(any(), any(), anyList(), any());
  }

  /**
   * Verifies the port-check command passes IP and port as separate arguments after "--" so user
   * input is not interpolated into the script (safe from injection). First invocation is forward
   * check from source to target (10.10.10.2:7100).
   */
  @Test
  public void testPython3UsesSafeArguments() {
    CheckDbNodePortConnectivity.Params params = new CheckDbNodePortConnectivity.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = sourceNode.nodeName;
    params.nodeDetailsSet = new HashSet<>(Arrays.asList(sourceNode, targetNode));
    params.sourceNode = sourceNode;
    params.targetNodes = Collections.singletonList(targetNode);

    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenReturn(ShellResponse.create(0, "ok"));

    CheckDbNodePortConnectivity task =
        AbstractTaskBase.createTask(CheckDbNodePortConnectivity.class);
    task.initialize(params);
    task.run();

    @SuppressWarnings("unchecked")
    ArgumentCaptor<List<String>> commandCaptor =
        (ArgumentCaptor<List<String>>) (ArgumentCaptor<?>) ArgumentCaptor.forClass(List.class);
    verify(mockNodeUniverseManager, atLeastOnce())
        .runCommand(any(), any(), commandCaptor.capture(), any());
    // First command is forward check from source to target IP:7100
    List<String> firstCommand = commandCaptor.getAllValues().get(0);
    assertEquals("bash", firstCommand.get(0));
    assertEquals("-c", firstCommand.get(1));
    assertEquals("_", firstCommand.get(3)); // placeholder for $0 so host/port go into $@
    assertEquals("10.10.10.2", firstCommand.get(4));
    assertEquals("7100", firstCommand.get(5));
    // Script must not contain the IP (args come via argv)
    assertFalse(firstCommand.get(2).contains("10.10.10.2"));
    assertTrue(firstCommand.get(2).contains("sys.argv[1]"));
    assertTrue(firstCommand.get(2).contains("sys.argv[2]"));
  }

  @Test
  public void testReverseCheckFailure() {
    CheckDbNodePortConnectivity.Params params = new CheckDbNodePortConnectivity.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = sourceNode.nodeName;
    params.nodeDetailsSet = new HashSet<>(Arrays.asList(sourceNode, targetNode));
    params.sourceNode = sourceNode;
    params.targetNodes = Collections.singletonList(targetNode);

    // Forward checks succeed; reverse check fails (e.g. first reverse call returns exit 1)
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenReturn(ShellResponse.create(0, "ok"))
        .thenReturn(ShellResponse.create(0, "ok"))
        .thenReturn(ShellResponse.create(1, "reverse failed"));

    CheckDbNodePortConnectivity task =
        AbstractTaskBase.createTask(CheckDbNodePortConnectivity.class);
    task.initialize(params);
    RuntimeException error = assertThrows(RuntimeException.class, task::run);
    assertTrue(error.getMessage().contains("Port connectivity check failed (reverse)"));
    assertTrue(error.getMessage().contains("10.10.10.1"));
  }

  @Test
  public void testSuccessWithHostnameTargetNode() {
    NodeDetails hostnameTarget = new NodeDetails();
    hostnameTarget.nodeName = "hostname-target";
    hostnameTarget.cloudInfo = new CloudSpecificInfo();
    hostnameTarget.cloudInfo.private_ip = "abc123.itest.yugabyte.com";
    hostnameTarget.isMaster = true;
    hostnameTarget.isTserver = true;
    hostnameTarget.masterRpcPort = 7100;
    hostnameTarget.tserverRpcPort = 9100;

    // Source also uses a hostname
    sourceNode.cloudInfo.private_ip = "source-host.itest.yugabyte.com";

    universe.getUniverseDetails().nodeDetailsSet.add(hostnameTarget);
    universe.setUniverseDetails(universe.getUniverseDetails());
    universe.save();

    CheckDbNodePortConnectivity.Params params = new CheckDbNodePortConnectivity.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = sourceNode.nodeName;
    params.nodeDetailsSet = new HashSet<>(Arrays.asList(sourceNode, hostnameTarget));
    params.sourceNode = sourceNode;
    params.targetNodes = Collections.singletonList(hostnameTarget);

    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenReturn(ShellResponse.create(0, "ok"));

    CheckDbNodePortConnectivity task =
        AbstractTaskBase.createTask(CheckDbNodePortConnectivity.class);
    task.initialize(params);
    task.run();

    // 2 ports forward + 2 ports reverse = 4
    verify(mockNodeUniverseManager, times(4)).runCommand(any(), any(), anyList(), any());
  }

  @Test
  public void testSkipsTargetWithNoPrivateIp() {
    NodeDetails noIpTarget = new NodeDetails();
    noIpTarget.nodeName = "no-ip-target";
    noIpTarget.cloudInfo = new CloudSpecificInfo();
    noIpTarget.cloudInfo.private_ip = null;
    noIpTarget.isMaster = true;
    noIpTarget.masterRpcPort = 7100;
    universe.getUniverseDetails().nodeDetailsSet.add(noIpTarget);
    universe.setUniverseDetails(universe.getUniverseDetails());
    universe.save();

    CheckDbNodePortConnectivity.Params params = new CheckDbNodePortConnectivity.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = sourceNode.nodeName;
    params.nodeDetailsSet = new HashSet<>(Arrays.asList(sourceNode, targetNode, noIpTarget));
    params.sourceNode = sourceNode;
    params.targetNodes = Arrays.asList(targetNode, noIpTarget);

    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenReturn(ShellResponse.create(0, "ok"));

    CheckDbNodePortConnectivity task =
        AbstractTaskBase.createTask(CheckDbNodePortConnectivity.class);
    task.initialize(params);
    task.run();

    // Only targetNode is checked (4 calls); noIpTarget is skipped
    verify(mockNodeUniverseManager, times(4)).runCommand(any(), any(), anyList(), any());
  }
}
