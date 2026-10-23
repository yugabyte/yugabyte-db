package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.commissioner.TaskGarbageCollector.CUSTOMER_TASK_METRIC_NAME;
import static com.yugabyte.yw.commissioner.TaskGarbageCollector.CUSTOMER_UUID_LABEL;
import static com.yugabyte.yw.commissioner.TaskGarbageCollector.NUM_TASK_GC_ERRORS;
import static com.yugabyte.yw.commissioner.TaskGarbageCollector.NUM_TASK_GC_RUNS;
import static com.yugabyte.yw.commissioner.TaskGarbageCollector.TASK_INFO_METRIC_NAME;
import static com.yugabyte.yw.commissioner.TaskGarbageCollector.YB_TASK_GC_GC_CHECK_INTERVAL;
import static com.yugabyte.yw.common.TestUtils.validateMetric;
import static io.prometheus.metrics.model.registry.PrometheusRegistry.defaultRegistry;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
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
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.time.Instant;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;

@RunWith(JUnitParamsRunner.class)
public class TaskGarbageCollectorTest extends FakeDBApplication {

  private void checkCounters(
      UUID customerUuid,
      Double expectedNumRuns,
      Double expectedErrors,
      Double expectedCustomerTaskGC,
      Double expectedTaskInfoGC) {
    validateMetric(NUM_TASK_GC_RUNS, expectedNumRuns);
    validateMetric(NUM_TASK_GC_ERRORS, expectedErrors);
    validateMetric(
        CUSTOMER_TASK_METRIC_NAME,
        expectedCustomerTaskGC,
        CUSTOMER_UUID_LABEL,
        customerUuid.toString());
    validateMetric(
        TASK_INFO_METRIC_NAME, expectedTaskInfoGC, CUSTOMER_UUID_LABEL, customerUuid.toString());
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
    MockitoAnnotations.initMocks(this);
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
    TaskInfo parentTask = new TaskInfo(TaskType.CreateUniverse, null);
    parentTask.setOwner("test");
    parentTask.setTaskState(TaskInfo.State.Success);
    parentTask.setTaskParams(mapper.createObjectNode());
    parentTask.save();

    TaskInfo subTask = new TaskInfo(TaskType.CreateUniverse, null);
    subTask.setOwner("test");
    subTask.setParentUuid(parentTask.getUuid());
    subTask.setPosition(0);
    subTask.setTaskState(TaskInfo.State.Success);
    subTask.setTaskParams(mapper.createObjectNode());
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

  @Test
  @Parameters({"true", "false"})
  public void testKeepOriginalTaskOfOwningChain(boolean ownerIsUpdatingTask) {
    Universe universe = ModelFactory.createUniverse(defaultCustomer.getId());

    TaskInfo originalTask = new TaskInfo(TaskType.ResizeNode, null);
    originalTask.setOwner("test");
    originalTask.setTaskState(TaskInfo.State.Failure);
    originalTask.setTaskParams(mapper.createObjectNode());
    originalTask.save();
    CustomerTask originalCustomerTask =
        CustomerTask.create(
            defaultCustomer,
            universe.getUniverseUUID(),
            originalTask.getUuid(),
            TargetType.Universe,
            CustomerTask.TaskType.ResizeNode,
            universe.getName());
    originalCustomerTask.setCompletionTime(new Date());
    originalCustomerTask.save();

    TaskInfo intermediateRetry = new TaskInfo(TaskType.ResizeNode, null);
    intermediateRetry.setOwner("test");
    intermediateRetry.setTaskState(TaskInfo.State.Failure);
    intermediateRetry.setTaskParams(
        mapper.createObjectNode().put("originalTaskUUID", originalTask.getUuid().toString()));
    intermediateRetry.save();
    CustomerTask intermediateCustomerTask =
        CustomerTask.create(
            defaultCustomer,
            universe.getUniverseUUID(),
            intermediateRetry.getUuid(),
            TargetType.Universe,
            CustomerTask.TaskType.ResizeNode,
            universe.getName());
    intermediateCustomerTask.setCompletionTime(new Date());
    intermediateCustomerTask.save();

    TaskInfo owningTask = new TaskInfo(TaskType.ResizeNode, null);
    owningTask.setOwner("test");
    owningTask.setTaskState(TaskInfo.State.Failure);
    owningTask.setTaskParams(
        mapper.createObjectNode().put("originalTaskUUID", originalTask.getUuid().toString()));
    owningTask.save();
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          if (ownerIsUpdatingTask) {
            details.updatingTaskUUID = owningTask.getUuid();
          } else {
            details.placementModificationTaskUuid = owningTask.getUuid();
          }
          u.setUniverseDetails(details);
        });

