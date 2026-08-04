// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.Mockito.mockStatic;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.MockedStatic;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class CheckNodeDataDirDiskSpaceTest extends CommissionerBaseTest {

  private static final String NODE_NAME = "test-n1";
  private static final String DATA_DIR = "/mnt/d0";
  // 104004792 KB >= 3 GB (command outputs only this number via awk)
  private static final String DF_NUMBER_SUFFICIENT = "104004792";
  // 1000000 KB < 3 GB
  private static final String DF_NUMBER_INSUFFICIENT = "1000000";

  private Universe defaultUniverse;
  private NodeDetails node;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse();
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    node = new NodeDetails();
    node.nodeName = NODE_NAME;
    node.placementUuid = defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid;
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "1.2.3.4";
    node.isMaster = true;
    node.isTserver = true;
    details.nodeDetailsSet.add(node);
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();
  }

  @Test
  public void testVM_sufficientDiskSpace() {
    try (MockedStatic<Util> utilMock = mockStatic(Util.class)) {
      utilMock.when(() -> Util.getDataDirectoryPath(any(), any(), any())).thenReturn(DATA_DIR);
      ShellResponse response = ShellResponse.create(0, DF_NUMBER_SUFFICIENT);
      when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any())).thenReturn(response);

      CheckNodeDataDirDiskSpace.Params params = new CheckNodeDataDirDiskSpace.Params();
      params.setUniverseUUID(defaultUniverse.getUniverseUUID());
      params.nodeName = NODE_NAME;

      CheckNodeDataDirDiskSpace task = AbstractTaskBase.createTask(CheckNodeDataDirDiskSpace.class);
      task.initialize(params);
      task.run();

      verify(mockNodeUniverseManager, times(1)).runCommand(any(), any(), anyList(), any());
    }
  }

  @Test
  public void testVM_insufficientDiskSpace() {
    try (MockedStatic<Util> utilMock = mockStatic(Util.class)) {
      utilMock.when(() -> Util.getDataDirectoryPath(any(), any(), any())).thenReturn(DATA_DIR);
      ShellResponse response = ShellResponse.create(0, DF_NUMBER_INSUFFICIENT);
      when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any())).thenReturn(response);

      CheckNodeDataDirDiskSpace.Params params = new CheckNodeDataDirDiskSpace.Params();
      params.setUniverseUUID(defaultUniverse.getUniverseUUID());
      params.nodeName = NODE_NAME;

      CheckNodeDataDirDiskSpace task = AbstractTaskBase.createTask(CheckNodeDataDirDiskSpace.class);
      task.initialize(params);
      RuntimeException re = assertThrows(RuntimeException.class, () -> task.run());

      assertTrue(re.getMessage().contains("insufficient free disk space"));
      assertTrue(re.getMessage().contains("required"));
      assertTrue(re.getMessage().contains("available"));
      verify(mockNodeUniverseManager, times(1)).runCommand(any(), any(), anyList(), any());
    }
  }

  @Test
  public void testVM_nodeNotFound() {
    CheckNodeDataDirDiskSpace.Params params = new CheckNodeDataDirDiskSpace.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.nodeName = "nonexistent-node";

    CheckNodeDataDirDiskSpace task = AbstractTaskBase.createTask(CheckNodeDataDirDiskSpace.class);
    task.initialize(params);
    IllegalArgumentException re = assertThrows(IllegalArgumentException.class, () -> task.run());

    assertTrue(re.getMessage().contains("Node nonexistent-node not found"));
    verify(mockNodeUniverseManager, never()).runCommand(any(), any(), anyList(), any());
  }

  @Test
  public void testVM_dfOutputParse_singleNumberWithNewline() {
    try (MockedStatic<Util> utilMock = mockStatic(Util.class)) {
      utilMock.when(() -> Util.getDataDirectoryPath(any(), any(), any())).thenReturn(DATA_DIR);
      // Output may have leading newline (e.g. from wrapper); parser takes last line
      ShellResponse response = ShellResponse.create(0, "\n" + DF_NUMBER_SUFFICIENT);
      when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any())).thenReturn(response);

      CheckNodeDataDirDiskSpace.Params params = new CheckNodeDataDirDiskSpace.Params();
      params.setUniverseUUID(defaultUniverse.getUniverseUUID());
      params.nodeName = NODE_NAME;

      CheckNodeDataDirDiskSpace task = AbstractTaskBase.createTask(CheckNodeDataDirDiskSpace.class);
      task.initialize(params);
      task.run();

      verify(mockNodeUniverseManager, times(1)).runCommand(any(), any(), anyList(), any());
    }
  }

  @Test
  public void testVM_dfCommandFailure() {
    try (MockedStatic<Util> utilMock = mockStatic(Util.class)) {
      utilMock.when(() -> Util.getDataDirectoryPath(any(), any(), any())).thenReturn(DATA_DIR);
      ShellResponse response = ShellResponse.create(1, "df: cannot access path");
      when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any())).thenReturn(response);

      CheckNodeDataDirDiskSpace.Params params = new CheckNodeDataDirDiskSpace.Params();
      params.setUniverseUUID(defaultUniverse.getUniverseUUID());
      params.nodeName = NODE_NAME;

      CheckNodeDataDirDiskSpace task = AbstractTaskBase.createTask(CheckNodeDataDirDiskSpace.class);
      task.initialize(params);
      assertThrows(RuntimeException.class, () -> task.run());

      verify(mockNodeUniverseManager, times(1)).runCommand(any(), any(), anyList(), any());
    }
  }

  @Test
  public void testParse_emptyOutput() {
    try (MockedStatic<Util> utilMock = mockStatic(Util.class)) {
      utilMock.when(() -> Util.getDataDirectoryPath(any(), any(), any())).thenReturn(DATA_DIR);
      ShellResponse response = ShellResponse.create(0, "");
      when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any())).thenReturn(response);

      CheckNodeDataDirDiskSpace.Params params = new CheckNodeDataDirDiskSpace.Params();
      params.setUniverseUUID(defaultUniverse.getUniverseUUID());
      params.nodeName = NODE_NAME;

      CheckNodeDataDirDiskSpace task = AbstractTaskBase.createTask(CheckNodeDataDirDiskSpace.class);
      task.initialize(params);
      RuntimeException re = assertThrows(RuntimeException.class, () -> task.run());

      assertTrue(re.getMessage().contains("Empty or invalid df output"));
      verify(mockNodeUniverseManager, times(1)).runCommand(any(), any(), anyList(), any());
    }
  }

  @Test
  public void testParse_invalidNumber() {
    try (MockedStatic<Util> utilMock = mockStatic(Util.class)) {
      utilMock.when(() -> Util.getDataDirectoryPath(any(), any(), any())).thenReturn(DATA_DIR);
      ShellResponse response = ShellResponse.create(0, "invalid");
      when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any())).thenReturn(response);

      CheckNodeDataDirDiskSpace.Params params = new CheckNodeDataDirDiskSpace.Params();
      params.setUniverseUUID(defaultUniverse.getUniverseUUID());
      params.nodeName = NODE_NAME;

      CheckNodeDataDirDiskSpace task = AbstractTaskBase.createTask(CheckNodeDataDirDiskSpace.class);
      task.initialize(params);
      RuntimeException re = assertThrows(RuntimeException.class, () -> task.run());

      assertTrue(re.getMessage().contains("Failed to parse available disk space"));
      verify(mockNodeUniverseManager, times(1)).runCommand(any(), any(), anyList(), any());
    }
  }
}
