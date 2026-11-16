// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.atLeast;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.supportbundle.SupportBundleTaskTiming;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.EnumSet;
import java.util.HashSet;
import java.util.List;
import java.util.UUID;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import org.slf4j.LoggerFactory;

@RunWith(MockitoJUnitRunner.class)
public class CreateSupportBundleTest extends CommissionerBaseTest {
  private Universe universe;
  private Customer customer;
  protected RuntimeConfigFactory runtimeConfigFactory;
  protected RuntimeConfGetter runtimeConfGetter;

  @Before
  public void setUp() {
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
    this.customer = ModelFactory.testCustomer();
    this.universe = ModelFactory.createUniverse(customer.getId());
    this.runtimeConfigFactory = mockBaseTaskDependencies.getRuntimeConfigFactory();
    this.runtimeConfGetter = mockBaseTaskDependencies.getConfGetter();
  }

  @After
  public void tearDown() throws IOException {
    // Delete all the fake support bundles created
    String tmpStoragePath =
        runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path");
    File tmpDirectory = new File(tmpStoragePath);
    for (File f : tmpDirectory.listFiles()) {
      if (f.getName().startsWith("yb-support-bundle-")) {
        f.delete();
      }
    }
  }

  private TaskInfo submitTask(Date startDate, Date endDate) {
    return submitTask(startDate, endDate, EnumSet.allOf(ComponentType.class));
  }

  private TaskInfo submitTask(Date startDate, Date endDate, EnumSet<ComponentType> components) {
    // Filling all the bundle details with fake data
    SupportBundleFormData bundleData = new SupportBundleFormData();
    bundleData.startDate = startDate;
    bundleData.endDate = endDate;
    bundleData.components = components;
    SupportBundle supportBundle = SupportBundle.create(bundleData, universe, runtimeConfGetter);
    SupportBundleTaskParams bundleTaskParams =
        new SupportBundleTaskParams(supportBundle, bundleData, customer, universe);
    try {
      UUID taskUUID = commissioner.submit(TaskType.CreateSupportBundle, bundleTaskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testCreateSupportBundleWithStartAndEndDate() throws Exception {
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    doNothing()
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

    // Check if task succeeded
    TaskInfo taskInfo = submitTask(new Date(), new Date());
    assertEquals(Success, taskInfo.getTaskState());

    // Check if the components are executed
    verify(mockSupportBundleComponent, atLeast(1))
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

    // Check if bundle exists in db
    List<SupportBundle> supportBundleList = SupportBundle.getAll();
    assertEquals(supportBundleList.size(), 1);

    // Check if bundle path exists
    File bundleFile = new File(supportBundleList.get(0).getPath());
    assertTrue(bundleFile.isFile());
  }

  @Test
  public void testCreateSupportBundleWithOnlyStartDate() throws Exception {
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    doNothing()
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

    // Check if task succeeded
    TaskInfo taskInfo = submitTask(new Date(), null);
    assertEquals(Success, taskInfo.getTaskState());

    // Check if the components are executed
    verify(mockSupportBundleComponent, atLeast(1))
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

    // Check if bundle exists in db
    List<SupportBundle> supportBundleList = SupportBundle.getAll();
    assertEquals(supportBundleList.size(), 1);

    // Check if bundle path exists
    File bundleFile = new File(supportBundleList.get(0).getPath());
    assertTrue(bundleFile.isFile());
  }

  @Test
  public void testCreateSupportBundleWithOnlyEndDate() throws Exception {
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    doNothing()
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

    // Check if task succeeded
    TaskInfo taskInfo = submitTask(null, new Date());
    assertEquals(Success, taskInfo.getTaskState());

    // Check if the components are executed
    verify(mockSupportBundleComponent, atLeast(1))
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

    // Check if bundle exists in db
    List<SupportBundle> supportBundleList = SupportBundle.getAll();
    assertEquals(supportBundleList.size(), 1);

    // Check if bundle path exists
    File bundleFile = new File(supportBundleList.get(0).getPath());
    assertTrue(bundleFile.isFile());
  }

  @Test
  public void testCreateSupportBundlePreservesStartAndEndDateTime() throws Exception {
    ArgumentCaptor<Date> startCaptor = ArgumentCaptor.forClass(Date.class);
    ArgumentCaptor<Date> endCaptor = ArgumentCaptor.forClass(Date.class);

    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    doNothing()
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(
            any(), any(), any(), any(), startCaptor.capture(), endCaptor.capture(), any());

    SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
    Date start = sdf.parse("2026-05-06 09:17:13");
    Date end = sdf.parse("2026-05-06 15:30:45");

    TaskInfo taskInfo = submitTask(start, end);
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockSupportBundleComponent, atLeast(1))
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

    for (Date capturedStart : startCaptor.getAllValues()) {
      assertEquals(start, capturedStart);
    }
    for (Date capturedEnd : endCaptor.getAllValues()) {
      assertEquals(end, capturedEnd);
    }
  }

  @Test
  public void testCreateSupportBundleWithNoDates() throws Exception {
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    doNothing()
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

    // Check if task succeeded
    TaskInfo taskInfo = submitTask(null, null);
    assertEquals(Success, taskInfo.getTaskState());

    // Check if the components are executed
    verify(mockSupportBundleComponent, atLeast(1))
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

    // Check if bundle exists in db
    List<SupportBundle> supportBundleList = SupportBundle.getAll();
    assertEquals(supportBundleList.size(), 1);

    // Check if bundle path exists
    File bundleFile = new File(supportBundleList.get(0).getPath());
    assertTrue(bundleFile.isFile());
  }

  /**
   * The v1 task probes every node of the universe, and collection of node level components is
   * limited to the ones that answered.
   */
  @Test
  public void testCreateSupportBundleSkipsUnreachableNodes() throws Exception {
    List<NodeDetails> nodes = addUniverseNodes("node-1", "node-2");
    NodeDetails reachableNode = nodes.get(0);
    NodeDetails unreachableNode = nodes.get(1);

    ArgumentCaptor<NodeDetails> nodeCaptor = ArgumentCaptor.forClass(NodeDetails.class);
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    doNothing()
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(
            any(), any(), any(), any(), any(), any(), nodeCaptor.capture());
    when(mockNodeUniverseManager.isNodeReachable(eq(reachableNode), any(), anyLong()))
        .thenReturn(true);
    when(mockNodeUniverseManager.isNodeReachable(eq(unreachableNode), any(), anyLong()))
        .thenReturn(false);

    TaskInfo taskInfo = submitTask(new Date(), new Date(), EnumSet.of(ComponentType.UniverseLogs));
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockNodeUniverseManager, times(2)).isNodeReachable(any(), any(), anyLong());
    assertEquals(List.of(reachableNode), nodeCaptor.getAllValues());
  }

  @Test
  public void testCreateSupportBundleSuccessUpdatesOperatorStatus() throws Exception {
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    doNothing()
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

    TaskInfo taskInfo = submitTask(new Date(), new Date());
    assertEquals(Success, taskInfo.getTaskState());

    SupportBundle bundle = SupportBundle.getAll().get(0);
    assertEquals(SupportBundleStatusType.Success, bundle.getStatus());
    verify(mockOperatorStatusUpdater)
        .markSupportBundleFinished(any(), any(), eq(bundle.getPathObject()));
    verify(mockOperatorStatusUpdater, never()).markSupportBundleFailed(any(), any());
  }

  /** A bundle that could not be collected must not be left behind in the Running state. */
  @Test
  public void testCreateSupportBundleFailureMarksBundleFailed() throws Exception {
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenThrow(new RuntimeException("component lookup failed"));

    TaskInfo taskInfo = submitTask(new Date(), new Date());
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());

    SupportBundle bundle = SupportBundle.getAll().get(0);
    assertEquals(SupportBundleStatusType.Failed, bundle.getStatus());
    verify(mockOperatorStatusUpdater).markSupportBundleFailed(any(), any());
    verify(mockOperatorStatusUpdater, never()).markSupportBundleFinished(any(), any(), any());
  }