    taskGarbageCollector.purgeStaleTasks(
        defaultCustomer, List.of(originalCustomerTask, intermediateCustomerTask));
    assertNotNull(CustomerTask.get(originalCustomerTask.getId()));
    assertTrue(TaskInfo.maybeGet(originalTask.getUuid()).isPresent());
    assertNull(CustomerTask.get(intermediateCustomerTask.getId()));
    assertFalse(TaskInfo.maybeGet(intermediateRetry.getUuid()).isPresent());

    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.updatingTaskUUID = null;
          details.placementModificationTaskUuid = null;
          u.setUniverseDetails(details);
        });
    taskGarbageCollector.purgeStaleTasks(
        defaultCustomer, Collections.singletonList(originalCustomerTask));
    checkCounters(defaultCustomer.getUuid(), 2.0, 0.0, 2.0, 2.0);
    assertNull(CustomerTask.get(originalCustomerTask.getId()));
    assertFalse(TaskInfo.maybeGet(originalTask.getUuid()).isPresent());
  }

  @Test
  @Parameters({"SoftwareUpgrade", "RollbackUpgrade", "FinalizeUpgrade"})
  public void testDeleteUpgradeTask(CustomerTask.TaskType taskType) {
    TaskInfo parentTask = new TaskInfo(TaskType.CreateUniverse, null);
    parentTask.setOwner("test");
    parentTask.setTaskState(TaskInfo.State.Success);
    parentTask.setTaskParams(mapper.createObjectNode());
    parentTask.save();
    Universe universe = ModelFactory.createUniverse(defaultCustomer.getId());
    CustomerTask customerTask =
        spy(
            CustomerTask.create(
                defaultCustomer,
                universe.getUniverseUUID(),
                parentTask.getUuid(),
                TargetType.Universe,
                taskType,
                universe.getName()));
    customerTask.setCompletionTime(new Date());
    customerTask.save();
    taskGarbageCollector.purgeStaleTasks(defaultCustomer, Collections.singletonList(customerTask));
    assertTrue(TaskInfo.maybeGet(parentTask.getUuid()).isPresent());
    assertNotNull(CustomerTask.get(customerTask.getId()));
    universe.delete();
    // Check that the task is deleted if the universe does not exist.
    taskGarbageCollector.purgeStaleTasks(defaultCustomer, Collections.singletonList(customerTask));
    checkCounters(defaultCustomer.getUuid(), 2.0, 0.0, 1.0, 1.0);
    assertFalse(TaskInfo.maybeGet(parentTask.getUuid()).isPresent());
    assertNull(CustomerTask.get(customerTask.getId()));
  }

  private CustomerTask createCompletedUniverseTask(
      Universe universe, CustomerTask.TaskType taskType, Date createTime) {
    TaskInfo taskInfo = new TaskInfo(TaskType.CreateUniverse, null);
    taskInfo.setOwner("test");
    taskInfo.setTaskState(TaskInfo.State.Success);
    taskInfo.setTaskParams(mapper.createObjectNode());
    taskInfo.save();
    CustomerTask customerTask =
        CustomerTask.create(
            defaultCustomer,
            universe.getUniverseUUID(),
            taskInfo.getUuid(),
            TargetType.Universe,
            taskType,
            universe.getName());
    customerTask.setCreateTime(createTime);
    customerTask.setCompletionTime(createTime);
    customerTask.save();
    return customerTask;
  }

  @Test
  @Parameters({"SoftwareUpgrade", "RollbackUpgrade", "FinalizeUpgrade"})
  public void testDeleteOlderUpgradeTaskOfSameType(CustomerTask.TaskType taskType) {
    Universe universe = ModelFactory.createUniverse(defaultCustomer.getId());
    Instant now = Instant.now();
    CustomerTask olderTask =
        createCompletedUniverseTask(universe, taskType, Date.from(now.minus(Duration.ofDays(2))));
    CustomerTask newerTask =
        createCompletedUniverseTask(universe, taskType, Date.from(now.minus(Duration.ofDays(1))));
    taskGarbageCollector.purgeStaleTasks(defaultCustomer, List.of(olderTask, newerTask));
    checkCounters(defaultCustomer.getUuid(), 1.0, 0.0, 1.0, 1.0);
    assertNull(CustomerTask.get(olderTask.getId()));
    assertFalse(TaskInfo.maybeGet(olderTask.getTaskUUID()).isPresent());
    assertNotNull(CustomerTask.get(newerTask.getId()));
    assertTrue(TaskInfo.maybeGet(newerTask.getTaskUUID()).isPresent());
  }

  @Test
  public void testKeepLatestUpgradeTaskOfEachType() {
    // An older SoftwareUpgrade and a newer FinalizeUpgrade are each the latest of their type.
    Universe universe = ModelFactory.createUniverse(defaultCustomer.getId());
    Instant now = Instant.now();
    CustomerTask upgradeTask =
        createCompletedUniverseTask(
            universe,
            CustomerTask.TaskType.SoftwareUpgrade,
            Date.from(now.minus(Duration.ofDays(2))));
    CustomerTask finalizeTask =
        createCompletedUniverseTask(
            universe,
            CustomerTask.TaskType.FinalizeUpgrade,
            Date.from(now.minus(Duration.ofDays(1))));
    taskGarbageCollector.purgeStaleTasks(defaultCustomer, List.of(upgradeTask, finalizeTask));
    checkCounters(defaultCustomer.getUuid(), 1.0, 0.0, null, null);
    assertNotNull(CustomerTask.get(upgradeTask.getId()));
    assertNotNull(CustomerTask.get(finalizeTask.getId()));
  }

  @Test
  public void testKeepOlderUpgradeTaskOwningUniverse() {
    Universe universe = ModelFactory.createUniverse(defaultCustomer.getId());
    Instant now = Instant.now();
    CustomerTask owningTask =
        createCompletedUniverseTask(
            universe,
            CustomerTask.TaskType.SoftwareUpgrade,
            Date.from(now.minus(Duration.ofDays(2))));
    CustomerTask newerTask =
        createCompletedUniverseTask(
            universe,
            CustomerTask.TaskType.SoftwareUpgrade,
            Date.from(now.minus(Duration.ofDays(1))));
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.placementModificationTaskUuid = owningTask.getTaskUUID();
          u.setUniverseDetails(details);
        });
    taskGarbageCollector.purgeStaleTasks(defaultCustomer, List.of(owningTask, newerTask));
    checkCounters(defaultCustomer.getUuid(), 1.0, 0.0, null, null);
    assertNotNull(CustomerTask.get(owningTask.getId()));
    assertNotNull(CustomerTask.get(newerTask.getId()));
  }
}
