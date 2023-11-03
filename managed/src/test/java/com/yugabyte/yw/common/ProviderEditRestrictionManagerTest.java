// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.inject.Bindings.bind;

import com.google.common.collect.ArrayListMultimap;
import com.google.common.collect.Multimap;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.commissioner.tasks.upgrade.ThirdpartySoftwareUpgrade;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.ThirdpartySoftwareUpgradeParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.Callable;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Consumer;
import lombok.extern.slf4j.Slf4j;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.mockito.Mockito;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;

@Slf4j
public class ProviderEditRestrictionManagerTest extends CommissionerBaseTest {
  private static final int TIMEOUT_MILLIS = 500;

  private ProviderEditRestrictionManager providerEditRestrictionManager;
  private boolean isEnabled = true;
  private Multimap<UUID, UUID> useProviderIdsByTaskId = ArrayListMultimap.create();
  private Map<UUID, UUID> editProviderIdByTaskId = new HashMap<>();

  public static class FakeTask extends AbstractTaskBase {

    protected FakeTask() {
      super(Mockito.mock(BaseTaskDependencies.class));
    }

    @Override
    public void run() {}
  }

  public static class FakeParams extends AbstractTaskParams {}

  @Override
  protected GuiceApplicationBuilder configureApplication(GuiceApplicationBuilder builder) {
    builder = super.configureApplication(builder);
    ProviderEditRestrictionManager manager =
        new ProviderEditRestrictionManager(
            new RuntimeConfGetter(
                new DummyRuntimeConfigFactoryImpl(Mockito.mock(Config.class)), null)) {
          @Override
          protected long getLockTimeoutMillis() {
            return TIMEOUT_MILLIS;
          }

          @Override
          protected Set<UUID> getProviderUUIDsToUse(UUID taskId, ITask task, ITaskParams params) {
            return new HashSet<>(useProviderIdsByTaskId.get(taskId));
          }

          @Override
          protected Optional<UUID> getProviderUUIDToEdit(
              UUID taskId, ITask task, ITaskParams params) {
            return Optional.ofNullable(editProviderIdByTaskId.get(taskId));
          }

          @Override
          protected boolean isEnabled() {
            return isEnabled;
          }
        };
    return builder.overrides(
        bind(ProviderEditRestrictionManager.class).toInstance(Mockito.spy(manager)));
  }

  @Before
  public void setUp() {
    super.setUp();
    providerEditRestrictionManager =
        app.injector().instanceOf(ProviderEditRestrictionManager.class);
    ;
  }

  @Test
  public void testForNotEnabled() {
    isEnabled = false;
    UUID providerUUID = UUID.randomUUID();
    providerEditRestrictionManager.tryEditProvider(
        providerUUID,
        () -> {
          Boolean res =
              doInParallel(
                  () -> {
                    onCreateEditTask(providerUUID);
                    return true;
                  });
          Assert.assertTrue(res);
        });
  }

  @Test(expected = PlatformServiceException.class)
  public void test2SyncEditsFail() throws Exception {
    UUID providerUUID = UUID.randomUUID();

    providerEditRestrictionManager.tryEditProvider(
        providerUUID,
        () -> {
          doInParallel(
              () -> {
                providerEditRestrictionManager.tryEditProvider(providerUUID, () -> {});
                return true;
              });
        });
  }

  @Test(expected = PlatformServiceException.class)
  public void testSyncEditWhileActiveEditTask() {
    UUID providerUUID = UUID.randomUUID();
    onCreateEditTask(providerUUID);
    Boolean res =
        providerEditRestrictionManager.tryEditProvider(
            providerUUID,
            () -> {
              return true;
            });
  }

  @Test(expected = PlatformServiceException.class)
  public void testAddEditTaskWhileSyncEdit() {
    UUID providerUUID = UUID.randomUUID();
    providerEditRestrictionManager.tryEditProvider(
        providerUUID,
        () -> {
          doInParallel(
              () -> {
                onCreateEditTask(providerUUID);
                return null;
              });
        });
  }

  @Test(expected = PlatformServiceException.class)
  public void testAddUseTaskWhileSyncEdit() {
    UUID providerUUID = UUID.randomUUID();
    onCreateEditTask(providerUUID);
    providerEditRestrictionManager.tryEditProvider(
        providerUUID,
        () -> {
          doInParallel(
              () -> {
                onCreateUseTask(UUID.randomUUID(), providerUUID);
                return null;
              });
        });
  }

  @Test
  public void testAddUseDifferentTaskWhileSyncEdit() {
    UUID providerUUID = UUID.randomUUID();
    boolean res =
        providerEditRestrictionManager.tryEditProvider(
            providerUUID,
            () ->
                doInParallel(
                    () -> {
                      onCreateUseTask(UUID.randomUUID());
                      return true;
                    }));
    Assert.assertTrue(res);
  }

  @Test(expected = PlatformServiceException.class)
  public void test2EditTasksNotAllowed() {
    UUID providerUUID = UUID.randomUUID();
    onCreateEditTask(providerUUID);
    onCreateEditTask(providerUUID);
  }

