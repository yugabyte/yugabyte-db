// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.ReinstallNodeAgent.Params;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.List;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ReinstallNodeAgentTest extends FakeDBApplication {

  private static final String STOPPED_NODE_NAME = "host-n2";

  private Customer customer;
  private Provider provider;
  private Universe defaultUniverse;
  private ReinstallNodeAgent reinstallNodeAgent;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.awsProvider(customer);
    Region region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.provider = provider.getUuid().toString();
    userIntent.providerType = Common.CloudType.aws;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.replicationFactor = 3;
    userIntent.regionList = ImmutableList.of(region.getUuid());
    defaultUniverse = createUniverse(customer.getId());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    // Set the node state to Stopped to test the targeted reinstall.
    setNodeState(STOPPED_NODE_NAME, NodeState.Stopped);
    reinstallNodeAgent =
        new ReinstallNodeAgent(app.injector().instanceOf(BaseTaskDependencies.class));
  }

  private void setNodeState(String nodeName, NodeState nodeState) {
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> universe.getNode(nodeName).state = nodeState);
  }

  private Params createTaskParams() {
    Params params = new Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.clusters = defaultUniverse.getUniverseDetails().clusters;
    return params;
  }

  private List<NodeDetails> getEligibleNodes(Params params) {
    reinstallNodeAgent.initialize(params);
    return reinstallNodeAgent.getEligibleNodesForReinstall(defaultUniverse);
  }

  @Test
  public void testTargetedNonLiveNodeSuccess() {
    Params params = createTaskParams();
    params.nodeNames = ImmutableSet.of(STOPPED_NODE_NAME);
    List<NodeDetails> eligibleNodes = getEligibleNodes(params);
    assertEquals(1, eligibleNodes.size());
    assertEquals(STOPPED_NODE_NAME, eligibleNodes.get(0).getNodeName());
    assertEquals(NodeState.Stopped, eligibleNodes.get(0).state);
  }

  @Test
  public void testAllNodesWithNonLiveNodeFailsValidation() {
    reinstallNodeAgent.initialize(createTaskParams());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> reinstallNodeAgent.getEligibleNodesForReinstall(defaultUniverse));
    assertTrue(
        exception.getMessage().contains("Some nodes are not eligible for installing node agent"));
  }

  @Test
  public void testAllNodesWithOnlyLiveNodesSuccess() {
    // Make all nodes live.
    for (NodeDetails node : defaultUniverse.getNodes()) {
      setNodeState(node.getNodeName(), NodeState.Live);
    }
    List<NodeDetails> eligibleNodes = getEligibleNodes(createTaskParams());
    assertEquals(defaultUniverse.getNodes().size(), eligibleNodes.size());
    assertEquals(
        defaultUniverse.getNodes().stream()
            .map(NodeDetails::getNodeName)
            .collect(Collectors.toSet()),
        eligibleNodes.stream().map(NodeDetails::getNodeName).collect(Collectors.toSet()));
  }
}
