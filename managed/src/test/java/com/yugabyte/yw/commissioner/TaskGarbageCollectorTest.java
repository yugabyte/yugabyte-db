package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.commissioner.TaskGarbageCollector.CUSTOMER_TASK_METRIC_NAME;
import static com.yugabyte.yw.commissioner.TaskGarbageCollector.CUSTOMER_UUID_LABEL;
import static com.yugabyte.yw.commissioner.TaskGarbageCollector.NUM_TASK_GC_ERRORS;
import static com.yugabyte.yw.commissioner.TaskGarbageCollector.NUM_TASK_GC_RUNS;
import static com.yugabyte.yw.commissioner.TaskGarbageCollector.TASK_INFO_METRIC_NAME;
import static com.yugabyte.yw.commissioner.TaskGarbageCollector.YB_TASK_GC_GC_CHECK_INTERVAL;
import static io.prometheus.client.CollectorRegistry.defaultRegistry;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyZeroInteractions;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import java.time.Duration;
import java.util.Collections;
import java.util.UUID;
import junit.framework.TestCase;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class TaskGarbageCollectorTest extends TestCase {

  private void checkCounters(
      UUID customerUuid,
      Double expectedNumRuns,
      Double expectedErrors,
      Double expectedCustomerTaskGC,
      Double expectedTaskInfoGC) {
    assertEquals(
        expectedNumRuns, defaultRegistry.getSampleValue(getTotalCounterName(NUM_TASK_GC_RUNS)));
    assertEquals(
        expectedErrors, defaultRegistry.getSampleValue(getTotalCounterName(NUM_TASK_GC_ERRORS)));
    assertEquals(
        expectedCustomerTaskGC,
        defaultRegistry.getSampleValue(
            getTotalCounterName(CUSTOMER_TASK_METRIC_NAME),
            new String[] {CUSTOMER_UUID_LABEL},
            new String[] {customerUuid.toString()}));
    assertEquals(
        expectedTaskInfoGC,
        defaultRegistry.getSampleValue(
            getTotalCounterName(TASK_INFO_METRIC_NAME),
            new String[] {CUSTOMER_UUID_LABEL},
            new String[] {customerUuid.toString()}));
  }

  @Mock PlatformScheduler mockPlatformScheduler;

  @Mock Config mockAppConfig;

  @Mock RuntimeConfigFactory mockRuntimeConfigFactory;

  @Mock Customer mockCustomer;

  @Mock CustomerTask mockCustomerTask;

  private TaskGarbageCollector taskGarbageCollector;

  @Before
  public void setUp() {
    when(mockRuntimeConfigFactory.staticApplicationConf()).thenReturn(mockAppConfig);
    taskGarbageCollector =
        new TaskGarbageCollector(mockPlatformScheduler, mockRuntimeConfigFactory);
    defaultRegistry.clear();
    TaskGarbageCollector.registerMetrics();
  }

  @Test
  public void testStart_disabled() {
    when(mockAppConfig.getDuration(YB_TASK_GC_GC_CHECK_INTERVAL)).thenReturn(Duration.ZERO);
    taskGarbageCollector.start();
    verifyZeroInteractions(mockPlatformScheduler);
  }

  @Test
  public void testStart_enabled() {
    when(mockAppConfig.getDuration(YB_TASK_GC_GC_CHECK_INTERVAL)).thenReturn(Duration.ofDays(1));
    taskGarbageCollector.start();
    verify(mockPlatformScheduler, times(1))
        .schedule(any(), eq(Duration.ZERO), eq(Duration.ofDays(1)), any());
  }

  @Test
  public void testPurge_noneStale() {
    UUID customerUuid = UUID.randomUUID();

    taskGarbageCollector.purgeStaleTasks(mockCustomer, Collections.emptyList());

    checkCounters(customerUuid, 1.0, 0.0, null, null);
  }

  @Test
  public void testPurge() {
    UUID customerUuid = UUID.randomUUID();
    when(mockCustomer.getUuid()).thenReturn(customerUuid);
    // Pretend we deleted 5 rows in all:
    when(mockCustomerTask.cascadeDeleteCompleted()).thenReturn(5);

    taskGarbageCollector.purgeStaleTasks(mockCustomer, Collections.singletonList(mockCustomerTask));

    checkCounters(customerUuid, 1.0, 0.0, 1.0, 4.0);
  }

  // Test that if we do not delete when there are referential integrity issues; then we report such
  // error in counter.
  @Test
  public void testPurge_invalidData() {
    UUID customerUuid = UUID.randomUUID();
    // Pretend we deleted 5 rows in all:
    when(mockCustomerTask.cascadeDeleteCompleted()).thenReturn(0);

    taskGarbageCollector.purgeStaleTasks(mockCustomer, Collections.singletonList(mockCustomerTask));

    checkCounters(customerUuid, 1.0, 1.0, null, null);
  }

  private String getTotalCounterName(String name) {
    return name + "_total";
  }
}
