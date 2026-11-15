// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.supportbundle.SupportBundleSubTaskBuilder.ComponentDownloadRequest;
import com.yugabyte.yw.common.supportbundle.SupportBundleSubTaskBuilder.SubTaskGroupRegistry;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Date;
import java.util.EnumSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.slf4j.LoggerFactory;

public class SupportBundleSubTaskBuilderTest extends FakeDBApplication {

  private Customer customer;
  private Universe universe;
  private Path bundlePath;

  private SupportBundleComponentFactory componentFactory;
  private SupportBundleComponent component;
  private SupportBundleUtil supportBundleUtil;
  private SupportBundleSubTaskBuilder builder;
  private RecordingRegistry registry;
  private ListAppender<ILoggingEvent> listAppender;
  private Logger logger;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
    bundlePath =
        Paths.get(AppConfigHelper.getStoragePath(), "yb-support-bundle-" + UUID.randomUUID());

    componentFactory = mock(SupportBundleComponentFactory.class);
    component = mock(SupportBundleComponent.class);
    supportBundleUtil = mock(SupportBundleUtil.class);
    builder = new SupportBundleSubTaskBuilder(componentFactory, supportBundleUtil);
    registry = new RecordingRegistry(app.injector().instanceOf(TaskExecutor.class));

    logger = (Logger) LoggerFactory.getLogger(SupportBundleSubTaskBuilderTest.class);
    listAppender = new ListAppender<>();
    listAppender.start();
    logger.addAppender(listAppender);
  }

  @After
  public void tearDown() {
    FileUtils.deleteQuietly(bundlePath.toFile());
    logger.detachAppender(listAppender);
  }

  @Test
  public void testBuildBundlePathForUniverse() {
    Path path = builder.buildBundlePath(universe);

    assertEquals(AppConfigHelper.getStoragePath(), path.getParent().toString());
    String name = path.getFileName().toString();
    assertTrue(name, name.startsWith("yb-support-bundle-" + universe.getName() + "-"));
    assertTrue(name, name.endsWith("-logs"));
  }

  @Test
  public void testBuildBundlePathForYbaOnlyBundle() {
    Path path = builder.buildBundlePath(null /* universe */);

    String name = path.getFileName().toString();
    assertTrue(name, name.startsWith("yb-support-bundle-yba-"));
    assertFalse(name, name.contains(universe.getName()));
  }

  /** The manifest describes the requested bundle, so runtime state that is not yet final is cut. */
  @Test
  public void testSaveManifestDropsRuntimeOnlyFields() {
    SupportBundle supportBundle = createV1Bundle(EnumSet.of(ComponentType.YBAComponent));

    builder.saveManifest(customer, bundlePath, supportBundle);

    ArgumentCaptor<JsonNode> manifestCaptor = ArgumentCaptor.forClass(JsonNode.class);
    verify(supportBundleUtil)
        .saveMetadata(
            eq(customer),
            eq(bundlePath.toAbsolutePath().toString()),
            manifestCaptor.capture(),
            eq("manifest.json"));
    JsonNode manifest = manifestCaptor.getValue();
    assertEquals(supportBundle.getBundleUUID().toString(), manifest.get("bundleUUID").asText());
    assertFalse(manifest.toString(), manifest.has("status"));
    assertFalse(manifest.toString(), manifest.has("sizeInBytes"));
  }

  /** A manifest we cannot write is not worth failing the bundle over. */
  @Test
  public void testSaveManifestSwallowsWriteFailures() {
    doThrow(new RuntimeException("no space left on device"))
        .when(supportBundleUtil)
        .saveMetadata(any(), any(), any(), any());

    builder.saveManifest(customer, bundlePath, createV1Bundle(EnumSet.of(ComponentType.GFlags)));

    verify(supportBundleUtil).saveMetadata(any(), any(), any(), any());
  }

  @Test
  public void testAddNodeReachabilityChecksAddsOneCheckPerNode() {
    Set<NodeDetails> nodes = nodes("node-1", "node-2", "node-3");

    builder.addNodeReachabilityChecks(
        registry, nodes, universe, 30L /* nodeReachableTimeout */, ConcurrentHashMap.newKeySet());

    assertEquals(List.of("CheckNodeReachable"), registry.groupNames());
    assertEquals(nodes.size(), registry.group("CheckNodeReachable").getSubTaskCount());
  }

  /**
   * Node level components fan out one subtask per reachable node and global ones collect once, both
   * into a per-component group.
   */
  @Test
  public void testAddComponentDownloadTasksFansOutNodeLevelComponents() {
    Set<NodeDetails> reachableNodes = nodes("node-1", "node-2");
    when(componentFactory.getComponent(any())).thenReturn(component);

    builder.addComponentDownloadTasks(
        registry,
        request(EnumSet.of(ComponentType.UniverseLogs, ComponentType.YbaMetadata), reachableNodes)
            .build());

    assertEquals(
        List.of("UniverseLogsComponentDownload", "YbaMetadataComponentDownload"),
        registry.groupNames());
    assertEquals(2, registry.group("UniverseLogsComponentDownload").getSubTaskCount());
    assertEquals(1, registry.group("YbaMetadataComponentDownload").getSubTaskCount());
    assertEquals(
        SubTaskGroupType.SupportBundleComponentDownload,
        registry.group("UniverseLogsComponentDownload").getSubTaskGroupType());

    // Each subtask writes into its own directory, created up front.
    assertTrue(Files.isDirectory(bundlePath.resolve("node-1")));
    assertTrue(Files.isDirectory(bundlePath.resolve("node-2")));
    assertTrue(Files.isDirectory(bundlePath.resolve("YBA")));
  }

  /** Application logs are collected last so they cover the rest of the collection. */
  @Test
  public void testAddComponentDownloadTasksCollectsApplicationLogsLast() {
    when(componentFactory.getComponent(any())).thenReturn(component);

    builder.addComponentDownloadTasks(
        registry,
        request(
                EnumSet.of(ComponentType.ApplicationLogs, ComponentType.UniverseLogs),
                nodes("node-1"))
            .build());

    assertEquals(
        List.of("UniverseLogsComponentDownload", "ApplicationLogsComponentDownload"),
        registry.groupNames());
  }

  /** A YBA only bundle has no universe to collect from, so everything else is dropped. */
  @Test
  public void testAddComponentDownloadTasksForYbaOnlyBundleCollectsOnlyYbaComponent() {
    when(componentFactory.getComponent(any())).thenReturn(component);

    builder.addComponentDownloadTasks(
        registry,
        request(
                EnumSet.of(
                    ComponentType.YBAComponent,
                    ComponentType.UniverseLogs,
                    ComponentType.ApplicationLogs),
                nodes("node-1"))
            .universe(null)
            .ybaOnly(true)
            .build());

    assertEquals(List.of("YBAComponentComponentDownload"), registry.groupNames());
    verify(componentFactory).getComponent(ComponentType.YBAComponent);
    verify(componentFactory, never()).getComponent(ComponentType.UniverseLogs);
    verify(componentFactory, never()).getComponent(ComponentType.ApplicationLogs);
  }

  /** With every node down the node level group stays empty instead of failing collection. */
  @Test
  public void testAddComponentDownloadTasksWithNoReachableNodes() {
    when(componentFactory.getComponent(any())).thenReturn(component);

    builder.addComponentDownloadTasks(
        registry, request(EnumSet.of(ComponentType.UniverseLogs), Set.of()).build());

    assertEquals(0, registry.group("UniverseLogsComponentDownload").getSubTaskCount());
    assertFalse(Files.exists(bundlePath.resolve("node-1")));
  }

  private ComponentDownloadRequest.ComponentDownloadRequestBuilder request(
      EnumSet<ComponentType> components, Set<NodeDetails> reachableNodes) {
    SupportBundle supportBundle = createV1Bundle(components);
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.components = components;
    return ComponentDownloadRequest.builder()
        .bundleDetails(bundleDetails)
        .v1TaskParams(
            new SupportBundleTaskParams(
                supportBundle, new SupportBundleFormData(), customer, universe))
        .customer(customer)
        .universe(universe)
        .reachableNodes(reachableNodes)
        .startDate(new Date())
        .endDate(new Date())
        .bundlePath(bundlePath);
  }

  @Test
  public void testFormatTimingLogLineContainsRequiredFields() {
    UUID bundleUUID = UUID.fromString("11111111-1111-1111-1111-111111111111");
    UUID taskUUID = UUID.fromString("22222222-2222-2222-2222-222222222222");
    String line =
        SupportBundleTaskTiming.formatTimingLogLine(
            "collection", "v1", bundleUUID, taskUUID, "test-universe", "Success", 84231);

    assertTrue(line.startsWith(SupportBundleTaskTiming.LOG_PREFIX));
    assertTrue(line.contains("phase=collection"));
    assertTrue(line.contains("version=v1"));
    assertTrue(line.contains("bundleUUID=" + bundleUUID));
    assertTrue(line.contains("taskUUID=" + taskUUID));
    assertTrue(line.contains("universe=test-universe"));
    assertTrue(line.contains("status=Success"));
    assertTrue(line.contains("durationMs=84231"));
    assertTrue(line.contains("durationSec=84.231"));
  }

  @Test
  public void testLogsCollectionAndTotalPhases() throws InterruptedException {
    UUID bundleUUID = UUID.randomUUID();
    UUID taskUUID = UUID.randomUUID();
    SupportBundleTaskTiming timing = SupportBundleTaskTiming.start();
    timing.startCollection();
    Thread.sleep(5);
    timing.logCollectionCompleted(logger, "v2", bundleUUID, taskUUID, "yba-only", "Success");
    timing.logTotalCompleted(logger, "v2", bundleUUID, taskUUID, "yba-only", "Success");

    assertEquals(2, listAppender.list.size());
    assertTrue(listAppender.list.get(0).getFormattedMessage().contains("phase=collection"));
    assertTrue(listAppender.list.get(0).getFormattedMessage().contains("version=v2"));
    assertTrue(listAppender.list.get(1).getFormattedMessage().contains("phase=total"));
  }

  @Test
  public void testCollectionLogIsEmittedOnce() {
    UUID bundleUUID = UUID.randomUUID();
    UUID taskUUID = UUID.randomUUID();
    SupportBundleTaskTiming timing = SupportBundleTaskTiming.start();
    timing.startCollection();
    timing.logCollectionCompleted(logger, "v1", bundleUUID, taskUUID, "test-universe", "Failed");
    timing.logCollectionCompleted(logger, "v1", bundleUUID, taskUUID, "test-universe", "Failed");

    assertEquals(1, listAppender.list.size());
  }

  private SupportBundle createV1Bundle(EnumSet<ComponentType> components) {
    SupportBundleFormData bundleData = new SupportBundleFormData();
    bundleData.components = components;
    return SupportBundle.create(
        bundleData, universe, app.injector().instanceOf(RuntimeConfGetter.class));
  }

  private Set<NodeDetails> nodes(String... nodeNames) {
    Set<NodeDetails> nodes = new LinkedHashSet<>();
    for (int idx = 0; idx < nodeNames.length; idx++) {
      NodeDetails node = ApiUtils.getDummyNodeDetails(idx + 1);
      node.nodeName = nodeNames[idx];
      nodes.add(node);
    }
    return nodes;
  }

  /** Records the groups the builder hands back, standing in for the task's runnable. */
  private static class RecordingRegistry implements SubTaskGroupRegistry {
    private final TaskExecutor taskExecutor;
    private final List<SubTaskGroup> groups = new ArrayList<>();

    RecordingRegistry(TaskExecutor taskExecutor) {
      this.taskExecutor = taskExecutor;
    }

    @Override
    public SubTaskGroup newGroup(String name) {
      return taskExecutor.createSubTaskGroup(name);
    }

    @Override
    public SubTaskGroup newGroup(String name, SubTaskGroupType groupType) {
      return taskExecutor.createSubTaskGroup(name, groupType, false /* ignoreErrors */);
    }

    @Override
    public void addGroup(SubTaskGroup subTaskGroup) {
      groups.add(subTaskGroup);
    }

    List<String> groupNames() {
      return groups.stream().map(SubTaskGroup::getName).collect(Collectors.toList());
    }

    SubTaskGroup group(String name) {
      return groups.stream()
          .filter(g -> g.getName().equals(name))
          .findFirst()
          .orElseThrow(() -> new AssertionError("No subtask group named " + name));
    }
  }
}
