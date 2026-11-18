// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class KubernetesOverridesUpgradeTest extends KubernetesUpgradeTaskTest {

  private KubernetesOverridesUpgrade task;

  @Before
  public void setUp() {
    super.setUp();
    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
    this.task =
        new KubernetesOverridesUpgrade(mockBaseTaskDependencies, mockOperatorStatusUpdaterFactory);
  }

  // The sidecar's scrape scheme follows the TLS the upgrade rendered, so the targets are rewritten
  // once the pods have rolled and the overrides are persisted.
  @Test
  public void testOverridesUpgradeRewritesTargetsAtTheEnd() {
    setupUniverseSingleAZ(false, true);
    task.setUserTaskUUID(UUID.randomUUID());
    KubernetesOverridesUpgradeParams params = new KubernetesOverridesUpgradeParams();
    params.universeOverrides = "resource:\n  master:\n    requests:\n      cpu: 1\n";

    TaskInfo taskInfo = submitTask(params, TaskType.KubernetesOverridesUpgrade, commissioner);
    assertEquals(taskInfo.getErrorMessage(), Success, taskInfo.getTaskState());

    List<Integer> rewrites = positions(taskInfo, TaskType.SwamperTargetsFileUpdate);
    assertEquals(1, rewrites.size());
    assertTrue(
        rewrites.get(0) > positions(taskInfo, TaskType.UpdateAndPersistKubernetesOverrides).get(0));
  }

  private static List<Integer> positions(TaskInfo taskInfo, TaskType type) {
    return taskInfo.getSubTasks().stream()
        .filter(t -> t.getTaskType() == type)
        .map(TaskInfo::getPosition)
        .sorted()
        .collect(Collectors.toList());
  }
}
