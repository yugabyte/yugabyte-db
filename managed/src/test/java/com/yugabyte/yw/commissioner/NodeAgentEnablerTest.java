// Copyright (c) YugaByte, Inc.

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

import com.google.common.collect.ImmutableList;
import com.google.common.collect.Iterables;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.NodeAgentEnabler.NodeAgentInstaller;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
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
  private BaseTaskDependencies baseTaskDependencies;

  @Before
  public void setUp() {
    customer1 = ModelFactory.testCustomer();
    customer2 = ModelFactory.testCustomer();
    provider1 = ModelFactory.awsProvider(customer1);
    provider2 = ModelFactory.awsProvider(customer2);
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
    baseTaskDependencies = app.injector().instanceOf(BaseTaskDependencies.class);
    executorService = Executors.newCachedThreadPool();
    mockNodeAgentInstaller = mock(NodeAgentInstaller.class);
    nodeAgentEnabler =
        new NodeAgentEnabler(
            confGetter, platformExecutorFactory, platformScheduler, mockNodeAgentInstaller);
    nodeAgentEnabler.setUniverseInstallerExecutor(executorService);
    universeTaskBase = new TestUniverseTaskBase(baseTaskDependencies);
  }

  @After
  public void tearDown() {
    if (executorService != null) {
      executorService.shutdownNow();
    }
  }

  private static class TestUniverseTaskBase extends UniverseTaskBase {
    private final RunnableTask runnableTask;

    public TestUniverseTaskBase(BaseTaskDependencies baseTaskDependencies) {
      super(baseTaskDependencies);
      runnableTask = mock(RunnableTask.class);
      taskParams = mock(UniverseTaskParams.class);
    }

    public UniverseTaskParams getMockParams() {
      return (UniverseTaskParams) super.taskParams();
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
    Region region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "default-key";
    userIntent.replicationFactor = 3;
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.instanceType = "c3.large";
    userIntent.providerType = providerType;
    userIntent.provider = provider.getUuid().toString();
    userIntent.universeName = name;
    userIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    Universe universe = ModelFactory.createUniverse(name, customer.getId(), providerType);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    for (NodeDetails node : universeDetails.nodeDetailsSet) {
      node.cloudInfo.private_ip = ipPrefix + "." + node.getNodeIdx();
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
    nodeAgent.setVersion("2024.2.0.0");
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
    assertEquals(true, universe1.getUniverseDetails().disableNodeAgent);
    assertEquals(true, universe01.getUniverseDetails().disableNodeAgent);
    assertEquals(false, universe2.getUniverseDetails().disableNodeAgent);
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
    assertEquals(false, universe1.getUniverseDetails().disableNodeAgent);
    assertEquals(false, universe01.getUniverseDetails().disableNodeAgent);
    assertEquals(false, universe2.getUniverseDetails().disableNodeAgent);
    nodeAgentEnabler.markUniverses();
    universe1 = Universe.getOrBadRequest(universeUuid1);
    universe01 = Universe.getOrBadRequest(universeUuid01);
    universe2 = Universe.getOrBadRequest(universeUuid2);
    assertEquals(true, universe1.getUniverseDetails().disableNodeAgent);
    assertEquals(true, universe01.getUniverseDetails().disableNodeAgent);
    assertEquals(false, universe2.getUniverseDetails().disableNodeAgent);
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
    assertEquals(false, universe1.getUniverseDetails().disableNodeAgent);
    assertEquals(false, universe01.getUniverseDetails().disableNodeAgent);
    assertEquals(false, universe2.getUniverseDetails().disableNodeAgent);
    nodeAgentEnabler.markUniverses();
    universe1 = Universe.getOrBadRequest(universeUuid1);
    universe01 = Universe.getOrBadRequest(universeUuid01);
    universe2 = Universe.getOrBadRequest(universeUuid2);
    assertEquals(true, universe1.getUniverseDetails().disableNodeAgent);
    assertEquals(true, universe01.getUniverseDetails().disableNodeAgent);
    assertEquals(false, universe2.getUniverseDetails().disableNodeAgent);
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
    assertEquals(false, universe1.getUniverseDetails().disableNodeAgent);
    assertEquals(false, universe2.getUniverseDetails().disableNodeAgent);
    // Node agent is disabled for provider1.
    when(universeTaskBase.getMockParams().getUniverseUUID()).thenReturn(universeUuid1);
    universeTaskBase.createInstallNodeAgentTasks(
        Universe.getOrBadRequest(universeUuid1).getNodes());
    // Node agent is enabled for provider2.
    when(universeTaskBase.getMockParams().getUniverseUUID()).thenReturn(universeUuid2);
    universeTaskBase.createInstallNodeAgentTasks(
        Universe.getOrBadRequest(universeUuid2).getNodes());
    universe1 = Universe.getOrBadRequest(universeUuid1);
    universe2 = Universe.getOrBadRequest(universeUuid2);
    assertEquals(true, universe1.getUniverseDetails().disableNodeAgent);
    assertEquals(false, universe2.getUniverseDetails().disableNodeAgent);
  }

  @Test
  public void testInstallNodeAgentFailure() throws Exception {
    markUniverses();
    nodeAgentEnabler.scanUniverses();
    nodeAgentEnabler.waitFor(Duration.ofSeconds(10));
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
    nodeAgentEnabler.scanUniverses();
    nodeAgentEnabler.waitFor(Duration.ofSeconds(10));
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
    nodeAgentEnabler.scanUniverses();
    nodeAgentEnabler.waitFor(Duration.ofSeconds(10));
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
    // Field disableNodeAgent must not be cleared because migration did not succeed.
    assertEquals(true, universe01.getUniverseDetails().disableNodeAgent);
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
    nodeAgentEnabler.scanUniverses();
    nodeAgentEnabler.waitFor(Duration.ofSeconds(10));
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
    // Field disableNodeAgent must not be cleared because migration did not succeed.
    assertEquals(true, universe01.getUniverseDetails().disableNodeAgent);
  }

  @Test
  public void testInstallNodeAgentAllSuccess() throws Exception {
    when(mockNodeAgentInstaller.install(any(), any(), any())).thenReturn(true);
    markUniverses();
    nodeAgentEnabler.scanUniverses();
    nodeAgentEnabler.waitFor(Duration.ofSeconds(10));
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
    // Field disableNodeAgent must not be cleared because migration did not succeed.
    assertEquals(true, universe1.getUniverseDetails().disableNodeAgent);
    assertEquals(true, universe01.getUniverseDetails().disableNodeAgent);
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
        .install(any(), any(), any());
    markUniverses();
    nodeAgentEnabler.scanUniverses();
    // Wait for the install to start.
    latch1.await();
    Universe universe1 = Universe.getOrBadRequest(universeUuid1);
    universe1.delete();
    // This run must cancel the already running installation.
    nodeAgentEnabler.scanUniverses();
    // This must not time out.
    nodeAgentEnabler.waitFor(Duration.ofSeconds(10));
  }

  @Test
  public void testReInstallNodeAgentFailure() throws Exception {
    markUniverses();
    Universe universe1 = Universe.getOrBadRequest(universeUuid1);
    Universe universe01 = Universe.getOrBadRequest(universeUuid01);
    Universe universe2 = Universe.getOrBadRequest(universeUuid2);
    NodeDetails reinstallNode = Iterables.getLast(universe1.getNodes());
    // Node agent is already installed in a universe marked with installNodeAgent.
    createNodeAgent(customer1.getUuid(), reinstallNode, NodeAgent.State.REGISTERING);
    nodeAgentEnabler.scanUniverses();
    nodeAgentEnabler.waitFor(Duration.ofSeconds(10));
    for (NodeDetails node : universe1.getNodes()) {
      if (reinstallNode.equals(node)) {
        verify(mockNodeAgentInstaller, times(1))
            .reinstall(eq(customer1.getUuid()), eq(universeUuid1), eq(node), any());
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
    // Node agent is already installed in a universe marked with installNodeAgent.
    NodeDetails reinstallNode = Iterables.getFirst(universe1.getNodes(), null);
    createNodeAgent(customer1.getUuid(), reinstallNode, NodeAgent.State.REGISTERING);
    when(mockNodeAgentInstaller.install(any(), any(), any())).thenReturn(true);
    when(mockNodeAgentInstaller.reinstall(any(), any(), eq(reinstallNode), any())).thenReturn(true);
    nodeAgentEnabler.scanUniverses();
    nodeAgentEnabler.waitFor(Duration.ofSeconds(10));
    for (NodeDetails node : universe1.getNodes()) {
      if (reinstallNode.equals(node)) {
        verify(mockNodeAgentInstaller, times(1))
            .reinstall(eq(customer1.getUuid()), eq(universeUuid1), eq(node), any());
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
    // Field disableNodeAgent must not be cleared because migration did not succeed.
    assertEquals(true, universe1.getUniverseDetails().disableNodeAgent);
    assertEquals(true, universe01.getUniverseDetails().disableNodeAgent);
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
              universeDetails.disableNodeAgent = false;
              universe.setUniverseDetails(universeDetails);
              universe.save();
              return true;
            })
        .when(mockNodeAgentInstaller)
        .migrate(any(), any());
    markUniverses();
    nodeAgentEnabler.scanUniverses();
    nodeAgentEnabler.waitFor(Duration.ofSeconds(10));
    // Migration must happen for eligible universes because the install method returned success.
    verify(mockNodeAgentInstaller, times(1)).migrate(eq(customer1.getUuid()), eq(universeUuid1));
    verify(mockNodeAgentInstaller, times(1)).migrate(eq(customer1.getUuid()), eq(universeUuid01));
    verify(mockNodeAgentInstaller, times(0)).migrate(eq(customer2.getUuid()), eq(universeUuid2));
    Universe universe1 = Universe.getOrBadRequest(universeUuid1);
    Universe universe01 = Universe.getOrBadRequest(universeUuid01);
    Universe universe2 = Universe.getOrBadRequest(universeUuid2);
    // Field disableNodeAgent must be cleared because migration did not succeed.
    assertEquals(false, universe1.getUniverseDetails().disableNodeAgent);
    assertEquals(false, universe01.getUniverseDetails().disableNodeAgent);
    // Run scan again that should not have any effect.
    nodeAgentEnabler.scanUniverses();
    nodeAgentEnabler.waitFor(Duration.ofSeconds(10));
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
}
