// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.ybc;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.BackupRequestParams.ParallelBackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.HashMap;
import java.util.Map;
import org.apache.commons.lang3.StringUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;

@RunWith(MockitoJUnitRunner.class)
public class YbcBackupNodeRetrieverTest extends FakeDBApplication {

  private Universe mockUniverse;
  private Customer mockCustomer;

  @Before
  public void setup() {
    mockCustomer = ModelFactory.testCustomer();
    mockUniverse = ModelFactory.createUniverse(mockCustomer.getCustomerId());
    mockUniverse = ModelFactory.addNodesToUniverse(mockUniverse.universeUUID, 3);
  }

  @Test
  public void testNodesInPoolNewTask() throws InterruptedException {
    Map<String, ParallelBackupState> subTasksMap = new HashMap<>();
    subTasksMap.put("keyspace1", new ParallelBackupState());
    subTasksMap.put("keyspace2", new ParallelBackupState());
    subTasksMap.put("keyspace3", new ParallelBackupState());
    when(mockYbcManager.ybcPingCheck(anyString(), eq(null), anyInt())).thenReturn(true);
    YbcBackupNodeRetriever ybcBackupNodeRetriever =
        new YbcBackupNodeRetriever(mockUniverse.universeUUID, 3);
    ybcBackupNodeRetriever.initializeNodePoolForBackups(subTasksMap);
    // Verify 3 polls are successful.
    String node_ip1 = ybcBackupNodeRetriever.getNodeIpForBackup();
    String node_ip2 = ybcBackupNodeRetriever.getNodeIpForBackup();
    String node_ip3 = ybcBackupNodeRetriever.getNodeIpForBackup();
    assertTrue(StringUtils.isNotBlank(node_ip1));
    assertTrue(StringUtils.isNotBlank(node_ip2));
    assertTrue(StringUtils.isNotBlank(node_ip3));

    // Verify node-ips retrieved are not equal among themselves.
    assertNotEquals(node_ip1, node_ip2);
    assertNotEquals(node_ip2, node_ip3);
    assertNotEquals(node_ip1, node_ip3);

    // Verify next poll returns null.
    assertTrue(StringUtils.isBlank(ybcBackupNodeRetriever.peekNodeIpForBackup()));
  }

  @Test
  public void testNodesInPoolResumedTaskWithRunningBackup() throws InterruptedException {
    Map<String, ParallelBackupState> subTasksMap = new HashMap<>();
    ParallelBackupState bS1 = new ParallelBackupState();
    bS1.nodeIp = "127.0.0.1";
    subTasksMap.put("keyspace1", bS1);
    subTasksMap.put("keyspace2", new ParallelBackupState());
    subTasksMap.put("keyspace3", new ParallelBackupState());
    when(mockYbcManager.ybcPingCheck(anyString(), eq(null), anyInt())).thenReturn(true);
    YbcBackupNodeRetriever ybcBackupNodeRetriever =
        new YbcBackupNodeRetriever(mockUniverse.universeUUID, 3);
    ybcBackupNodeRetriever.initializeNodePoolForBackups(subTasksMap);
    // Verify 2 polls are successful and node-ips are not equal to 127.0.0.1
    String node_ip2 = ybcBackupNodeRetriever.getNodeIpForBackup();
    assertTrue(StringUtils.isNotBlank(node_ip2));
    assertNotEquals(bS1.nodeIp, node_ip2);

    String node_ip3 = ybcBackupNodeRetriever.getNodeIpForBackup();
    assertTrue(StringUtils.isNotBlank(node_ip3));
    assertNotEquals(bS1.nodeIp, node_ip3);

    // Verify node-ips retrieved are not equal among themselves.
    assertNotEquals(node_ip2, node_ip3);

    // Verify next poll returns null.
    assertTrue(StringUtils.isBlank(ybcBackupNodeRetriever.peekNodeIpForBackup()));

    // Add "127.0.0.1" back to pool.
    ybcBackupNodeRetriever.putNodeIPBackToPool(bS1.nodeIp);

    // Verify poll succeeds and ip equal to "127.0.0.1"
    assertTrue(StringUtils.equals(ybcBackupNodeRetriever.getNodeIpForBackup(), "127.0.0.1"));
  }

