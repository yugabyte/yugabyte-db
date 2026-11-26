// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.atLeast;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.mockingDetails;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.yugabyte.yw.commissioner.SupportBundleCleanup;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParamsV2;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.supportbundle.SupportBundleTaskTiming;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleClusterScope;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleNodeStaging;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleStaging;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleServerConfRedactionSupport;
import com.yugabyte.yw.forms.SupportBundleFormDataV2;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundleV2;
import com.yugabyte.yw.models.SupportBundleV2StatusType;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.EnumSet;
import java.util.HashSet;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import org.slf4j.LoggerFactory;

@RunWith(MockitoJUnitRunner.class)
public class CreateSupportBundleV2Test extends CommissionerBaseTest {

  private Universe universe;
  private Customer customer;
  protected RuntimeConfigFactory runtimeConfigFactory;
  protected RuntimeConfGetter runtimeConfGetter;

  @Before
  public void setUp() {
    this.customer = ModelFactory.testCustomer();
    this.universe = ModelFactory.createUniverse(customer.getId());
    this.runtimeConfigFactory = mockBaseTaskDependencies.getRuntimeConfigFactory();
    this.runtimeConfGetter = mockBaseTaskDependencies.getConfGetter();
  }

  @After
  public void tearDown() throws IOException {
    String tmpStoragePath =
        runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path");
    File tmpDirectory = new File(tmpStoragePath);
    for (File f : tmpDirectory.listFiles()) {
      if (f.getName().startsWith("yb-support-bundle-")) {
        f.delete();
      }
    }
  }

  @Test
  public void testCreateSupportBundleV2UsesV2Table() throws Exception {
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    doNothing()
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.startDate = new Date();
    bundleData.endDate = new Date();
    bundleData.components = EnumSet.of(ComponentType.YBAComponent);
    SupportBundleV2 supportBundle =
        SupportBundleV2.createYbaOnly(bundleData, customer, runtimeConfGetter);
    SupportBundleTaskParamsV2 bundleTaskParams =
        SupportBundleTaskParamsV2.forYbaOnly(supportBundle, bundleData, customer);

    UUID taskUUID = commissioner.submit(TaskType.CreateSupportBundleV2, bundleTaskParams);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockSupportBundleComponent, atLeast(1))
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

