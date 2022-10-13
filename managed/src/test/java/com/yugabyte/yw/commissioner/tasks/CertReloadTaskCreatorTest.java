package com.yugabyte.yw.commissioner.tasks;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.mockStatic;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import java.util.Arrays;
import java.util.List;
import java.util.Set;
import java.util.UUID;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.MockedStatic;
import org.mockito.junit.MockitoJUnitRunner;

import com.google.common.collect.Sets;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.subtasks.CertReloadTaskCreator;
import com.yugabyte.yw.commissioner.tasks.subtasks.NodeCertReloadTask;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;

@RunWith(MockitoJUnitRunner.class)
public class CertReloadTaskCreatorTest {

  @Test
  public void reloadCertsTest() {

    // master
    NodeDetails node1 = mock(NodeDetails.class);
    CloudSpecificInfo info1 = mock(CloudSpecificInfo.class);
    when(node1.isTserver).thenReturn(true);
    when(node1.cloudInfo).thenReturn(info1);
    when(info1.private_ip).thenReturn("node1");
    when(node1.masterRpcPort).thenReturn(7100);

    // follower
    NodeDetails node2 = mock(NodeDetails.class);
    CloudSpecificInfo info2 = mock(CloudSpecificInfo.class);
    when(node2.isTserver).thenReturn(true);
    when(node2.cloudInfo).thenReturn(info2);
    when(info2.private_ip).thenReturn("node2");

    List<NodeDetails> nodes = Arrays.asList(node1, node2);
    Set<ServerType> processTypes = Sets.newHashSet(ServerType.YQLSERVER);

    TaskExecutor te = mock(TaskExecutor.class);
    RunnableTask rt = mock(RunnableTask.class);
    SubTaskGroup tg = mock(SubTaskGroup.class);
    List<NodeDetails> masters = Arrays.asList(node1);
    MockedStatic<NodeCertReloadTask> mockedTaskClass = mockStatic(NodeCertReloadTask.class);
    NodeCertReloadTask task = mock(NodeCertReloadTask.class);
    mockedTaskClass.when(NodeCertReloadTask::createTask).thenReturn(task);
    doNothing().when(task).initialize(any());
    when(te.createSubTaskGroup(anyString())).thenReturn(tg);
    doNothing().when(tg).addSubTask(any());

    CertReloadTaskCreator reloadCreator =
        new CertReloadTaskCreator(UUID.randomUUID(), UUID.randomUUID(), rt, te, masters);
    reloadCreator.run(nodes, processTypes);

    // for YQLSERVER, reload certificates should not happen
    verify(task, times(0)).initialize(any());
    verify(tg, times(0)).addSubTask(task);

    processTypes = Sets.newHashSet(ServerType.MASTER, ServerType.TSERVER);
    reloadCreator.run(nodes, processTypes);
  }
}
