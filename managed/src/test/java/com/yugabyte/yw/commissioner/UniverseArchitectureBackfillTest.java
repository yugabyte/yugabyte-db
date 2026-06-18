// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.UniverseArchitectureResolver;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.time.Duration;
import java.util.Collections;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import org.apache.pekko.actor.Cancellable;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UniverseArchitectureBackfillTest extends FakeDBApplication {

  private static final String LIVE_NODE_SCHEDULER_NAME = "UniverseArchitectureBackfill.liveNode";

  @Mock private PlatformScheduler mockPlatformScheduler;
  @Mock private UniverseArchitectureResolver mockResolver;

  private UniverseArchitectureBackfill backfill;
  private UniverseArchitectureBackfill mockedBackfill;

  @Before
  public void setUp() {
    backfill =
        new UniverseArchitectureBackfill(
            app.injector().instanceOf(PlatformScheduler.class),
            app.injector().instanceOf(UniverseArchitectureResolver.class));
    mockedBackfill = new UniverseArchitectureBackfill(mockPlatformScheduler, mockResolver);
  }

  @Test
  public void testBackfillSetsArchFromNodeAgentMetadata() {
    Customer customer = ModelFactory.testCustomer();
    ModelFactory.newProvider(customer, CloudType.onprem);

    Universe universe =
        ModelFactory.createUniverse(
            "backfill-test", UUID.randomUUID(), customer.getId(), CloudType.onprem);
    universe = ModelFactory.addNodesToUniverse(universe.getUniverseUUID(), 1);
    UniverseDefinitionTaskParams params = universe.getUniverseDetails();
    params.arch = null;
    NodeDetails node = params.nodeDetailsSet.iterator().next();
    node.nodeName = "backfill-node";
    node.cloudInfo.private_ip = "10.0.0.2";
    node.state = NodeState.Live;
    node.isTserver = true;
    universe.setUniverseDetails(params);
    universe.update();

    NodeAgent nodeAgent = new NodeAgent();
    nodeAgent.setIp(node.cloudInfo.private_ip);
    nodeAgent.setName(node.nodeName);
    nodeAgent.setCustomerUuid(customer.getUuid());
    nodeAgent.setOsType(OSType.LINUX);
    nodeAgent.setArchType(ArchType.ARM64);
    nodeAgent.setState(State.READY);
    nodeAgent.setVersion("2024.2.4.0");
    nodeAgent.setHome("/home/yugabyte/node-agent");
    nodeAgent.setConfig(new NodeAgent.Config());
    nodeAgent.save();

    assertNull(Universe.getOrBadRequest(universe.getUniverseUUID()).getUniverseDetails().arch);

    backfill.start();

    assertEquals(
        Architecture.aarch64,
        Universe.getOrBadRequest(universe.getUniverseUUID()).getUniverseDetails().arch);
  }

  @Test
  public void testStartReturnsEarlyWhenNoUniversesNeedBackfill() {
    mockedBackfill.start();

    verify(mockResolver, never()).resolveFromMetadata(anyList());
    verify(mockPlatformScheduler, never())
        .scheduleOnce(anyString(), any(Duration.class), any(Runnable.class));
  }

  @Test
  public void testBackfillFromLiveNodesSetsArch() {
    Customer customer = ModelFactory.testCustomer();
    Universe universe =
        ModelFactory.createUniverse(
            "live-node-backfill", UUID.randomUUID(), customer.getId(), CloudType.onprem);
    UniverseDefinitionTaskParams params = universe.getUniverseDetails();
    params.arch = null;
    universe.setUniverseDetails(params);
    universe.update();

    when(mockResolver.resolveFromLiveNode(any(Universe.class)))
        .thenReturn(Optional.of(Architecture.x86_64));

    mockedBackfill.backfillFromLiveNodes(List.of(universe.getUniverseUUID()));

    assertEquals(
        Architecture.x86_64,
        Universe.getOrBadRequest(universe.getUniverseUUID()).getUniverseDetails().arch);
  }

  @Test
  public void testStartSchedulesLiveNodeProbeWhenMetadataFails() {
    Customer customer = ModelFactory.testCustomer();
    Universe universe =
        ModelFactory.createUniverse(
            "pending-live-node", UUID.randomUUID(), customer.getId(), CloudType.onprem);
    UniverseDefinitionTaskParams params = universe.getUniverseDetails();
    params.arch = null;
    universe.setUniverseDetails(params);
    universe.update();

    when(mockResolver.resolveFromMetadata(anyList()))
        .thenReturn(List.of(universe.getUniverseUUID()));
    when(mockResolver.resolveFromLiveNode(any(Universe.class)))
        .thenReturn(Optional.of(Architecture.aarch64));
    when(mockPlatformScheduler.scheduleOnce(anyString(), any(Duration.class), any(Runnable.class)))
        .thenAnswer(
            invocation -> {
              Runnable runnable = invocation.getArgument(2);
              runnable.run();
              return mock(Cancellable.class);
            });

    mockedBackfill.start();

    verify(mockPlatformScheduler)
        .scheduleOnce(eq(LIVE_NODE_SCHEDULER_NAME), eq(Duration.ZERO), any(Runnable.class));
    assertEquals(
        Architecture.aarch64,
        Universe.getOrBadRequest(universe.getUniverseUUID()).getUniverseDetails().arch);
  }

  @Test
  public void testStartDoesNotScheduleLiveNodeWhenMetadataSucceeds() {
    Customer customer = ModelFactory.testCustomer();
    Universe universe =
        ModelFactory.createUniverse(
            "metadata-resolved", UUID.randomUUID(), customer.getId(), CloudType.onprem);
    UniverseDefinitionTaskParams params = universe.getUniverseDetails();
    params.arch = null;
    universe.setUniverseDetails(params);
    universe.update();

    when(mockResolver.resolveFromMetadata(anyList())).thenReturn(List.of());

    mockedBackfill.start();

    verify(mockPlatformScheduler, never())
        .scheduleOnce(anyString(), any(Duration.class), any(Runnable.class));
  }

  @Test
  public void testStartSkipsUniverseWithArchAlreadySet() {
    Customer customer = ModelFactory.testCustomer();
    Universe universe =
        ModelFactory.createUniverse(
            "arch-already-set", UUID.randomUUID(), customer.getId(), CloudType.onprem);
    UniverseDefinitionTaskParams params = universe.getUniverseDetails();
    params.arch = Architecture.aarch64;
    universe.setUniverseDetails(params);
    universe.update();

    mockedBackfill.start();

    verify(mockResolver, never()).resolveFromMetadata(anyList());
    verify(mockResolver, never()).resolveFromLiveNode(any(Universe.class));
    verify(mockPlatformScheduler, never())
        .scheduleOnce(anyString(), any(Duration.class), any(Runnable.class));
    assertEquals(
        Architecture.aarch64,
        Universe.getOrBadRequest(universe.getUniverseUUID()).getUniverseDetails().arch);
  }

  @Test
  public void testBackfillFromLiveNodesLeavesArchUnsetWhenProbeFails() {
    Customer customer = ModelFactory.testCustomer();
    Universe universe =
        ModelFactory.createUniverse(
            "live-node-fails", UUID.randomUUID(), customer.getId(), CloudType.onprem);
    UniverseDefinitionTaskParams params = universe.getUniverseDetails();
    params.arch = null;
    universe.setUniverseDetails(params);
    universe.update();

    when(mockResolver.resolveFromLiveNode(any(Universe.class))).thenReturn(Optional.empty());

    mockedBackfill.backfillFromLiveNodes(Collections.singletonList(universe.getUniverseUUID()));

    assertNull(Universe.getOrBadRequest(universe.getUniverseUUID()).getUniverseDetails().arch);
  }
}
