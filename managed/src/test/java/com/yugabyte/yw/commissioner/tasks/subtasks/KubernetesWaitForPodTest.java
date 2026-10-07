// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
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
import io.fabric8.kubernetes.api.model.PodBuilder;
import java.time.Duration;
import java.util.Collections;
import org.junit.Before;
import org.junit.Test;

public class KubernetesWaitForPodTest extends SubTaskBaseTest {

  private static final int MAX_POLLS = 10;

  private KubernetesManager mockKubernetesManager;
  private TestKubernetesWaitForPod task;

  // Counts waits instead of sleeping; run() is called outside a TaskExecutor here.
  private static class TestKubernetesWaitForPod extends KubernetesWaitForPod {
    int waits = 0;

    TestKubernetesWaitForPod(
        BaseTaskDependencies baseTaskDependencies,
        KubernetesManagerFactory kubernetesManagerFactory) {
      super(baseTaskDependencies, kubernetesManagerFactory);
    }

    @Override
    protected void waitFor(Duration duration) {
      waits++;
    }
  }

  @Before
  public void setUp() {
    super.setUp();
    mockKubernetesManager = mock(KubernetesManager.class);
    KubernetesManagerFactory mockFactory = mock(KubernetesManagerFactory.class);
    when(mockFactory.getManager()).thenReturn(mockKubernetesManager);
    task =
        new TestKubernetesWaitForPod(
            app.injector().instanceOf(BaseTaskDependencies.class), mockFactory);
    KubernetesWaitForPod.Params params = new KubernetesWaitForPod.Params();
    params.commandType = KubernetesWaitForPod.CommandType.WAIT_FOR_POD;
    params.namespace = "test-ns";
    params.podName = "yb-tserver-0";
    params.config = Collections.emptyMap();
    task.initialize(params);
  }

  private static Pod pod(boolean ready) {
    String status = ready ? "True" : "False";
    return new PodBuilder()
        .withNewMetadata()
        .withName("yb-tserver-0")
        .endMetadata()
        .withNewStatus()
        .withPhase("Running")
        .addNewCondition()
        .withType("ContainersReady")
        .withStatus(status)
        .endCondition()
        .addNewCondition()
        .withType("Ready")
        .withStatus(status)
        .endCondition()
        .endStatus()
        .build();
  }

  @Test
  public void testReadyOnFirstPoll() {
    when(mockKubernetesManager.getPodObject(any(), anyString(), anyString())).thenReturn(pod(true));
    task.run();
    verify(mockKubernetesManager, times(1)).getPodObject(any(), anyString(), anyString());
    assertEquals(0, task.waits);
  }

  // A pod whose database container runs but another container is not ready.
  @Test
  public void testNeverReadyFails() {
    when(mockKubernetesManager.getPodObject(any(), anyString(), anyString()))
        .thenReturn(pod(false));
    RuntimeException e = assertThrows(RuntimeException.class, () -> task.run());
    assertTrue(e.getMessage(), e.getMessage().contains("creation taking too long"));
    verify(mockKubernetesManager, times(MAX_POLLS)).getPodObject(any(), anyString(), anyString());
  }

  @Test
  public void testPodNotFoundFails() {
    when(mockKubernetesManager.getPodObject(any(), anyString(), anyString())).thenReturn(null);
    assertThrows(RuntimeException.class, () -> task.run());
  }

  @Test
  public void testApiErrorsUntilTimeoutFail() {
    when(mockKubernetesManager.getPodObject(any(), anyString(), anyString()))
        .thenThrow(new RuntimeException("connection refused"));
    RuntimeException e = assertThrows(RuntimeException.class, () -> task.run());
    assertTrue(e.getMessage(), e.getMessage().contains("creation taking too long"));
  }

  // Ready on the last allowed poll must succeed: a counter-based guard (iters >= MAX) fails here.
  @Test
  public void testReadyOnLastPollSucceeds() {
    Pod notReady = pod(false);
    when(mockKubernetesManager.getPodObject(any(), anyString(), anyString()))
        .thenReturn(
            notReady, notReady, notReady, notReady, notReady, notReady, notReady, notReady,
            notReady, pod(true));
    task.run();
    verify(mockKubernetesManager, times(MAX_POLLS)).getPodObject(any(), anyString(), anyString());
  }
}
