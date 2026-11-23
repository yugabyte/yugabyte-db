// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.time.Duration;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;
import org.yb.client.YBClient;

public class SetFlagInMemoryTest extends CommissionerBaseTest {

  private static final String NODE_NAME = "k8s-n1";
  private static final String USER_PG_CONF_CSV = "work_mem=4MB";

  private Universe universe;
  private YBClient mockClient;

  @Before
  public void setUp() throws Exception {
    mockClient = mock(YBClient.class);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
    universe = universeWithNode("k8s-universe", CloudType.kubernetes);
  }

  private Universe universeWithNode(String name, CloudType cloudType) {
    Universe created = createUniverse(name, defaultCustomer.getId(), cloudType);
    return Universe.saveDetails(
        created.getUniverseUUID(),
        u -> {
          NodeDetails node = new NodeDetails();
          node.nodeName = NODE_NAME;
          node.isTserver = true;
          node.isMaster = true;
          node.state = NodeDetails.NodeState.Live;
          node.tserverRpcPort = 9100;
          node.masterRpcPort = 7100;
          node.cloudInfo = new CloudSpecificInfo();
          node.cloudInfo.private_ip = "10.0.0.1";
          node.placementUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
          u.getUniverseDetails().nodeDetailsSet.add(node);
        });
  }

  private SetFlagInMemory.Params params(ServerType serverType) {
    SetFlagInMemory.Params params = new SetFlagInMemory.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = NODE_NAME;
    params.serverType = serverType;
    params.force = true;
    params.gflags = Map.of(GFlagsUtil.YSQL_PG_CONF_CSV, USER_PG_CONF_CSV);
    return params;
  }

  /** A task that does not wait between retries and retries for the given time. */
  private SetFlagInMemory retryingTask(SetFlagInMemory.Params params, Duration timeout) {
    SetFlagInMemory task = spy(app.injector().instanceOf(SetFlagInMemory.class));
    doReturn(timeout).when(task).retryTimeout();
    doNothing().when(task).waitBeforeRetry(any());
    task.initialize(params);
    return task;
  }

  @Test
  public void testFailedRpcFailsTask() throws Exception {
    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(false);
    SetFlagInMemory task = retryingTask(params(ServerType.TSERVER), Duration.ofMinutes(5));
    assertThrows(RuntimeException.class, task::run);
    // The server answered, so the refusal is not retried.
    verify(mockClient).setFlag(any(), anyString(), anyString(), anyBoolean());
    verify(task, never()).waitBeforeRetry(any());
  }

  @Test
  public void testRetriesUntilRestartingServerIsBack() throws Exception {
    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean()))
        .thenThrow(new IOException("Connection reset"))
        .thenThrow(new IOException("Connection reset"))
        .thenReturn(true);
    SetFlagInMemory task = retryingTask(params(ServerType.TSERVER), Duration.ofMinutes(5));
    task.run();
    verify(mockClient, times(3))
        .setFlag(any(), eq(GFlagsUtil.YSQL_PG_CONF_CSV), eq(USER_PG_CONF_CSV), eq(true));
  }

  @Test
  public void testFailsOnceRetryTimeoutPasses() throws Exception {
    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean()))
        .thenThrow(new IOException("Connection reset"));
    SetFlagInMemory task = retryingTask(params(ServerType.TSERVER), Duration.ZERO);
    assertThrows(RuntimeException.class, task::run);
    verify(mockClient).setFlag(any(), anyString(), anyString(), anyBoolean());
  }

  @Test
  public void testVmPushIsNotRetried() throws Exception {
    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean()))
        .thenThrow(new IOException("Connection reset"));
    universe = universeWithNode("vm-universe", CloudType.aws);
    SetFlagInMemory task = retryingTask(params(ServerType.TSERVER), Duration.ofMinutes(5));
    assertThrows(RuntimeException.class, task::run);
    verify(mockClient).setFlag(any(), anyString(), anyString(), anyBoolean());
    verify(task, never()).waitBeforeRetry(any());
  }
}
