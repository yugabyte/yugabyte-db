// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class RestartUniverseKubernetesUpgradeTest extends KubernetesUpgradeTaskTest {

  @Before
  public void setUp() {
    super.setUp();
    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
  }

  // A restart deletes the pods, which come back on the template they already had, so the
  // sidecar's TLS keeps the record from that template's render.
  @Test
  public void testRestartKeepsSidecarTlsRecord() {
    setupUniverseSingleAZ(false, false);
    recordNodeExporterHttps();
    TaskInfo taskInfo =
        submitTask(
            new RestartTaskParams(), TaskType.RestartUniverseKubernetesUpgrade, commissioner);
    assertEquals(taskInfo.getErrorMessage(), Success, taskInfo.getTaskState());
    assertTrue(isNodeExporterHttpsRecorded());
  }

  // The restart must fail before it touches any pod when KMS cannot return the universe key.
  @Test
  public void testRestartFailsBeforePodRestartWhenKmsKeyUnavailable() {
    setupUniverseSingleAZ(true /* setMasters */, false);
    setUpUniverseKeyInKms(defaultUniverse.getUniverseUUID(), false /* kmsWorks */);
    TaskInfo taskInfo =
        submitTask(
            new RestartTaskParams(), TaskType.RestartUniverseKubernetesUpgrade, commissioner);
    assertEquals(Failure, taskInfo.getTaskState());
    assertKmsCheckFailed(taskInfo);
    assertTrue(
        taskInfo.getSubTasks().stream()
            .noneMatch(t -> t.getTaskType() == TaskType.KubernetesCommandExecutor));
  }
}
