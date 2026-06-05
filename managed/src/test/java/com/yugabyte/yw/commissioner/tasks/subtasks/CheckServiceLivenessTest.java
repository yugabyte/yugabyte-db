// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.YBClient;

@RunWith(MockitoJUnitRunner.class)
public class CheckServiceLivenessTest extends CommissionerBaseTest {

  private static final String NODE_NAME = "test-n1";
  private static final String NODE_IP = "1.2.3.4";

  private Universe defaultUniverse;
  private YBClient mockClient;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse();

    NodeDetails node = new NodeDetails();
    node.nodeName = NODE_NAME;
    node.placementUuid = defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid;
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = NODE_IP;
    node.isMaster = true;
    node.isTserver = true;

    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    details.nodeDetailsSet.add(node);
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();

    mockClient = mock(YBClient.class);
    // mockYBClient is the YBClientService mock provided by CommissionerBaseTest.
    // isServerAlive() in UniverseTaskBase calls ybService.getClient(masterAddrs, certificate).
    // For a non-TLS universe created by ModelFactory the certificate arg is null.
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
  }

  /** Master and tserver liveness should propagate the configured timeoutMs into waitForServer. */
  @Test
  public void testTimeoutMsPropagatedToYbClient() throws Exception {
    when(mockClient.waitForServer(any(HostAndPort.class), anyLong())).thenReturn(true);

    long expectedTimeoutMs = 60_000L; // simulate the 1m runtime config
    CheckServiceLiveness.Params params = new CheckServiceLiveness.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.nodeName = NODE_NAME;
    params.timeoutMs = expectedTimeoutMs;

    CheckServiceLiveness task = AbstractTaskBase.createTask(CheckServiceLiveness.class);
    task.initialize(params);
    task.run();

    // Master + tserver each call waitForServer once on this node.
    ArgumentCaptor<HostAndPort> hpCaptor = ArgumentCaptor.forClass(HostAndPort.class);
    ArgumentCaptor<Long> timeoutCaptor = ArgumentCaptor.forClass(Long.class);
    verify(mockClient, times(2)).waitForServer(hpCaptor.capture(), timeoutCaptor.capture());

    // Every call must use the configured timeout, not the old 5000ms default.
    for (Long actual : timeoutCaptor.getAllValues()) {
      assertTrue(
          "Expected waitForServer to be called with timeoutMs="
              + expectedTimeoutMs
              + " but got "
              + actual,
          actual == expectedTimeoutMs);
    }

    // Both master and tserver host:port pairs should be probed against the right ports.
    boolean sawMaster = false;
    boolean sawTserver = false;
    for (HostAndPort hp : hpCaptor.getAllValues()) {
      if (hp.getHost().equals(NODE_IP) && hp.getPort() == 7100) {
        sawMaster = true;
      }
      if (hp.getHost().equals(NODE_IP) && hp.getPort() == 9100) {
        sawTserver = true;
      }
    }
    assertTrue("master RPC port (7100) was not probed", sawMaster);
    assertTrue("tserver RPC port (9100) was not probed", sawTserver);
  }

  @Test
  public void testDefaultTimeoutMsIsLegacyValue() throws Exception {
    when(mockClient.waitForServer(any(HostAndPort.class), anyLong())).thenReturn(true);

    CheckServiceLiveness.Params params = new CheckServiceLiveness.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.nodeName = NODE_NAME;
    // Intentionally do not set params.timeoutMs; rely on the field default of 5000.

    CheckServiceLiveness task = AbstractTaskBase.createTask(CheckServiceLiveness.class);
    task.initialize(params);
    task.run();

    // Two calls (master + tserver), both with the default 5000ms.
    verify(mockClient, times(2)).waitForServer(any(HostAndPort.class), eq(30000L));
  }

  /** If the YBClient reports the server is not ready, the subtask must fail. */
  @Test
  public void testFailsWhenServerNotReady() throws Exception {
    when(mockClient.waitForServer(any(HostAndPort.class), anyLong())).thenReturn(false);

    CheckServiceLiveness.Params params = new CheckServiceLiveness.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.nodeName = NODE_NAME;
    params.timeoutMs = 60_000L;

    CheckServiceLiveness task = AbstractTaskBase.createTask(CheckServiceLiveness.class);
    task.initialize(params);

    RuntimeException re = assertThrows(RuntimeException.class, () -> task.run());
    assertTrue(
        "Failure message should list the not-alive services. Got: " + re.getMessage(),
        re.getMessage().contains("MASTER") && re.getMessage().contains("TSERVER"));
  }
}