  @Test
  public void testEditDiffProviders() {
    onCreateEditTask(UUID.randomUUID());
    onCreateEditTask(UUID.randomUUID());
  }

  @Test
  public void testEditAfterUnlock() {
    UUID providerUUID = UUID.randomUUID();
    UUID taskUUID = onCreateEditTask(providerUUID);
    providerEditRestrictionManager.onTaskFinished(taskUUID);
    onCreateEditTask(providerUUID);
  }

  @Test
  public void testEditAfterTaskFailed() {
    UUID providerUUID = UUID.randomUUID();
    UUID taskUUID = onCreateEditTask(providerUUID);
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    taskInfo.setTaskState(TaskInfo.State.Failure);
    taskInfo.save();
    onCreateEditTask(providerUUID);
  }

  @Test
  public void testMultipleUses() {
    UUID providerUUID = UUID.randomUUID();
    UUID taskUUID = onCreateUseTask(providerUUID);
    onCreateUseTask(UUID.randomUUID(), providerUUID);
    onCreateUseTask(UUID.randomUUID());
  }

  @Test(expected = PlatformServiceException.class)
  public void testEditWhileUse() {
    UUID providerUUID = UUID.randomUUID();
    UUID taskUUID = onCreateUseTask(providerUUID);
    onCreateEditTask(providerUUID);
  }

  @Test
  public void testUseAfterEditFinished() {
    UUID providerUUID = UUID.randomUUID();
    UUID taskUUID = onCreateEditTask(providerUUID);
    providerEditRestrictionManager.onTaskFinished(taskUUID);
    onCreateUseTask(providerUUID);
  }

  @Test
  public void testEditAfterUseFinished() {
    UUID providerUUID = UUID.randomUUID();
    UUID taskUUID = onCreateUseTask(UUID.randomUUID(), providerUUID);
    providerEditRestrictionManager.onTaskFinished(taskUUID);
    onCreateEditTask(providerUUID);
  }

  @Test
  public void testListenerIsCalled() {
    ThirdpartySoftwareUpgradeParams params = new ThirdpartySoftwareUpgradeParams();
    Universe universe =
        ModelFactory.createUniverse("nn", defaultCustomer.getId(), defaultProvider.getCloudCode());
    params.setUniverseUUID(universe.getUniverseUUID());
    commissioner.submit(TaskType.ThirdpartySoftwareUpgrade, params);
    Mockito.verify(providerEditRestrictionManager)
        .onTaskCreated(
            Mockito.any(UUID.class),
            Mockito.any(ThirdpartySoftwareUpgrade.class),
            Mockito.eq(params));
  }

  private UUID onCreateEditTask(UUID providerUUID) {
    UUID taskUUID = UUID.randomUUID();
    editProviderIdByTaskId.put(taskUUID, providerUUID);
    TaskInfo taskInfo = createFakeTaskInfo(taskUUID);
    providerEditRestrictionManager.onTaskCreated(taskUUID, new FakeTask(), new FakeParams());
    return taskUUID;
  }

  private UUID onCreateUseTask(UUID... providerUUIDs) {
    UUID taskUUID = UUID.randomUUID();
    TaskInfo taskInfo = createFakeTaskInfo(taskUUID);
    for (UUID providerUUID : providerUUIDs) {
      useProviderIdsByTaskId.put(taskUUID, providerUUID);
    }
    providerEditRestrictionManager.onTaskCreated(taskUUID, new FakeTask(), new FakeParams());
    return taskUUID;
  }

  private TaskInfo createFakeTaskInfo(UUID taskUUID) {
    TaskInfo taskInfo = new TaskInfo(TaskType.ResizeNode);
    taskInfo.setTaskState(TaskInfo.State.Running);
    taskInfo.setTaskUUID(taskUUID);
    taskInfo.setDetails(Json.newObject());
    taskInfo.setOwner("Myself");
    taskInfo.save();
    return taskInfo;
  }

  private <T> T doInParallel(Callable<T> callable) {
    AtomicReference<T> ref = new AtomicReference<>();
    doInParallel(callable, (x) -> ref.set(x));
    return ref.get();
  }

  private <T> void doInParallel(Callable<T> callable, Consumer<T> callback) {
    AtomicReference<T> ref = new AtomicReference<>();
    AtomicReference<RuntimeException> exceptionHolder = new AtomicReference<>();
    Thread th =
        new Thread(
            () -> {
              try {
                ref.set(callable.call());
              } catch (Exception e) {
                if (e instanceof RuntimeException) {
                  exceptionHolder.set((RuntimeException) e);
                } else {
                  exceptionHolder.set(new RuntimeException(e));
                }
              }
            });
    th.start();
    try {
      th.join();
    } catch (InterruptedException e) {
      exceptionHolder.set(new RuntimeException(e));
    }
    if (exceptionHolder.get() != null) {
      throw exceptionHolder.get();
    }
    callback.accept(ref.get());
  }
}
