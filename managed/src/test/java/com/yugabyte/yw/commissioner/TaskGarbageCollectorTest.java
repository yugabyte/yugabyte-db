package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.commissioner.TaskGarbageCollector.CUSTOMER_TASK_METRIC_NAME;
import static com.yugabyte.yw.commissioner.TaskGarbageCollector.CUSTOMER_UUID_LABEL;
import static com.yugabyte.yw.commissioner.TaskGarbageCollector.NUM_TASK_GC_ERRORS;
import static com.yugabyte.yw.commissioner.TaskGarbageCollector.NUM_TASK_GC_RUNS;
import static com.yugabyte.yw.commissioner.TaskGarbageCollector.TASK_INFO_METRIC_NAME;
import static com.yugabyte.yw.commissioner.TaskGarbageCollector.YB_TASK_GC_GC_CHECK_INTERVAL;
import static io.prometheus.client.CollectorRegistry.defaultRegistry;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.util.Collections;
import java.util.Date;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class TaskGarbageCollectorTest extends FakeDBApplication {

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

  private final ObjectMapper mapper = new ObjectMapper();

  private TaskGarbageCollector taskGarbageCollector;

  private Customer defaultCustomer;

  @Mock PlatformScheduler mockPlatformScheduler;

  @Mock RuntimeConfigFactory mockRuntimeConfFactory;

  @Mock Config mockAppConfig;

  @Mock CustomerTask mockCustomerTask;

  @Mock RuntimeConfGetter mockConfGetter;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    when(mockRuntimeConfFactory.globalRuntimeConf()).thenReturn(mockAppConfig);
    taskGarbageCollector =
        new TaskGarbageCollector(mockPlatformScheduler, mockRuntimeConfFactory, mockConfGetter);
    defaultRegistry.clear();
    TaskGarbageCollector.registerMetrics();
  }

  @Test
  public void testStartDisabled() {
    when(mockAppConfig.getDuration(YB_TASK_GC_GC_CHECK_INTERVAL)).thenReturn(Duration.ZERO);
    taskGarbageCollector.start();
    verifyNoInteractions(mockPlatformScheduler);
  }

  @Test
  public void testStartEnabled() {
    when(mockAppConfig.getDuration(YB_TASK_GC_GC_CHECK_INTERVAL)).thenReturn(Duration.ofDays(1));
    taskGarbageCollector.start();
    verify(mockPlatformScheduler, times(1))
        .schedule(any(), eq(Duration.ofMinutes(5)), eq(Duration.ofDays(1)), any());
  }

  @Test
  public void testPurgeNoneStale() {
    taskGarbageCollector.purgeStaleTasks(defaultCustomer, Collections.emptyList());
    checkCounters(defaultCustomer.getUuid(), 1.0, 0.0, null, null);
  }

  @Test
  public void testPurge() {
    // Pretend we deleted 5 rows in all.
    when(mockCustomerTask.cascadeDeleteCompleted()).thenReturn(5);
    when(mockCustomerTask.isDeletable()).thenReturn(true);
    taskGarbageCollector.purgeStaleTasks(
        defaultCustomer, Collections.singletonList(mockCustomerTask));
    checkCounters(defaultCustomer.getUuid(), 1.0, 0.0, 1.0, 4.0);
  }

  // Test that if we do not delete when there are referential integrity issues; then we report such
  // error in counter.
  @Test
  public void testPurgeNonDeletable() {
    // Pretend we deleted no rows.
    when(mockCustomerTask.isDeletable()).thenReturn(false);
    taskGarbageCollector.purgeStaleTasks(
        defaultCustomer, Collections.singletonList(mockCustomerTask));
    checkCounters(defaultCustomer.getUuid(), 1.0, 0.0, null, null);
  }

  // Test that if we do not delete when there are referential integrity issues; then we report such
  // error in counter.
  @Test
  public void testPurgeInvalidData() {
    // Pretend we deleted no rows.
    when(mockCustomerTask.cascadeDeleteCompleted()).thenReturn(0);
    when(mockCustomerTask.isDeletable()).thenReturn(true);
    taskGarbageCollector.purgeStaleTasks(
        defaultCustomer, Collections.singletonList(mockCustomerTask));
    checkCounters(defaultCustomer.getUuid(), 1.0, 1.0, null, null);
  }

  @Test
  public void testDeletableDBConstraints() {
    TaskInfo parentTask = new TaskInfo(TaskType.CreateUniverse);
    parentTask.setOwner("test");
    parentTask.setTaskState(TaskInfo.State.Success);
    parentTask.setDetails(mapper.createObjectNode());
    parentTask.save();

    TaskInfo subTask = new TaskInfo(TaskType.CreateUniverse);
    subTask.setOwner("test");
    subTask.setParentUuid(parentTask.getUuid());
    subTask.setPosition(0);
    subTask.setTaskState(TaskInfo.State.Success);
    subTask.setDetails(mapper.createObjectNode());
    subTask.save();

    UUID targetUuid = UUID.randomUUID();
    CustomerTask customerTask =
        spy(
            CustomerTask.create(
                defaultCustomer,
                targetUuid,
                parentTask.getUuid(),
                TargetType.Universe,
                CustomerTask.TaskType.Create,
                "test-universe"));
    customerTask.setCompletionTime(new Date());
    customerTask.save();
    doReturn(true).when(customerTask).isDeletable();
    taskGarbageCollector.purgeStaleTasks(defaultCustomer, Collections.singletonList(customerTask));
    checkCounters(defaultCustomer.getUuid(), 1.0, 0.0, 1.0, 2.0);
    assertFalse(TaskInfo.maybeGet(parentTask.getUuid()).isPresent());
    assertFalse(TaskInfo.maybeGet(subTask.getUuid()).isPresent());
    assertTrue(CustomerTask.get(customerTask.getId()) == null);
  }

  private String getTotalCounterName(String name) {
    return name + "_total";
  }
}
