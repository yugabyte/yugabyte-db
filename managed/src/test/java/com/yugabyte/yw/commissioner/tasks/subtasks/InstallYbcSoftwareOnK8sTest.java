// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anySet;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

/**
 * Unit tests covering the race condition guard added to {@link InstallYbcSoftwareOnK8s}. The
 * background {@link com.yugabyte.yw.commissioner.YbcUpgrade} scheduler can attempt to upgrade YBC
 * on the same universe at the same time as this subtask. The guard uses {@code
 * YbcUpgrade.checkYBCUpgradeProcessExistsOnK8s}/{@code setYBCUpgradeProcessOnK8s} so that only one
 * of the two flows touches the pods at a time.
 */
public class InstallYbcSoftwareOnK8sTest extends FakeDBApplication {

  private static final String NEW_YBC_VERSION = "2.0.0-b1";

  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private InstallYbcSoftwareOnK8s task;
  private UUID universeUUID;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    // enableYbc=true so the universe has a YBC version configured, and the default user intent has
    // useYbdbInbuiltYbc=false so the K8s install code path is exercised.
    defaultUniverse =
        ModelFactory.createUniverse(
            "Test-Universe-1",
            UUID.randomUUID(),
            defaultCustomer.getId(),
            CloudType.kubernetes,
            null,
            null,
            true /* enableYbc */);
    universeUUID = defaultUniverse.getUniverseUUID();

    // Create the task via DI so that mockYbcUpgrade (bound in FakeDBApplication) is injected, then
    // wrap in a spy so the K8s subtask methods can be stubbed out without standing up a runnable
    // task / k8s plumbing.
    task = spy(AbstractTaskBase.createTask(InstallYbcSoftwareOnK8s.class));

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.setUniverseUUID(universeUUID);
    params.setYbcSoftwareVersion(NEW_YBC_VERSION);
    task.initialize(params);

    // Stub out methods that would otherwise touch the runnable-task / k8s plumbing.
    SubTaskGroup mockSubTaskGroup = mock(SubTaskGroup.class);
    when(mockSubTaskGroup.setSubTaskGroupType(any())).thenReturn(mockSubTaskGroup);
    doNothing().when(task).installYbcOnThePods(anySet(), anyBoolean(), anyString(), anyMap());
    doNothing().when(task).performYbcAction(anySet(), anyBoolean(), anyString());
    doReturn(mockSubTaskGroup).when(task).createWaitForYbcServerTask(anySet());
    doReturn(mockSubTaskGroup).when(task).createUpdateYbcTask(anyString());
    doReturn(mockSubTaskGroup).when(task).createMarkUniverseUpdateSuccessTasks();
  }

  /**
   * If the {@link com.yugabyte.yw.commissioner.YbcUpgrade} scheduler is already mid-upgrade for
   * this universe, the subtask must bail out early without touching any pods, without taking
   * ownership of the upgrade flag (so we don't clobber the scheduler's bookkeeping), and without
   * marking the universe as failed.
   */
  @Test
  public void testRaceConditionGuardSkipsRunWhenUpgradeProcessAlreadyExists() {
    when(mockYbcUpgrade.checkYBCUpgradeProcessExistsOnK8s(eq(universeUUID))).thenReturn(true);

    task.run();

    verify(mockYbcUpgrade).checkYBCUpgradeProcessExistsOnK8s(eq(universeUUID));
    // The guard must not take ownership of the in-progress flag set by the scheduler.
    verify(mockYbcUpgrade, never()).setYBCUpgradeProcessOnK8s(any());
    // No K8s side-effects should have been queued.
    verify(task, never()).installYbcOnThePods(anySet(), anyBoolean(), anyString(), anyMap());
    verify(task, never()).performYbcAction(anySet(), anyBoolean(), anyString());
    verify(task).createWaitForYbcServerTask(anySet());
    verify(task, never()).createUpdateYbcTask(anyString());
    verify(task, never()).createMarkUniverseUpdateSuccessTasks();
    // We bailed out cleanly - we must not clear the scheduler's flag or mark the universe failed.
    verify(mockYbcUpgrade, never()).removeYBCUpgradeProcessOnK8s(any());
    verify(mockYbcUpgrade, never()).addFailedYBCUpgradeUniverseOnK8s(any());
  }

  /**
   * On the happy path the subtask should claim ownership of the upgrade-in-progress flag before
   * touching the pods, run the install/upgrade subtasks, and release the flag at the end so the
   * background scheduler is free to act on the universe again.
   */
  @Test
  public void testSuccessfulRunSetsAndClearsUpgradeProcessFlag() {
    when(mockYbcUpgrade.checkYBCUpgradeProcessExistsOnK8s(eq(universeUUID))).thenReturn(false);

    task.run();

    verify(mockYbcUpgrade).checkYBCUpgradeProcessExistsOnK8s(eq(universeUUID));
    verify(mockYbcUpgrade).setYBCUpgradeProcessOnK8s(eq(universeUUID));
    verify(task, atLeastOnce())
        .installYbcOnThePods(anySet(), anyBoolean(), eq(NEW_YBC_VERSION), anyMap());
    verify(task, atLeastOnce()).performYbcAction(anySet(), anyBoolean(), eq("stop"));
    verify(mockYbcUpgrade).removeYBCUpgradeProcessOnK8s(eq(universeUUID));
    verify(mockYbcUpgrade, never()).addFailedYBCUpgradeUniverseOnK8s(any());
  }

  /**
   * If the install/upgrade fails after we have taken ownership of the flag, we must mark the
   * universe as failed so the background scheduler will skip it on the next tick instead of
   * retrying immediately. The scheduler is responsible for clearing the in-progress flag on the
   * next tick, so this subtask must not call {@code removeYBCUpgradeProcessOnK8s} from the failure
   * path.
   */
  @Test
  public void testFailedRunMarksUniverseAsFailed() {
    when(mockYbcUpgrade.checkYBCUpgradeProcessExistsOnK8s(eq(universeUUID))).thenReturn(false);
    doThrow(new RuntimeException("boom"))
        .when(task)
        .installYbcOnThePods(anySet(), anyBoolean(), anyString(), anyMap());

    assertThrows(RuntimeException.class, () -> task.run());

    verify(mockYbcUpgrade).setYBCUpgradeProcessOnK8s(eq(universeUUID));
    verify(mockYbcUpgrade).addFailedYBCUpgradeUniverseOnK8s(eq(universeUUID));
    verify(mockYbcUpgrade, never()).removeYBCUpgradeProcessOnK8s(any());
  }
}
