// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.Iterables;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.NodeAgentEnabler.NodeAgentInstaller;
import com.yugabyte.yw.commissioner.NodeAgentEnabler.UniverseNodeAgentInstaller;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.ArrayList;
import java.util.LinkedList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class NodeAgentEnablerTest extends FakeDBApplication {
  private Customer customer1;
  private Customer customer2;
  private Provider provider1;
  private Provider provider2;
  private UUID universeUuid1;
  private UUID universeUuid01;
  private UUID universeUuid2;
  private RuntimeConfGetter confGetter;
  private SettableRuntimeConfigFactory settableRuntimeConfigFactory;
  private PlatformExecutorFactory platformExecutorFactory;
  private PlatformScheduler platformScheduler;
  private ExecutorService executorService;
  private NodeAgentInstaller mockNodeAgentInstaller;
  private NodeAgentEnabler nodeAgentEnabler;
  private TestUniverseTaskBase universeTaskBase;

  @Before
  public void setUp() {
    customer1 = ModelFactory.testCustomer("customer1");
    customer2 = ModelFactory.testCustomer("customer2");
    provider1 = ModelFactory.awsProvider(customer1);
    provider2 = ModelFactory.awsProvider(customer2);
    AvailabilityZone.createOrThrow(
        Region.create(provider1, "region-1", "Region 1", "yb-image-1"), "az-1", "AZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(
        Region.create(provider2, "region-1", "Region 1", "yb-image-1"), "az-1", "AZ 1", "subnet-1");
    universeUuid1 =
        createAwsUniverse(customer1, provider1, "test-universe1", "10.10.10").getUniverseUUID();
    universeUuid01 =
        createAwsUniverse(customer1, provider1, "test-universe01", "10.10.20").getUniverseUUID();
    universeUuid2 =
        createAwsUniverse(customer2, provider2, "test-universe2", "10.10.30").getUniverseUUID();
    confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    settableRuntimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
    platformExecutorFactory = app.injector().instanceOf(PlatformExecutorFactory.class);
    platformScheduler = app.injector().instanceOf(PlatformScheduler.class);
    executorService = Executors.newCachedThreadPool();
    mockNodeAgentInstaller = mock(NodeAgentInstaller.class);
    nodeAgentEnabler =
        new NodeAgentEnabler(
            confGetter, platformExecutorFactory, platformScheduler, mockNodeAgentInstaller);
    nodeAgentEnabler.setUniverseInstallerExecutor(executorService);
    universeTaskBase =
        new TestUniverseTaskBase(
            app.injector().instanceOf(BaseTaskDependencies.class), nodeAgentEnabler);
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.nodeAgentEnablerRunInstaller.getKey(), String.valueOf(true));
  }

  @After
  public void tearDown() {
    if (executorService != null) {
      executorService.shutdownNow();
    }
  }

  private NodeAgent createNodeAgent(UUID customerUuid, NodeDetails node) {
    // Output is like Linux x86_64.
    NodeAgent nodeAgent = new NodeAgent();
    nodeAgent.setIp(node.cloudInfo.private_ip);
    nodeAgent.setName(node.nodeName);
    nodeAgent.setCustomerUuid(customerUuid);
    nodeAgent.setOsType(OSType.LINUX);
    nodeAgent.setArchType(ArchType.AMD64);
    nodeAgent.setState(State.READY);
    nodeAgent.setVersion("2024.2.4.0");
    nodeAgent.setHome("/home/yugabyte/node-agent");
    nodeAgent.save();
    return nodeAgent;
  }

  private static class TestUniverseTaskBase extends UniverseTaskBase {
    private final NodeAgentEnabler nodeAgentEnabler;
    private final RunnableTask runnableTask;

    public TestUniverseTaskBase(
        BaseTaskDependencies baseTaskDependencies, NodeAgentEnabler nodeAgentEnabler) {
      super(baseTaskDependencies);
      this.nodeAgentEnabler = nodeAgentEnabler;
      runnableTask = mock(RunnableTask.class);
      taskParams = mock(UniverseTaskParams.class);
    }

    @Override
    protected <T> T getInstanceOf(Class<T> clazz) {
      if (clazz == NodeAgentEnabler.class) {
        return clazz.cast(nodeAgentEnabler);
      }
      return super.getInstanceOf(clazz);
    }

    @Override
    protected RunnableTask getRunnableTask() {
      return runnableTask;
    }

    @Override
    public void run() {}
  }

  private Universe createAwsUniverse(
      Customer customer, Provider provider, String name, String ipPrefix) {
    CloudType providerType = Common.CloudType.valueOf(provider.getCode());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "default-key";
    userIntent.replicationFactor = 3;
    userIntent.regionList =
        provider.getAllRegions().stream().map(Region::getUuid).collect(Collectors.toList());
    userIntent.instanceType = "c3.large";
    userIntent.providerType = providerType;
    userIntent.provider = provider.getUuid().toString();
    userIntent.universeName = name;
    userIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    userIntent.useSystemd = true;
    Universe universe = ModelFactory.createUniverse(name, customer.getId(), providerType);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    for (NodeDetails node : universeDetails.nodeDetailsSet) {
      node.cloudInfo.private_ip = ipPrefix + "." + node.getNodeIdx();
      node.nodeUuid = UUID.randomUUID();
    }
    universe.setUniverseDetails(universeDetails);
    universe.save();
    return universe;
  }

  private NodeAgent createNodeAgent(UUID customerUuid, NodeDetails node, NodeAgent.State state) {
    NodeAgent.maybeGetByIp(node.cloudInfo.private_ip).ifPresent(NodeAgent::delete);
    NodeAgent nodeAgent = new NodeAgent();
    nodeAgent.setIp(node.cloudInfo.private_ip);
    nodeAgent.setName(node.nodeName);
    nodeAgent.setPort(9070);
    nodeAgent.setCustomerUuid(customerUuid);
    nodeAgent.setOsType(NodeAgent.OSType.LINUX);
    nodeAgent.setArchType(NodeAgent.ArchType.AMD64);
    nodeAgent.setVersion(Util.getYbaVersion());
    nodeAgent.setHome("/home/yugabyte/node-agent");
    nodeAgent.setState(state);
    nodeAgent.setConfig(new NodeAgent.Config());
    nodeAgent.save();
    return nodeAgent;
  }

  private void markUniverses() {
    // Enable marking for universes with provider1 only.
    provider1.getDetails().setEnableNodeAgent(false);
    provider1.save();
    provider2.getDetails().setEnableNodeAgent(true);
    provider2.save();
    nodeAgentEnabler.markUniverses();
    Universe universe1 = Universe.getOrBadRequest(universeUuid1);
    Universe universe01 = Universe.getOrBadRequest(universeUuid01);
    Universe universe2 = Universe.getOrBadRequest(universeUuid2);
    assertEquals(true, universe1.getUniverseDetails().installNodeAgent);
    assertEquals(true, universe01.getUniverseDetails().installNodeAgent);
    assertEquals(false, universe2.getUniverseDetails().installNodeAgent);
  }

  private void scanUniverses(boolean waitForCompletion) throws Exception {
    // Max 2 universes for any customer.
    for (int i = 0; i < 2; i++) {
      // Scan as many times as the number of universes to pick all the universes.
      nodeAgentEnabler.scanUniverses();
      if (waitForCompletion) {
        nodeAgentEnabler.waitFor(Duration.ofSeconds(10));
      }
    }
  }

  @Test
  public void testMarkUniversesForOldProvider() {
    provider1.getDetails().setEnableNodeAgent(false);
    provider1.save();
    provider2.getDetails().setEnableNodeAgent(true);
    provider2.save();
    Universe universe1 = Universe.getOrBadRequest(universeUuid1);
    Universe universe01 = Universe.getOrBadRequest(universeUuid01);
    Universe universe2 = Universe.getOrBadRequest(universeUuid2);
    assertEquals(false, universe1.getUniverseDetails().installNodeAgent);
    assertEquals(false, universe01.getUniverseDetails().installNodeAgent);
    assertEquals(false, universe2.getUniverseDetails().installNodeAgent);
    nodeAgentEnabler.markUniverses();
    universe1 = Universe.getOrBadRequest(universeUuid1);
    universe01 = Universe.getOrBadRequest(universeUuid01);
    universe2 = Universe.getOrBadRequest(universeUuid2);
    assertEquals(true, universe1.getUniverseDetails().installNodeAgent);
    assertEquals(true, universe01.getUniverseDetails().installNodeAgent);
    assertEquals(false, universe2.getUniverseDetails().installNodeAgent);
  }

  @Test
  public void testMarkUniversesForClientDisabled() {
    provider1.getDetails().setEnableNodeAgent(true);
    provider1.save();
    provider2.getDetails().setEnableNodeAgent(true);
    provider2.save();
    settableRuntimeConfigFactory
        .forProvider(provider1)
        .setValue(ProviderConfKeys.enableNodeAgentClient.getKey(), String.valueOf(false));
    Universe universe1 = Universe.getOrBadRequest(universeUuid1);
    Universe universe01 = Universe.getOrBadRequest(universeUuid01);
    Universe universe2 = Universe.getOrBadRequest(universeUuid2);
    assertEquals(false, universe1.getUniverseDetails().installNodeAgent);
    assertEquals(false, universe01.getUniverseDetails().installNodeAgent);
    assertEquals(false, universe2.getUniverseDetails().installNodeAgent);
    nodeAgentEnabler.markUniverses();
    universe1 = Universe.getOrBadRequest(universeUuid1);
    universe01 = Universe.getOrBadRequest(universeUuid01);
    universe2 = Universe.getOrBadRequest(universeUuid2);
    assertEquals(true, universe1.getUniverseDetails().installNodeAgent);
    assertEquals(true, universe01.getUniverseDetails().installNodeAgent);
    assertEquals(false, universe2.getUniverseDetails().installNodeAgent);
  }

  @Test
  public void testMarkUniversesOnInstallNodeAgentSubTask() {
    provider1.getDetails().setEnableNodeAgent(true);
    provider1.save();
    provider2.getDetails().setEnableNodeAgent(true);
    provider2.save();
    settableRuntimeConfigFactory
        .forProvider(provider1)
        .setValue(ProviderConfKeys.enableNodeAgentClient.getKey(), String.valueOf(false));
    Universe universe1 = Universe.getOrBadRequest(universeUuid1);
    Universe universe2 = Universe.getOrBadRequest(universeUuid2);
    assertEquals(false, universe1.getUniverseDetails().installNodeAgent);
    assertEquals(false, universe2.getUniverseDetails().installNodeAgent);
    // Node agent is disabled for provider1.
    SubTaskGroup group1 =
        universeTaskBase.createInstallNodeAgentTasks(
            Universe.getOrBadRequest(universeUuid1),
            Universe.getOrBadRequest(universeUuid1).getNodes());
    // Node agent is enabled for provider2.
    SubTaskGroup group2 =
        universeTaskBase.createInstallNodeAgentTasks(
            Universe.getOrBadRequest(universeUuid2),
            Universe.getOrBadRequest(universeUuid2).getNodes());
    universe1 = Universe.getOrBadRequest(universeUuid1);
    universe2 = Universe.getOrBadRequest(universeUuid2);
    assertEquals(false, universe1.getUniverseDetails().installNodeAgent);
    assertEquals(false, universe2.getUniverseDetails().installNodeAgent);
    // Client is disabled.
    assertEquals(0, group1.getSubTaskCount());
    // Node agent installation tasks should be added instead of marking.
    assertEquals(3, group2.getSubTaskCount());
  }

  @Test
  public void testInstallNodeAgentFailure() throws Exception {
    markUniverses();
    scanUniverses(true);
    Universe universe1 = Universe.getOrBadRequest(universeUuid1);
    Universe universe01 = Universe.getOrBadRequest(universeUuid01);
    Universe universe2 = Universe.getOrBadRequest(universeUuid2);
    // All the marked universes must have install invoked for each universe.
    for (NodeDetails node : universe1.getNodes()) {
      verify(mockNodeAgentInstaller, times(1))
          .install(eq(customer1.getUuid()), eq(universeUuid1), eq(node));
    }
    for (NodeDetails node : universe01.getNodes()) {
      verify(mockNodeAgentInstaller, times(1))
          .install(eq(customer1.getUuid()), eq(universeUuid01), eq(node));
    }
    for (NodeDetails node : universe2.getNodes()) {
      verify(mockNodeAgentInstaller, times(0))
          .install(eq(customer2.getUuid()), eq(universeUuid2), eq(node));
    }
    // Migration must not happen as install method did not return true.
    verify(mockNodeAgentInstaller, times(0)).migrate(eq(customer1.getUuid()), eq(universeUuid1));
    verify(mockNodeAgentInstaller, times(0)).migrate(eq(customer1.getUuid()), eq(universeUuid01));
    verify(mockNodeAgentInstaller, times(0)).migrate(eq(customer2.getUuid()), eq(universeUuid2));
  }

  @Test
  public void testInstallNodeAgentException() throws Exception {
    doThrow(new RuntimeException("Error occurred in installation"))
        .when(mockNodeAgentInstaller)
        .install(any(), any(), any());
    markUniverses();
    scanUniverses(true);
    Universe universe1 = Universe.getOrBadRequest(universeUuid1);
    Universe universe01 = Universe.getOrBadRequest(universeUuid01);
    Universe universe2 = Universe.getOrBadRequest(universeUuid2);
    // All the marked universes must have install invoked for each universe.
    for (NodeDetails node : universe1.getNodes()) {
      verify(mockNodeAgentInstaller, times(1))
          .install(eq(customer1.getUuid()), eq(universeUuid1), eq(node));
    }
    for (NodeDetails node : universe01.getNodes()) {
      verify(mockNodeAgentInstaller, times(1))
          .install(eq(customer1.getUuid()), eq(universeUuid01), eq(node));
    }
    for (NodeDetails node : universe2.getNodes()) {
      verify(mockNodeAgentInstaller, times(0))
          .install(eq(customer2.getUuid()), eq(universeUuid2), eq(node));
    }
    // Migration must not happen as install method threw exception.
    verify(mockNodeAgentInstaller, times(0)).migrate(eq(customer1.getUuid()), eq(universeUuid1));
    verify(mockNodeAgentInstaller, times(0)).migrate(eq(customer1.getUuid()), eq(universeUuid01));
    verify(mockNodeAgentInstaller, times(0)).migrate(eq(customer2.getUuid()), eq(universeUuid2));
  }

  @Test
  public void testInstallNodeAgentOneUniverseFailure() throws Exception {
    // Do not fail for other universes.
    when(mockNodeAgentInstaller.install(any(), any(), any())).thenReturn(true);
    // Fail for all nodes in a universe.
    doThrow(new RuntimeException("Error occurred in installation"))
        .when(mockNodeAgentInstaller)
        .install(any(), eq(universeUuid1), any());
    markUniverses();
    scanUniverses(true);
    Universe universe1 = Universe.getOrBadRequest(universeUuid1);
    Universe universe01 = Universe.getOrBadRequest(universeUuid01);
    Universe universe2 = Universe.getOrBadRequest(universeUuid2);
    for (NodeDetails node : universe1.getNodes()) {
      verify(mockNodeAgentInstaller, times(1))
          .install(eq(customer1.getUuid()), eq(universeUuid1), eq(node));
    }
    for (NodeDetails node : universe01.getNodes()) {
      verify(mockNodeAgentInstaller, times(1))
          .install(eq(customer1.getUuid()), eq(universeUuid01), eq(node));
    }
    for (NodeDetails node : universe2.getNodes()) {
      verify(mockNodeAgentInstaller, times(0))
          .install(eq(customer2.getUuid()), eq(universeUuid2), eq(node));
    }
    // Migration must happen only for the universe that did not fail.
    verify(mockNodeAgentInstaller, times(0)).migrate(eq(customer1.getUuid()), eq(universeUuid1));
    verify(mockNodeAgentInstaller, times(1)).migrate(eq(customer1.getUuid()), eq(universeUuid01));
    verify(mockNodeAgentInstaller, times(0)).migrate(eq(customer2.getUuid()), eq(universeUuid2));
    universe01 = Universe.getOrBadRequest(universeUuid01);
    // Field installNodeAgent must not be cleared because migration did not succeed.
    assertEquals(true, universe01.getUniverseDetails().installNodeAgent);
  }

  @Test
  public void testInstallNodeAgentOneNodeFailure() throws Exception {
    // Fail for a node in a universe.
    when(mockNodeAgentInstaller.install(any(), any(), any())).thenReturn(true);
    NodeDetails failedNode = Iterables.getLast(Universe.getOrBadRequest(universeUuid1).getNodes());
    doThrow(new RuntimeException("Error occurred in installation"))
        .when(mockNodeAgentInstaller)
        .install(any(), eq(universeUuid1), eq(failedNode));
    markUniverses();
    scanUniverses(true);
    Universe universe1 = Universe.getOrBadRequest(universeUuid1);
    Universe universe01 = Universe.getOrBadRequest(universeUuid01);
    Universe universe2 = Universe.getOrBadRequest(universeUuid2);
    for (NodeDetails node : universe1.getNodes()) {
      verify(mockNodeAgentInstaller, times(1))
          .install(eq(customer1.getUuid()), eq(universeUuid1), eq(node));
    }
    for (NodeDetails node : universe01.getNodes()) {
      verify(mockNodeAgentInstaller, times(1))
          .install(eq(customer1.getUuid()), eq(universeUuid01), eq(node));
    }
    for (NodeDetails node : universe2.getNodes()) {
      verify(mockNodeAgentInstaller, times(0))
          .install(eq(customer2.getUuid()), eq(universeUuid2), eq(node));
    }
    // Migration must happen only for the universe that did not fail.
    verify(mockNodeAgentInstaller, times(0)).migrate(eq(customer1.getUuid()), eq(universeUuid1));
    verify(mockNodeAgentInstaller, times(1)).migrate(eq(customer1.getUuid()), eq(universeUuid01));
    verify(mockNodeAgentInstaller, times(0)).migrate(eq(customer2.getUuid()), eq(universeUuid2));
    universe01 = Universe.getOrBadRequest(universeUuid01);
    // Field installNodeAgent must not be cleared because migration did not succeed.
    assertEquals(true, universe01.getUniverseDetails().installNodeAgent);
  }

  @Test
  public void testInstallNodeAgentAllSuccess() throws Exception {
    when(mockNodeAgentInstaller.install(any(), any(), any())).thenReturn(true);
    markUniverses();
    scanUniverses(true);
    Universe universe1 = Universe.getOrBadRequest(universeUuid1);
    Universe universe01 = Universe.getOrBadRequest(universeUuid01);
    Universe universe2 = Universe.getOrBadRequest(universeUuid2);
    for (NodeDetails node : universe1.getNodes()) {
      verify(mockNodeAgentInstaller, times(1))
          .install(eq(customer1.getUuid()), eq(universeUuid1), eq(node));
    }
    for (NodeDetails node : universe01.getNodes()) {
      verify(mockNodeAgentInstaller, times(1))
          .install(eq(customer1.getUuid()), eq(universeUuid01), eq(node));
    }
    for (NodeDetails node : universe2.getNodes()) {
      verify(mockNodeAgentInstaller, times(0))
          .install(eq(customer2.getUuid()), eq(universeUuid2), eq(node));
    }
    // Migration must happen for eligible universes because the install method returned success.
    verify(mockNodeAgentInstaller, times(1)).migrate(eq(customer1.getUuid()), eq(universeUuid1));
    verify(mockNodeAgentInstaller, times(1)).migrate(eq(customer1.getUuid()), eq(universeUuid01));
    verify(mockNodeAgentInstaller, times(0)).migrate(eq(customer2.getUuid()), eq(universeUuid2));
    universe1 = Universe.getOrBadRequest(universeUuid1);
    universe01 = Universe.getOrBadRequest(universeUuid01);
    // Field installNodeAgent must not be cleared because migration did not succeed.
    assertEquals(true, universe1.getUniverseDetails().installNodeAgent);
    assertEquals(true, universe01.getUniverseDetails().installNodeAgent);
  }

  @Test
  public void testInstallNodeAgentOrder() throws Exception {
    when(mockNodeAgentInstaller.install(any(), any(), any())).thenReturn(true);
    markUniverses();
    LinkedList<UUID> universeUuids = new LinkedList<>();
    for (int i = 0; i < 2; i++) {
      // Scan as many times as the number of universes to pick all the universes.
      nodeAgentEnabler.scanUniverses();
      nodeAgentEnabler.waitFor(Duration.ofSeconds(10));
      nodeAgentEnabler.getUniverseNodeAgentInstallers().stream()
          .map(UniverseNodeAgentInstaller::getUniverseUuid)
          .forEach(universeUuids::add);
    }
    List<UUID> expectedOrder = new ArrayList<>();
    // Customer 1, Universe 1.
    expectedOrder.add(universeUuid1);
    // Customer 1, Universe 2.
    expectedOrder.add(universeUuid01);
    assertEquals(expectedOrder, universeUuids);
  }

  @Test
  public void testCancelInstallNodeAgent() throws Exception {
    CountDownLatch latch1 = new CountDownLatch(1);
    CountDownLatch latch2 = new CountDownLatch(1);
    doAnswer(
            inv -> {
              // Let it take long time.
              latch1.countDown();
              latch2.await();
              return true;
            })
        .when(mockNodeAgentInstaller)
        .install(any(), eq(universeUuid1), any());
    markUniverses();
    // Scan to start the installation but do not wait for completion otherwise, it'll time out.
    scanUniverses(false);
    // Wait for the installation to start.
    latch1.await();
    nodeAgentEnabler.cancelForUniverse(universeUuid1);
    // This must not time out as the stuck installer is cancelled.
    scanUniverses(true);
  }

  @Test
  public void testInstallNodeAgentOnUniverseDestroy() throws Exception {
    CountDownLatch latch1 = new CountDownLatch(1);
    CountDownLatch latch2 = new CountDownLatch(1);
    doAnswer(
            inv -> {
              // Let it take long time.
              latch1.countDown();
              latch2.await();
              return true;
            })
        .when(mockNodeAgentInstaller)
        .install(any(), eq(universeUuid1), any());
    markUniverses();
    // Scan to start the installation but do not wait for completion otherwise, it'll time out.
    scanUniverses(false);
    // Wait for the installation to start.
    latch1.await();
    Universe universe1 = Universe.getOrBadRequest(universeUuid1);
    universe1.delete();
    // This run must cancel the already running installation.
    scanUniverses(true);
  }

  @Test
  public void testReInstallNodeAgentFailure() throws Exception {
    markUniverses();
    Universe universe1 = Universe.getOrBadRequest(universeUuid1);
    Universe universe01 = Universe.getOrBadRequest(universeUuid01);
    Universe universe2 = Universe.getOrBadRequest(universeUuid2);
    settableRuntimeConfigFactory
        .forUniverse(universe1)
        .setValue(UniverseConfKeys.nodeAgentEnablerReinstallCooldown.getKey(), "0s");
    NodeDetails reinstallNode = Iterables.getLast(universe1.getNodes());
    // Node agent is already installed in a universe marked with installNodeAgent.
    createNodeAgent(customer1.getUuid(), reinstallNode, NodeAgent.State.REGISTERING);
    scanUniverses(true);
    for (NodeDetails node : universe1.getNodes()) {
      if (reinstallNode.equals(node)) {
        verify(mockNodeAgentInstaller, times(1))
            .reinstall(eq(customer1.getUuid()), eq(universeUuid1), eq(node), any(), any());
      } else {
        verify(mockNodeAgentInstaller, times(1))
            .install(eq(customer1.getUuid()), eq(universeUuid1), eq(node));
      }
    }
    for (NodeDetails node : universe01.getNodes()) {
      verify(mockNodeAgentInstaller, times(1))
          .install(eq(customer1.getUuid()), eq(universeUuid01), eq(node));
    }
    for (NodeDetails node : universe2.getNodes()) {
      verify(mockNodeAgentInstaller, times(0))
          .install(eq(customer2.getUuid()), eq(universeUuid2), eq(node));
    }
    verify(mockNodeAgentInstaller, times(0)).migrate(eq(customer1.getUuid()), eq(universeUuid1));
    verify(mockNodeAgentInstaller, times(0)).migrate(eq(customer1.getUuid()), eq(universeUuid01));
    verify(mockNodeAgentInstaller, times(0)).migrate(eq(customer2.getUuid()), eq(universeUuid2));
  }

  @Test
  public void testReInstallNodeAgentSuccess() throws Exception {
    markUniverses();
    Universe universe1 = Universe.getOrBadRequest(universeUuid1);
    Universe universe01 = Universe.getOrBadRequest(universeUuid01);
    Universe universe2 = Universe.getOrBadRequest(universeUuid2);
    settableRuntimeConfigFactory
        .forUniverse(universe1)
        .setValue(UniverseConfKeys.nodeAgentEnablerReinstallCooldown.getKey(), "1m");
    // Node agent is already installed in a universe marked with installNodeAgent.
    NodeDetails reinstallNode = Iterables.getFirst(universe1.getNodes(), null);
    createNodeAgent(customer1.getUuid(), reinstallNode, NodeAgent.State.REGISTERING);
    when(mockNodeAgentInstaller.install(any(), any(), any())).thenReturn(true);
    when(mockNodeAgentInstaller.reinstall(any(), any(), eq(reinstallNode), any(), any()))
        .thenReturn(true);
    scanUniverses(true);
    for (NodeDetails node : universe1.getNodes()) {
      if (reinstallNode.equals(node)) {
        verify(mockNodeAgentInstaller, times(1))
            .reinstall(
                eq(customer1.getUuid()),
                eq(universeUuid1),
                eq(node),
                any(),
                eq(Duration.ofMinutes(1)));
      } else {
        verify(mockNodeAgentInstaller, times(1))
            .install(eq(customer1.getUuid()), eq(universeUuid1), eq(node));
      }
    }
    for (NodeDetails node : universe01.getNodes()) {
      verify(mockNodeAgentInstaller, times(1))
          .install(eq(customer1.getUuid()), eq(universeUuid01), eq(node));
    }
    for (NodeDetails node : universe2.getNodes()) {
      verify(mockNodeAgentInstaller, times(0))
          .install(eq(customer2.getUuid()), eq(universeUuid2), eq(node));
    }
    verify(mockNodeAgentInstaller, times(1)).migrate(eq(customer1.getUuid()), eq(universeUuid1));
    verify(mockNodeAgentInstaller, times(1)).migrate(eq(customer1.getUuid()), eq(universeUuid01));
    verify(mockNodeAgentInstaller, times(0)).migrate(eq(customer2.getUuid()), eq(universeUuid2));
    universe1 = Universe.getOrBadRequest(universeUuid1);
    universe01 = Universe.getOrBadRequest(universeUuid01);
    // Field installNodeAgent must not be cleared because migration did not succeed.
    assertEquals(true, universe1.getUniverseDetails().installNodeAgent);
    assertEquals(true, universe01.getUniverseDetails().installNodeAgent);
  }

  @Test
  public void testMigrateNodeAgentSuccess() throws Exception {
    when(mockNodeAgentInstaller.install(any(), any(), any())).thenReturn(true);
    doAnswer(
            inv -> {
              Object[] objects = inv.getArguments();
              UUID universeUuid = (UUID) objects[1];
              Universe universe = Universe.getOrBadRequest(universeUuid);
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.installNodeAgent = false;
              universe.setUniverseDetails(universeDetails);
              universe.save();
              return true;
            })
        .when(mockNodeAgentInstaller)
        .migrate(any(), any());
    markUniverses();
    scanUniverses(true);
    // Migration must happen for eligible universes because the install method returned success.
    verify(mockNodeAgentInstaller, times(1)).migrate(eq(customer1.getUuid()), eq(universeUuid1));
    verify(mockNodeAgentInstaller, times(1)).migrate(eq(customer1.getUuid()), eq(universeUuid01));
    verify(mockNodeAgentInstaller, times(0)).migrate(eq(customer2.getUuid()), eq(universeUuid2));
    Universe universe1 = Universe.getOrBadRequest(universeUuid1);
    Universe universe01 = Universe.getOrBadRequest(universeUuid01);
    Universe universe2 = Universe.getOrBadRequest(universeUuid2);
    // Field installNodeAgent must be cleared because migration did not succeed.
    assertEquals(false, universe1.getUniverseDetails().installNodeAgent);
    assertEquals(false, universe01.getUniverseDetails().installNodeAgent);
    // Run scan again that should not have any effect.
    scanUniverses(true);
    for (NodeDetails node : universe1.getNodes()) {
      verify(mockNodeAgentInstaller, times(1))
          .install(eq(customer1.getUuid()), eq(universeUuid1), eq(node));
    }
    for (NodeDetails node : universe01.getNodes()) {
      verify(mockNodeAgentInstaller, times(1))
          .install(eq(customer1.getUuid()), eq(universeUuid01), eq(node));
    }
    for (NodeDetails node : universe2.getNodes()) {
      verify(mockNodeAgentInstaller, times(0))
          .install(eq(customer2.getUuid()), eq(universeUuid2), eq(node));
    }
  }

  @Test
  public void testSkipInstallNodeAgents() throws Exception {
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.nodeAgentEnablerRunInstaller.getKey(), String.valueOf(false));
    markUniverses();
    scanUniverses(true);
    Universe universe1 = Universe.getOrBadRequest(universeUuid1);
    Universe universe2 = Universe.getOrBadRequest(universeUuid2);
    // Installation must not happen because the installer is disabled.
    for (NodeDetails node : universe1.getNodes()) {
      verify(mockNodeAgentInstaller, times(0))
          .install(eq(customer1.getUuid()), eq(universeUuid1), eq(node));
    }
    for (NodeDetails node : universe2.getNodes()) {
      verify(mockNodeAgentInstaller, times(0))
          .install(eq(customer2.getUuid()), eq(universeUuid2), eq(node));
    }
    universe1 = Universe.getOrBadRequest(universeUuid1);
    universe2 = Universe.getOrBadRequest(universeUuid01);
    // Field installNodeAgent must still be set.
    assertEquals(true, universe1.getUniverseDetails().installNodeAgent);
    assertEquals(true, universe2.getUniverseDetails().installNodeAgent);
  }

  @Test
  public void testUpdateMissingNodeAgents() {
    Universe universe = Universe.getOrBadRequest(universeUuid1);
    List<NodeDetails> nodes = new ArrayList<>(universe.getNodes());
    assertEquals(3, nodes.size());
    createNodeAgent(customer1.getUuid(), nodes.get(0));
    createNodeAgent(customer1.getUuid(), nodes.get(1));
    nodeAgentEnabler.updateMissingNodeAgents(customer1.getUuid(), universeUuid1);
    universe = Universe.getOrBadRequest(universeUuid1);
    assertEquals(true, universe.getUniverseDetails().nodeAgentMissing);
    NodeAgent nodeAgent = createNodeAgent(customer1.getUuid(), nodes.get(2));
    nodeAgentEnabler.updateMissingNodeAgents(customer1.getUuid(), universeUuid1);
    universe = Universe.getOrBadRequest(universeUuid1);
    assertEquals(false, universe.getUniverseDetails().nodeAgentMissing);
    nodeAgent.delete();
    nodeAgentEnabler.updateMissingNodeAgents(customer1.getUuid(), universeUuid1);
    universe = Universe.getOrBadRequest(universeUuid1);
    assertEquals(true, universe.getUniverseDetails().nodeAgentMissing);
  }
}
