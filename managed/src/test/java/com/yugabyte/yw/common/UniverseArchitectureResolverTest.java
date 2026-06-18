// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class UniverseArchitectureResolverTest extends FakeDBApplication {

  private UniverseArchitectureResolver resolver;

  @Before
  public void setUp() {
    resolver = app.injector().instanceOf(UniverseArchitectureResolver.class);
  }

  @Test
  public void testResolveFromMetadataSetsArchFromNodeAgent() {
    Customer customer = ModelFactory.testCustomer();
    ModelFactory.newProvider(customer, CloudType.onprem);

    Universe universe =
        ModelFactory.createUniverse(
            "node-agent-arch", UUID.randomUUID(), customer.getId(), CloudType.onprem);
    universe = ModelFactory.addNodesToUniverse(universe.getUniverseUUID(), 1);
    UniverseDefinitionTaskParams params = universe.getUniverseDetails();
    params.arch = null;
    NodeDetails node = params.nodeDetailsSet.iterator().next();
    node.nodeName = "node-agent-test-node";
    node.cloudInfo.private_ip = "10.0.0.1";
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

    List<UUID> unresolved = resolver.resolveFromMetadata(List.of(universe.getUniverseUUID()));

    assertTrue(unresolved.isEmpty());
    assertEquals(
        Architecture.aarch64,
        Universe.getOrBadRequest(universe.getUniverseUUID()).getUniverseDetails().arch);
  }

  @Test
  public void testResolveFromMetadataReturnsUnresolvedWhenNodeAgentMissing() {
    Customer customer = ModelFactory.testCustomer();
    Universe universe =
        ModelFactory.createUniverse(
            "no-node-agent", UUID.randomUUID(), customer.getId(), CloudType.onprem);
    universe = ModelFactory.addNodesToUniverse(universe.getUniverseUUID(), 1);
    UniverseDefinitionTaskParams params = universe.getUniverseDetails();
    params.arch = null;
    universe.setUniverseDetails(params);
    universe.update();

    List<UUID> unresolved = resolver.resolveFromMetadata(List.of(universe.getUniverseUUID()));

    assertEquals(List.of(universe.getUniverseUUID()), unresolved);
    assertEquals(
        null, Universe.getOrBadRequest(universe.getUniverseUUID()).getUniverseDetails().arch);
  }

  @Test
  public void testResolveFromMetadataSkipsUniverseWithArchAlreadySet() {
    Customer customer = ModelFactory.testCustomer();
    Universe universe =
        ModelFactory.createUniverse(
            "arch-set", UUID.randomUUID(), customer.getId(), CloudType.onprem);
    UniverseDefinitionTaskParams params = universe.getUniverseDetails();
    params.arch = Architecture.x86_64;
    universe.setUniverseDetails(params);
    universe.update();

    List<UUID> unresolved = resolver.resolveFromMetadata(List.of(universe.getUniverseUUID()));

    assertTrue(unresolved.isEmpty());
    assertEquals(
        Architecture.x86_64,
        Universe.getOrBadRequest(universe.getUniverseUUID()).getUniverseDetails().arch);
  }

  @Test
  public void testResolveFromLiveNodeSkipsPausedUniverse() {
    Customer customer = ModelFactory.testCustomer();
    Universe universe =
        ModelFactory.createUniverse(
            "paused-universe", UUID.randomUUID(), customer.getId(), CloudType.onprem);
    universe = ModelFactory.addNodesToUniverse(universe.getUniverseUUID(), 1);
    UniverseDefinitionTaskParams params = universe.getUniverseDetails();
    params.arch = null;
    params.universePaused = true;
    universe.setUniverseDetails(params);
    universe.update();

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    Optional<Architecture> arch = resolver.resolveFromLiveNode(universe);
    assertFalse(arch.isPresent());
  }

  @Test
  public void testResolveFromLiveNodeSkipsUniverseWithUpdateInProgress() {
    Customer customer = ModelFactory.testCustomer();
    Universe universe =
        ModelFactory.createUniverse(
            "updating-universe", UUID.randomUUID(), customer.getId(), CloudType.onprem);
    universe = ModelFactory.addNodesToUniverse(universe.getUniverseUUID(), 1);
    UniverseDefinitionTaskParams params = universe.getUniverseDetails();
    params.arch = null;
    params.updateInProgress = true;
    universe.setUniverseDetails(params);
    universe.update();

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    Optional<Architecture> arch = resolver.resolveFromLiveNode(universe);
    assertFalse(arch.isPresent());
  }

  @Test
  public void testResolveFromLiveNodeTriesNextNodeWhenFirstProbeFails() {
    NodeUniverseManager nodeUniverseManager = mock(NodeUniverseManager.class);
    UniverseArchitectureResolver probingResolver =
        new UniverseArchitectureResolver(nodeUniverseManager);

    Customer customer = ModelFactory.testCustomer();
    ModelFactory.newProvider(customer, CloudType.onprem);
    Universe universe =
        ModelFactory.createUniverse(
            "multi-node-probe", UUID.randomUUID(), customer.getId(), CloudType.onprem);
    universe = ModelFactory.addNodesToUniverse(universe.getUniverseUUID(), 3);
    UniverseDefinitionTaskParams params = universe.getUniverseDetails();
    params.arch = null;
    universe.setUniverseDetails(params);
    universe.update();

    when(nodeUniverseManager.runCommand(any(), any(), anyList()))
        .thenReturn(ShellResponse.create(ShellResponse.ERROR_CODE_GENERIC_ERROR, "probe failed"))
        .thenReturn(
            ShellResponse.create(
                ShellResponse.ERROR_CODE_SUCCESS,
                ShellResponse.RUN_COMMAND_OUTPUT_PREFIX + " aarch64"));

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    Optional<Architecture> arch = probingResolver.resolveFromLiveNode(universe);

    assertTrue(arch.isPresent());
    assertEquals(Architecture.aarch64, arch.get());
    verify(nodeUniverseManager, times(2)).runCommand(any(), any(), anyList());
  }

  @Test
  public void testResolveFromLiveNodeReturnsEmptyWhenNoLiveTserver() {
    Customer customer = ModelFactory.testCustomer();
    Universe universe =
        ModelFactory.createUniverse(
            "no-live-tserver", UUID.randomUUID(), customer.getId(), CloudType.onprem);
    universe = ModelFactory.addNodesToUniverse(universe.getUniverseUUID(), 1);
    UniverseDefinitionTaskParams params = universe.getUniverseDetails();
    params.arch = null;
    for (NodeDetails node : params.nodeDetailsSet) {
      node.state = NodeState.Stopped;
      node.isTserver = true;
    }
    universe.setUniverseDetails(params);
    universe.update();

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    Optional<Architecture> arch = resolver.resolveFromLiveNode(universe);
    assertFalse(arch.isPresent());
  }
}
