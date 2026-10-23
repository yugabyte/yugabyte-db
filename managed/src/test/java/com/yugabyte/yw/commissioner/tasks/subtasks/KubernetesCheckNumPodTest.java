// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import io.fabric8.kubernetes.api.model.Pod;
import java.time.Duration;
import java.util.Collections;
import java.util.List;
import org.junit.Before;
import org.junit.Test;

public class KubernetesCheckNumPodTest extends SubTaskBaseTest {

  private static final int MAX_POLLS = 10;

  private KubernetesManager mockKubernetesManager;
  private KubernetesCheckNumPod task;

  // Skips the sleep; run() is called outside a TaskExecutor here.
  private static class TestKubernetesCheckNumPod extends KubernetesCheckNumPod {
    TestKubernetesCheckNumPod(
        BaseTaskDependencies baseTaskDependencies,
        KubernetesManagerFactory kubernetesManagerFactory) {
      super(baseTaskDependencies, kubernetesManagerFactory);
    }

    @Override
    protected void waitFor(Duration duration) {}
  }

  @Before
  public void setUp() {
    super.setUp();
    mockKubernetesManager = mock(KubernetesManager.class);
    KubernetesManagerFactory mockFactory = mock(KubernetesManagerFactory.class);
    when(mockFactory.getManager()).thenReturn(mockKubernetesManager);
    task =
        new TestKubernetesCheckNumPod(
            app.injector().instanceOf(BaseTaskDependencies.class), mockFactory);
    KubernetesCheckNumPod.Params params = new KubernetesCheckNumPod.Params();
    params.commandType = KubernetesCheckNumPod.CommandType.WAIT_FOR_PODS;
    params.helmReleaseName = "test-release";
    params.namespace = "test-ns";
    params.podNum = 2;
    params.config = Collections.emptyMap();
    task.initialize(params);
  }

  @Test
  public void testCountNeverMatchesFails() {
    when(mockKubernetesManager.getPodInfos(any(), anyString(), anyString()))
        .thenReturn(List.of(new Pod()));
    RuntimeException e = assertThrows(RuntimeException.class, () -> task.run());
    assertTrue(e.getMessage(), e.getMessage().contains("start taking too long"));
    verify(mockKubernetesManager, times(MAX_POLLS)).getPodInfos(any(), anyString(), anyString());
  }

  // Count matching on the last allowed poll must succeed; the old iters >= MAX guard threw here.
  @Test
  public void testCountMatchesOnLastPollSucceeds() {
    List<Pod> one = List.of(new Pod());
    when(mockKubernetesManager.getPodInfos(any(), anyString(), anyString()))
        .thenReturn(one, one, one, one, one, one, one, one, one, List.of(new Pod(), new Pod()));
    task.run();
    verify(mockKubernetesManager, times(MAX_POLLS)).getPodInfos(any(), anyString(), anyString());
  }
}