  @Test
  public void testCreateSupportBundleLogsTiming() throws Exception {
    Logger taskLogger = (Logger) LoggerFactory.getLogger(CreateSupportBundle.class);
    ListAppender<ILoggingEvent> listAppender = new ListAppender<>();
    listAppender.start();
    taskLogger.addAppender(listAppender);
    try {
      when(mockSupportBundleComponentFactory.getComponent(any()))
          .thenReturn(mockSupportBundleComponent);
      doNothing()
          .when(mockSupportBundleComponent)
          .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

      SupportBundleFormData bundleData = new SupportBundleFormData();
      bundleData.components = EnumSet.of(ComponentType.YBAComponent);
      SupportBundle supportBundle = SupportBundle.create(bundleData, universe, runtimeConfGetter);
      SupportBundleTaskParams bundleTaskParams =
          new SupportBundleTaskParams(supportBundle, bundleData, customer, universe);

      UUID taskUUID = commissioner.submit(TaskType.CreateSupportBundle, bundleTaskParams);
      TaskInfo taskInfo = waitForTask(taskUUID);
      assertEquals(Success, taskInfo.getTaskState());

      boolean hasCollectionTiming =
          listAppender.list.stream()
              .map(ILoggingEvent::getFormattedMessage)
              .anyMatch(
                  msg ->
                      msg.contains(SupportBundleTaskTiming.LOG_PREFIX)
                          && msg.contains("phase=collection")
                          && msg.contains("version=v1"));
      boolean hasTotalTiming =
          listAppender.list.stream()
              .map(ILoggingEvent::getFormattedMessage)
              .anyMatch(
                  msg ->
                      msg.contains(SupportBundleTaskTiming.LOG_PREFIX)
                          && msg.contains("phase=total")
                          && msg.contains("version=v1"));
      assertTrue(hasCollectionTiming);
      assertTrue(hasTotalTiming);
    } finally {
      taskLogger.detachAppender(listAppender);
    }
  }

  /** Adds named nodes to the universe, returning them in the order given. */
  private List<NodeDetails> addUniverseNodes(String... nodeNames) {
    List<NodeDetails> nodes = new ArrayList<>();
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.nodeDetailsSet = new HashSet<>();
          for (int idx = 0; idx < nodeNames.length; idx++) {
            NodeDetails node = ApiUtils.getDummyNodeDetails(idx + 1);
            node.nodeName = nodeNames[idx];
            node.placementUuid = details.getPrimaryCluster().uuid;
            details.nodeDetailsSet.add(node);
            nodes.add(node);
          }
          u.setUniverseDetails(details);
        });
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    return nodes;
  }
}
