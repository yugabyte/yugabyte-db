// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.tasks.NodeExporterSidecarTestUtil;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import org.junit.Before;
import org.junit.Test;

public class ResumeKubernetesUniverseTest extends KubernetesUpgradeTaskTest {

  @Before
  public void setUp() {
    super.setUp();
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
  }

  @Test
  public void testResumeRewritesTargetsFromPodsWithSidecar() {
    assertEquals(1, countForcedTargetRewrites(resume(true /* sidecarKey */)));
  }

  @Test
  public void testResumeDoesNotForceTargetRewriteWithoutSidecar() {
    assertEquals(0, countForcedTargetRewrites(resume(false /* sidecarKey */)));
  }

  private TaskInfo resume(boolean sidecarKey) {
    setupUniverseSingleAZ(true, true);
    // Resume reads no pods before it waits for the servers, so the nodes need their addresses.
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        u -> {
          int i = 1;
          for (NodeDetails node : u.getUniverseDetails().nodeDetailsSet) {
            node.cloudInfo.private_ip = "10.0.0." + i++;
          }
        });
    TestHelper.updateUniverseVersion(
        defaultUniverse, NodeExporterSidecarTestUtil.SUPPORTED_VERSION);
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    RuntimeConfigEntry.upsert(
        defaultUniverse, "yb.kubernetes.node_exporter.enabled", String.valueOf(sidecarKey));
    ResumeKubernetesUniverse.Params params = new ResumeKubernetesUniverse.Params();
    params.customerUUID = defaultCustomer.getUuid();
    TaskInfo taskInfo = submitTask(params, TaskType.ResumeKubernetesUniverse, commissioner);
    assertEquals(taskInfo.getErrorMessage(), Success, taskInfo.getTaskState());
    return taskInfo;
  }

  private static long countForcedTargetRewrites(TaskInfo taskInfo) {
    return taskInfo.getSubTasks().stream()
        .filter(t -> t.getTaskType() == TaskType.SwamperTargetsFileUpdate)
        .filter(t -> t.getTaskParams().path("readPods").asBoolean())
        .count();
  }
}