    SupportBundleV2 bundle = SupportBundleV2.get(supportBundle.getBundleUUID());
    assertTrue(bundle.getPathObject().getFileName().toString().contains("yb-support-bundle-yba-"));
    assertEquals(customer.getUuid(), bundle.getCustomerUUID());
    assertNull(bundle.getScopeUUID());
    assertNull(com.yugabyte.yw.models.SupportBundle.get(supportBundle.getBundleUUID()));
  }

  @Test
  public void testCreateSupportBundleV2LogsTiming() throws Exception {
    Logger taskLogger = (Logger) LoggerFactory.getLogger(CreateSupportBundleV2.class);
    ListAppender<ILoggingEvent> listAppender = new ListAppender<>();
    listAppender.start();
    taskLogger.addAppender(listAppender);
    try {
      when(mockSupportBundleComponentFactory.getComponent(any()))
          .thenReturn(mockSupportBundleComponent);
      doNothing()
          .when(mockSupportBundleComponent)
          .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

      SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
      bundleData.startDate = new Date();
      bundleData.endDate = new Date();
      bundleData.components = EnumSet.of(ComponentType.YBAComponent);
      SupportBundleV2 supportBundle =
          SupportBundleV2.createYbaOnly(bundleData, customer, runtimeConfGetter);
      SupportBundleTaskParamsV2 bundleTaskParams =
          SupportBundleTaskParamsV2.forYbaOnly(supportBundle, bundleData, customer);

      UUID taskUUID = commissioner.submit(TaskType.CreateSupportBundleV2, bundleTaskParams);
      TaskInfo taskInfo = waitForTask(taskUUID);
      assertEquals(Success, taskInfo.getTaskState());

      boolean hasCollectionTiming =
          listAppender.list.stream()
              .map(ILoggingEvent::getFormattedMessage)
              .anyMatch(
                  msg ->
                      msg.contains(SupportBundleTaskTiming.LOG_PREFIX)
                          && msg.contains("phase=collection")
                          && msg.contains("version=v2"));
      boolean hasTotalTiming =
          listAppender.list.stream()
              .map(ILoggingEvent::getFormattedMessage)
              .anyMatch(
                  msg ->
                      msg.contains(SupportBundleTaskTiming.LOG_PREFIX)
                          && msg.contains("phase=total")
                          && msg.contains("version=v2"));
      assertTrue(hasCollectionTiming);
      assertTrue(hasTotalTiming);
    } finally {
      taskLogger.detachAppender(listAppender);
    }
  }

  /**
   * Node selection narrows the candidate set before the reachability probe, so a node left out of
   * the selection should never be contacted at all.
   */
  @Test
  public void testCreateSupportBundleV2ProbesOnlySelectedNodes() throws Exception {
    NodeDetails selectedNode = addUniverseNodes("node-1", "node-2").get(0);

    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    doNothing()
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());
    when(mockNodeUniverseManager.isNodeReachable(any(), any(), anyLong())).thenReturn(true);

    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.startDate = new Date();
    bundleData.endDate = new Date();
    bundleData.components = EnumSet.of(ComponentType.YBAComponent);
    bundleData.nodeNames = List.of("node-1");
    SupportBundleV2 supportBundle = SupportBundleV2.create(bundleData, universe, runtimeConfGetter);
    SupportBundleTaskParamsV2 bundleTaskParams =
        new SupportBundleTaskParamsV2(supportBundle, bundleData, customer, universe);

    TaskInfo taskInfo =
        waitForTask(commissioner.submit(TaskType.CreateSupportBundleV2, bundleTaskParams));
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockNodeUniverseManager).isNodeReachable(eq(selectedNode), any(), anyLong());
    verify(mockNodeUniverseManager, times(1)).isNodeReachable(any(), any(), anyLong());
  }

  /** A YBA only bundle has no universe, so universe components and node probing are skipped. */
  @Test
  public void testCreateSupportBundleV2YbaOnlySkipsUniverseComponents() throws Exception {
    ArgumentCaptor<NodeDetails> nodeCaptor = ArgumentCaptor.forClass(NodeDetails.class);
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    doNothing()
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(
            any(), any(), any(), any(), any(), any(), nodeCaptor.capture());

    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.YBAComponent, ComponentType.UniverseLogs);
    SupportBundleV2 supportBundle =
        SupportBundleV2.createYbaOnly(bundleData, customer, runtimeConfGetter);
    SupportBundleTaskParamsV2 bundleTaskParams =
        SupportBundleTaskParamsV2.forYbaOnly(supportBundle, bundleData, customer);

    TaskInfo taskInfo =
        waitForTask(commissioner.submit(TaskType.CreateSupportBundleV2, bundleTaskParams));
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockSupportBundleComponentFactory).getComponent(ComponentType.YBAComponent);
    verify(mockSupportBundleComponentFactory, never()).getComponent(ComponentType.UniverseLogs);
    verify(mockNodeUniverseManager, never()).isNodeReachable(any(), any(), anyLong());
    assertEquals(1, nodeCaptor.getAllValues().size());
    assertNull(nodeCaptor.getValue());
  }

  /** Node level components are collected only from the selected nodes that answered the probe. */
  @Test
  public void testCreateSupportBundleV2SkipsUnreachableNodes() throws Exception {
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

    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.UniverseLogs);
    SupportBundleV2 supportBundle = SupportBundleV2.create(bundleData, universe, runtimeConfGetter);
    SupportBundleTaskParamsV2 bundleTaskParams =
        new SupportBundleTaskParamsV2(supportBundle, bundleData, customer, universe);

    TaskInfo taskInfo =
        waitForTask(commissioner.submit(TaskType.CreateSupportBundleV2, bundleTaskParams));
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockNodeUniverseManager, times(2)).isNodeReachable(any(), any(), anyLong());
    assertEquals(List.of(reachableNode), nodeCaptor.getAllValues());
    // Limits need no per-node setup or sweep, so a bundle whose components staged nothing runs no
    // script on any node.
    verify(mockNodeUniverseManager, never())
        .runScript(any(), any(), any(), anyList(), any(ShellProcessContext.class));
    // An unreachable node costs only its probe: nothing else runs on it before collection.
    verify(mockNodeUniverseManager, never())
        .runCommand(eq(unreachableNode), any(), anyList(), any(ShellProcessContext.class));
    verify(mockNodeUniverseManager, never())
        .runCommand(
            eq(unreachableNode), any(), anyList(), any(ShellProcessContext.class), anyBoolean());
  }

  /** Collection sees the bundle's chosen node; the task forgets it once the bundle is done. */
  @Test
  public void testClusterScopeIsRegisteredDuringCollectionOnly() throws Exception {
    addUniverseNodes("node-2", "node-1");
    SupportBundleClusterScope clusterScope =
        app.injector().instanceOf(SupportBundleClusterScope.class);
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.UniverseLogs);
    SupportBundleV2 supportBundle = SupportBundleV2.create(bundleData, universe, runtimeConfGetter);
    UUID bundleUuid = supportBundle.getBundleUUID();
    List<String> seenDuringCollection = Collections.synchronizedList(new ArrayList<>());
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    doAnswer(
            invocation -> {
              seenDuringCollection.add(
                  clusterScope.chosenNode(bundleUuid, ComponentType.YbAdminComponent));
              return null;
            })
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());
    when(mockNodeUniverseManager.isNodeReachable(any(), any(), anyLong())).thenReturn(true);

    TaskInfo taskInfo =
        waitForTask(
            commissioner.submit(
                TaskType.CreateSupportBundleV2,
                new SupportBundleTaskParamsV2(supportBundle, bundleData, customer, universe)));
    assertEquals(Success, taskInfo.getTaskState());

    assertEquals(List.of("node-1", "node-1"), seenDuringCollection);
    assertNull(clusterScope.chosenNode(bundleUuid, ComponentType.YbAdminComponent));
  }

  /** The task drops the bundle's cached sensitive-gflag list once the bundle is done. */
  @Test
  public void testSensitiveGflagCacheIsDroppedAfterTheBundle() throws Exception {
    addUniverseNodes("node-1");
    SupportBundleServerConfRedactionSupport redactionSupport =
        app.injector().instanceOf(SupportBundleServerConfRedactionSupport.class);
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.UniverseLogs);
    SupportBundleV2 supportBundle = SupportBundleV2.create(bundleData, universe, runtimeConfGetter);
    UUID bundleUuid = supportBundle.getBundleUUID();
    AtomicReference<Universe> seenUniverse = new AtomicReference<>();
    AtomicReference<NodeDetails> seenNode = new AtomicReference<>();
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    when(mockNodeUniverseManager.getYbHomeDir(any(), any())).thenReturn("/home/yugabyte");
    doAnswer(
            invocation -> {
              seenUniverse.set(invocation.getArgument(2));
              seenNode.set(invocation.getArgument(6));
              redactionSupport.appendRedactionScriptParams(
                  bundleUuid, List.of(), seenUniverse.get(), seenNode.get());
              return null;
            })
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());
    when(mockNodeUniverseManager.isNodeReachable(any(), any(), anyLong())).thenReturn(true);

    TaskInfo taskInfo =
        waitForTask(
            commissioner.submit(
                TaskType.CreateSupportBundleV2,
                new SupportBundleTaskParamsV2(supportBundle, bundleData, customer, universe)));
    assertEquals(Success, taskInfo.getTaskState());

    // Cached for the bundle, the next request would not read the gflag metadata again.
    long before = sensitiveGflagReads();
    redactionSupport.appendRedactionScriptParams(
        bundleUuid, List.of(), seenUniverse.get(), seenNode.get());
    assertEquals(before + 1, sensitiveGflagReads());
  }

  private long sensitiveGflagReads() {
    return mockingDetails(mockGFlagsValidation).getInvocations().stream()
        .filter(call -> call.getMethod().getName().equals("getSensitiveJsonPathsForVersion"))
        .count();
  }

  /**
   * The v2 API persists the resolved component list on the bundle row, which can happen after the
   * task params were built, so collection follows the persisted bundle rather than the params.
   */
  @Test
  public void testCreateSupportBundleV2CollectsPersistedBundleDetails() throws Exception {
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    doNothing()
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.YBAComponent, ComponentType.YbaMetadata);
    SupportBundleV2 supportBundle = SupportBundleV2.create(bundleData, universe, runtimeConfGetter);
    SupportBundleTaskParamsV2 bundleTaskParams =
        new SupportBundleTaskParamsV2(supportBundle, bundleData, customer, universe);

    // Stale params: only the persisted row knows about YbaMetadata.
    supportBundle.getBundleDetails().components = EnumSet.of(ComponentType.YBAComponent);
    bundleTaskParams.bundleData.components = null;

    TaskInfo taskInfo =
        waitForTask(commissioner.submit(TaskType.CreateSupportBundleV2, bundleTaskParams));
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockSupportBundleComponentFactory).getComponent(ComponentType.YBAComponent);
    verify(mockSupportBundleComponentFactory).getComponent(ComponentType.YbaMetadata);
  }

  @Test
  public void testCreateSupportBundleV2FailureMarksBundleFailed() throws Exception {
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenThrow(new RuntimeException("component lookup failed"));

    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.YBAComponent);
    SupportBundleV2 supportBundle =
        SupportBundleV2.createYbaOnly(bundleData, customer, runtimeConfGetter);
    SupportBundleTaskParamsV2 bundleTaskParams =
        SupportBundleTaskParamsV2.forYbaOnly(supportBundle, bundleData, customer);

    TaskInfo taskInfo =
        waitForTask(commissioner.submit(TaskType.CreateSupportBundleV2, bundleTaskParams));
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());

    assertEquals(
        SupportBundleV2StatusType.Failed,
        SupportBundleV2.get(supportBundle.getBundleUUID()).getStatus());
  }

  /** On the row before collection starts, so a bundle cut short by a YBA restart is cleanable. */
  @Test
  public void testBundlePathIsSavedBeforeCollection() throws Exception {
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.startDate = new Date();
    bundleData.endDate = new Date();
    bundleData.components = EnumSet.of(ComponentType.YBAComponent);
    SupportBundleV2 supportBundle =
        SupportBundleV2.createYbaOnly(bundleData, customer, runtimeConfGetter);
    List<Path> savedPaths = new ArrayList<>();
    List<Path> componentDirs = new ArrayList<>();
    doAnswer(
            invocation -> {
              savedPaths.add(SupportBundleV2.get(supportBundle.getBundleUUID()).getPathObject());
              componentDirs.add(invocation.getArgument(3));
              return null;
            })
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

    TaskInfo taskInfo =
        waitForTask(
            commissioner.submit(
                TaskType.CreateSupportBundleV2,
                SupportBundleTaskParamsV2.forYbaOnly(supportBundle, bundleData, customer)));

    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(1, savedPaths.size());
    // Components write into <bundle dir>/YBA.
    assertEquals(componentDirs.get(0).getParent(), savedPaths.get(0));
  }

  /** A failed bundle keeps the path to its partial directory, so the cleanup deletes both. */
  @Test
  public void testFailedBundleDirectoryIsDeletedByCleanup() throws Exception {
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenThrow(new RuntimeException("component lookup failed"));
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.YBAComponent);
    SupportBundleV2 supportBundle =
        SupportBundleV2.createYbaOnly(bundleData, customer, runtimeConfGetter);

    TaskInfo taskInfo =
        waitForTask(
            commissioner.submit(
                TaskType.CreateSupportBundleV2,
                SupportBundleTaskParamsV2.forYbaOnly(supportBundle, bundleData, customer)));

    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    SupportBundleV2 failed = SupportBundleV2.get(supportBundle.getBundleUUID());
    Path partialDir = failed.getPathObject();
    assertNotNull(partialDir);
    assertTrue(Files.isDirectory(partialDir));

    app.injector().instanceOf(SupportBundleCleanup.class).deleteSupportBundleIfOld(failed);

    assertFalse(Files.exists(partialDir));
    assertNull(SupportBundleV2.get(supportBundle.getBundleUUID()));
  }

  /** Removed when the task ends, even on failure, so an archive caught mid-copy is not left. */
  @Test
  public void testStagingIsDeletedWhenTheTaskFails() throws Exception {
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenThrow(new RuntimeException("component lookup failed"));
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.YBAComponent);
    SupportBundleV2 supportBundle =
        SupportBundleV2.createYbaOnly(bundleData, customer, runtimeConfGetter);
    SupportBundleTaskParamsV2 bundleTaskParams =
        SupportBundleTaskParamsV2.forYbaOnly(supportBundle, bundleData, customer);
    Path stagingRoot = SupportBundleStaging.rootFor(supportBundle.getBundleUUID());
    Path leftover = stagingRoot.resolve("n1-UniverseLogs-1/UniverseLogs.tar");
    Files.createDirectories(leftover.getParent());
    Files.writeString(leftover, "partial");

    TaskInfo taskInfo =
        waitForTask(commissioner.submit(TaskType.CreateSupportBundleV2, bundleTaskParams));

    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    assertFalse(Files.exists(stagingRoot));
  }

  /** The status is saved before the node cleanup, which visits every staged node in turn. */
  @Test
  public void testBundleStatusIsSavedBeforeTheNodeCleanup() throws Exception {
    NodeDetails node = addUniverseNodes("node-1").get(0);
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.UniverseLogs);
    SupportBundleV2 supportBundle = SupportBundleV2.create(bundleData, universe, runtimeConfGetter);
    UUID bundleUuid = supportBundle.getBundleUUID();
    List<SupportBundleV2StatusType> statusAtCleanup =
        Collections.synchronizedList(new ArrayList<>());
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any(), eq(false)))
        .thenAnswer(
            invocation -> {
              if ("rm".equals(((List<?>) invocation.getArgument(2)).get(0))) {
                statusAtCleanup.add(SupportBundleV2.get(bundleUuid).getStatus());
              }
              return ShellResponse.create(0, "");
            });
    // A node the bundle staged before it failed.
    app.injector()
        .instanceOf(SupportBundleNodeStaging.class)
        .dir(bundleUuid, universe, node, ShellProcessContext.builder().build());
    when(mockNodeUniverseManager.isNodeReachable(any(), any(), anyLong())).thenReturn(true);
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenThrow(new RuntimeException("component lookup failed"));

    TaskInfo taskInfo =
        waitForTask(
            commissioner.submit(
                TaskType.CreateSupportBundleV2,
                new SupportBundleTaskParamsV2(supportBundle, bundleData, customer, universe)));

    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    assertEquals(List.of(SupportBundleV2StatusType.Failed), statusAtCleanup);
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
