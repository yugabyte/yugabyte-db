package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.Mockito.atLeast;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class CheckMemoryTest extends CommissionerBaseTest {

  private Universe defaultUniverse;
  private NodeDetails node;
  private long AVAILABLE_MEMORY_LIMIT_KB = 716800L;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse();
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    node = new NodeDetails();
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "1.2.3.4";
    details.nodeDetailsSet.add(node);
  }

  @Test
  public void testEnoughFreeMemory() {
    CheckMemory.Params params = new CheckMemory.Params();
    params.memoryLimitKB = AVAILABLE_MEMORY_LIMIT_KB;
    params.memoryType = Util.AVAILABLE_MEMORY;
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.nodeIpList =
        defaultUniverse.getUniverseDetails().nodeDetailsSet.stream()
            .map(node -> node.cloudInfo.private_ip)
            .collect(Collectors.toList());
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "Command output:\n2989898";
    shellResponse.code = 0;
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenReturn(shellResponse);
    CheckMemory checkMemoryTask = AbstractTaskBase.createTask(CheckMemory.class);
    checkMemoryTask.initialize(params);
    checkMemoryTask.run();
    verify(mockNodeUniverseManager, times(1)).runCommand(any(), any(), anyList(), any());
  }

  @Test
  public void testFailedShellReponse() {
    CheckMemory.Params params = new CheckMemory.Params();
    params.memoryLimitKB = AVAILABLE_MEMORY_LIMIT_KB;
    params.memoryType = Util.AVAILABLE_MEMORY;
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.nodeIpList =
        defaultUniverse.getUniverseDetails().nodeDetailsSet.stream()
            .map(node -> node.cloudInfo.private_ip)
            .collect(Collectors.toList());
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "COMMAND OUTPUT:";
    shellResponse.code = 0;
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenReturn(shellResponse);
    CheckMemory checkMemoryTask = AbstractTaskBase.createTask(CheckMemory.class);
    checkMemoryTask.initialize(params);
    PlatformServiceException re =
        assertThrows(PlatformServiceException.class, () -> checkMemoryTask.run());
    assertEquals(
        "Failed to fetch " + params.memoryType + " on node " + node.cloudInfo.private_ip,
        re.getMessage());
    verify(mockNodeUniverseManager, atLeast(2)).runCommand(any(), any(), anyList(), any());
  }

  @Test
  public void testInSufficientMemory() {
    CheckMemory.Params params = new CheckMemory.Params();
    params.memoryLimitKB = AVAILABLE_MEMORY_LIMIT_KB;
    params.memoryType = Util.AVAILABLE_MEMORY;
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.nodeIpList =
        defaultUniverse.getUniverseDetails().nodeDetailsSet.stream()
            .map(node -> node.cloudInfo.private_ip)
            .collect(Collectors.toList());
    ShellResponse shellResponse = new ShellResponse();
    long memoryOutput = AVAILABLE_MEMORY_LIMIT_KB - 1;
    shellResponse.message = "Command output:\n" + (memoryOutput);
    shellResponse.code = 0;
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenReturn(shellResponse);
    CheckMemory checkMemoryTask = AbstractTaskBase.createTask(CheckMemory.class);
    checkMemoryTask.initialize(params);
    RuntimeException re = assertThrows(RuntimeException.class, () -> checkMemoryTask.run());
    assertEquals(
        "Insufficient memory " + memoryOutput + "kB available on node " + node.cloudInfo.private_ip,
        re.getMessage());
    verify(mockNodeUniverseManager, times(1)).runCommand(any(), any(), anyList(), any());
  }
}