  @Test
  public void testNodesInPoolResumedTaskWithoutRunningBackup() throws InterruptedException {
    Map<String, ParallelBackupState> subTasksMap = new HashMap<>();
    ParallelBackupState bS1 = new ParallelBackupState();
    bS1.alreadyScheduled = true;
    subTasksMap.put("keyspace1", bS1);
    subTasksMap.put("keyspace2", new ParallelBackupState());
    subTasksMap.put("keyspace3", new ParallelBackupState());
    when(mockYbcManager.ybcPingCheck(anyString(), eq(null), anyInt())).thenReturn(true);
    YbcBackupNodeRetriever ybcBackupNodeRetriever =
        new YbcBackupNodeRetriever(mockUniverse.universeUUID, 3);
    ybcBackupNodeRetriever.initializeNodePoolForBackups(subTasksMap);
    // Verify 3 polls are successful.
    String node_ip1 = ybcBackupNodeRetriever.getNodeIpForBackup();
    String node_ip2 = ybcBackupNodeRetriever.getNodeIpForBackup();
    String node_ip3 = ybcBackupNodeRetriever.getNodeIpForBackup();
    assertTrue(StringUtils.isNotBlank(node_ip1));
    assertTrue(StringUtils.isNotBlank(node_ip2));
    assertTrue(StringUtils.isNotBlank(node_ip3));

    // Verify next poll returns null.
    assertTrue(StringUtils.isBlank(ybcBackupNodeRetriever.peekNodeIpForBackup()));
  }

  @Test
  public void testPoolSizeOne() throws InterruptedException {
    Map<String, ParallelBackupState> subTasksMap = new HashMap<>();
    YbcBackupNodeRetriever ybcBackupNodeRetriever =
        new YbcBackupNodeRetriever(mockUniverse.universeUUID, 1);
    when(mockYbcManager.ybcPingCheck(anyString(), eq(null), anyInt())).thenReturn(true);
    ybcBackupNodeRetriever.initializeNodePoolForBackups(subTasksMap);
    // Verify 1 poll is successful.
    String node_ip1 = ybcBackupNodeRetriever.getNodeIpForBackup();
    assertTrue(StringUtils.isNotBlank(node_ip1));

    // Verify next poll returns null.
    assertTrue(StringUtils.isBlank(ybcBackupNodeRetriever.peekNodeIpForBackup()));
  }

  @Test
  public void testPoolWithFewUnhealthyNodes() throws InterruptedException {
    Map<String, ParallelBackupState> subTasksMap = new HashMap<>();
    subTasksMap.put("keyspace1", new ParallelBackupState());
    subTasksMap.put("keyspace2", new ParallelBackupState());
    subTasksMap.put("keyspace3", new ParallelBackupState());

    when(mockYbcManager.ybcPingCheck(anyString(), eq(null), anyInt()))
        .thenReturn(false)
        .thenReturn(true);
    YbcBackupNodeRetriever ybcBackupNodeRetriever =
        new YbcBackupNodeRetriever(mockUniverse.universeUUID, 3);
    ybcBackupNodeRetriever.initializeNodePoolForBackups(subTasksMap);

    // Verify 2 polls are successful.
    String node_ip1 = ybcBackupNodeRetriever.getNodeIpForBackup();
    String node_ip2 = ybcBackupNodeRetriever.getNodeIpForBackup();

    assertTrue(StringUtils.isNotBlank(node_ip1));
    assertTrue(StringUtils.isNotBlank(node_ip2));

    // Verify next poll returns null.
    assertTrue(StringUtils.isBlank(ybcBackupNodeRetriever.peekNodeIpForBackup()));
  }

  @Test
  public void testAllUnhealthyNodes() {
    Map<String, ParallelBackupState> subTasksMap = new HashMap<>();
    subTasksMap.put("keyspace1", new ParallelBackupState());
    subTasksMap.put("keyspace2", new ParallelBackupState());
    subTasksMap.put("keyspace3", new ParallelBackupState());

    when(mockYbcManager.ybcPingCheck(anyString(), eq(null), anyInt())).thenReturn(false);
    YbcBackupNodeRetriever ybcBackupNodeRetriever =
        new YbcBackupNodeRetriever(mockUniverse.universeUUID, 3);
    assertThrows(
        RuntimeException.class,
        () -> ybcBackupNodeRetriever.initializeNodePoolForBackups(subTasksMap));
  }
}
