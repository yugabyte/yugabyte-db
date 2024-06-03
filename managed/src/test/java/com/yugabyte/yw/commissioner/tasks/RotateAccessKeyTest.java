// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.tasks.params.RotateAccessKeyParams;
import com.yugabyte.yw.common.AccessKeyRotationUtilTest;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@Slf4j
@RunWith(MockitoJUnitRunner.Silent.class)
public class RotateAccessKeyTest extends UniverseModifyBaseTest {

  private static final List<TaskType> ROTATE_ACCESS_KEY_SEQUENCE_PRIMARY_CLUSTER =
      ImmutableList.of(
          TaskType.VerifyNodeSSHAccess, // yugabyte user
          TaskType.VerifyNodeSSHAccess, // centos (ssh_user) user
          TaskType.AddAuthorizedKey, // yugabyte user
          TaskType.AddAuthorizedKey, // centos user
          TaskType.RemoveAuthorizedKey, // centos user
          TaskType.RemoveAuthorizedKey, // yugabyte user
          TaskType.UpdateUniverseAccessKey); // primary cluster

  private static final List<TaskType> ROTATE_ACCESS_KEY_SEQUENCE_WITH_READ_REPLICA =
      ImmutableList.of(
          // primary cluster steps
          TaskType.VerifyNodeSSHAccess, // yugabyte user
          TaskType.VerifyNodeSSHAccess, // centos (ssh_user) user
          TaskType.AddAuthorizedKey, // yugabyte user
          TaskType.AddAuthorizedKey, // centos user
          TaskType.RemoveAuthorizedKey, // centos user
          TaskType.RemoveAuthorizedKey, // yugabyte user
          TaskType.UpdateUniverseAccessKey, // primary cluster

          // read replica steps
          TaskType.VerifyNodeSSHAccess, // yugabyte user
          TaskType.VerifyNodeSSHAccess, // centos user
          TaskType.AddAuthorizedKey, // yugabyte user
          TaskType.AddAuthorizedKey, // centos user
          TaskType.RemoveAuthorizedKey, // centos user
          TaskType.RemoveAuthorizedKey, // yugabyte user
          TaskType.UpdateUniverseAccessKey); // primary cluster

  private AccessKey newAccessKey;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    newAccessKey = createAccessKeyForProvider("new-key", defaultProvider);
  }

  private TaskInfo submitTask(RotateAccessKeyParams taskParams) {
    try {
      UUID taskUUID = commissioner.submit(TaskType.RotateAccessKey, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      log.error("Failing with error {}", e.getMessage());
      fail();
    }
    return null;
  }

  private void assertTaskSequence(
      List<TaskType> sequence, Map<Integer, List<TaskInfo>> subTasksByPosition) {
    int position = 0;
    assertEquals(sequence.size(), subTasksByPosition.size());
    for (TaskType taskType : sequence) {
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      assertTrue(tasks.size() > 0);
      assertEquals(taskType, tasks.get(0).getTaskType());
      position++;
    }
  }

  private RotateAccessKeyParams createRotateAccessKeyParams(
      Universe universe, AccessKey newAccessKey) {
    RotateAccessKeyParams taskParams =
        new RotateAccessKeyParams(
            defaultCustomer.getUuid(),
            defaultProvider.getUuid(),
            universe.getUniverseUUID(),
            newAccessKey);
    return taskParams;
  }

  @Test
  public void testRotateAccessKeySuccess() {
    RotateAccessKeyParams taskParams = createRotateAccessKeyParams(defaultUniverse, newAccessKey);
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(Success, taskInfo.getTaskState());
    assertTaskSequence(ROTATE_ACCESS_KEY_SEQUENCE_PRIMARY_CLUSTER, subTasksByPosition);
  }

  @Test
  public void testRotateAccessKeySuccessWithReadReplica() {
    Universe uni1 = createUniverseForProviderWithReadReplica("uni-1", defaultProvider);
    RotateAccessKeyParams taskParams = createRotateAccessKeyParams(uni1, newAccessKey);
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(Success, taskInfo.getTaskState());
    assertTaskSequence(ROTATE_ACCESS_KEY_SEQUENCE_WITH_READ_REPLICA, subTasksByPosition);
  }

  @Test
  public void testRotateAccessKeyPausedUniverseFailure() {
    AccessKeyRotationUtilTest.setUniversePaused(true, defaultUniverse);
    RotateAccessKeyParams taskParams = createRotateAccessKeyParams(defaultUniverse, newAccessKey);
    PlatformServiceException thrown =
        assertThrows(PlatformServiceException.class, () -> submitTask(taskParams));
    assertThat(thrown.getMessage(), containsString("is currently paused"));
    AccessKeyRotationUtilTest.setUniversePaused(false, defaultUniverse);
  }

  @Test
  public void testRotateAccessKeyNonLiveFailure() {
    setNodeNonLive(true, defaultUniverse);
    RotateAccessKeyParams taskParams = createRotateAccessKeyParams(defaultUniverse, newAccessKey);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    setNodeNonLive(false, defaultUniverse);
  }

  public void setNodeNonLive(boolean setNonLive, Universe universe) {
    UUID clusterUUID = universe.getUniverseDetails().clusters.get(0).uuid;
    String nodeName =
        universe.getNodesInCluster(clusterUUID).stream()
            .collect(Collectors.toList())
            .get(0)
            .nodeName;
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          NodeDetails node = u.getNode(nodeName);
          if (setNonLive) {
            node.state = NodeState.Decommissioned;
          } else {
            node.state = NodeState.Live;
          }
          NodeInstance.maybeGetByName(nodeName)
              .ifPresent(
                  nodeInstance -> {
                    nodeInstance.setState(
                        setNonLive ? NodeInstance.State.FREE : NodeInstance.State.USED);
                    nodeInstance.save();
                  });
        });
  }
}
