// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.models.helpers.NodeConfig;
import com.yugabyte.yw.models.helpers.NodeConfig.Type;
import com.yugabyte.yw.models.helpers.TransactionUtil;
import jakarta.persistence.PersistenceException;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import net.logstash.logback.encoder.org.apache.commons.lang3.StringUtils;
import org.apache.commons.compress.utils.Sets;
import org.junit.Before;
import org.junit.Test;

public class NodeInstanceTest extends FakeDBApplication {
  private Provider provider;
  private Region region;
  private AvailabilityZone zone;

  @Before
  public void setUp() {
    provider = ModelFactory.awsProvider(ModelFactory.testCustomer());
    region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    zone = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
  }

  private NodeInstance createNode() {
    return createNode("default_instance_type");
  }

  private NodeInstance createNode(String instanceType) {
    return createNode(instanceType, null);
  }

  private NodeInstance createNode(String instanceType, Set<NodeConfig> nodeConfigs) {
    NodeInstanceFormData.NodeInstanceData nodeData = new NodeInstanceData();
    nodeData.ip = "fake_ip";
    nodeData.region = region.getCode();
    nodeData.zone = zone.getCode();
    nodeData.instanceType = instanceType;
    nodeData.nodeConfigs = nodeConfigs;
    return NodeInstance.create(zone.getUuid(), nodeData);
  }

  @Test
  public void testCreate() {
    NodeInstance node = createNode();
    String defaultNodeName = "";

    assertNotNull(node);
    assertEquals(defaultNodeName, node.getNodeName());
    assertEquals(zone.getUuid(), node.getZoneUuid());

    NodeInstanceFormData.NodeInstanceData details = node.getDetails();
    assertEquals("fake_ip", details.ip);
    assertEquals("default_instance_type", details.instanceType);
    assertEquals(region.getCode(), details.region);
    assertEquals(zone.getCode(), details.zone);
    assertEquals(defaultNodeName, details.nodeName);
  }

  @Test
  public void testListByZone() {
    NodeInstance node = createNode();
    List<NodeInstance> nodes = null;
    // Return all instance types.
    nodes = NodeInstance.listByZone(zone.getUuid(), null);
    assertEquals(nodes.size(), 1);
    assertEquals(nodes.get(0).getDetailsJson(), node.getDetailsJson());

    // Return by instance type.
    nodes = NodeInstance.listByZone(zone.getUuid(), "default_instance_type");
    assertEquals(nodes.size(), 1);
    assertEquals(nodes.get(0).getDetailsJson(), node.getDetailsJson());

    // Check invalid instance type.
    nodes = NodeInstance.listByZone(zone.getUuid(), "fail");
    assertEquals(nodes.size(), 0);

    // Update node to in use and confirm no more fetching.
    node.setState(NodeInstance.State.USED);
    node.save();
    nodes = NodeInstance.listByZone(zone.getUuid(), null);
    assertEquals(nodes.size(), 0);
  }

  @Test
  public void testDeleteNodeInstanceByProviderWithValidProvider() {
    createNode();
    List<NodeInstance> nodesListInitial = NodeInstance.listByZone(zone.getUuid(), null);
    int response = NodeInstance.deleteByProvider(provider.getUuid());
    List<NodeInstance> nodesListFinal = NodeInstance.listByZone(zone.getUuid(), null);
    assertEquals(nodesListInitial.size(), 1);
    assertEquals(nodesListFinal.size(), 0);
    assertEquals(response, 1);
  }

  @Test
  public void testDeleteNodeInstanceByProviderWithInvalidProvider() {
    createNode();
    List<NodeInstance> nodesListInitial = NodeInstance.listByZone(zone.getUuid(), null);
    UUID invalidProviderUUID = UUID.randomUUID();
    int response = NodeInstance.deleteByProvider(invalidProviderUUID);
    List<NodeInstance> nodesListFinal = NodeInstance.listByZone(zone.getUuid(), null);
    assertEquals(nodesListInitial.size(), 1);
    assertEquals(nodesListFinal.size(), 1);
    assertEquals(response, 0);
  }

  @Test
  public void testClearNodeDetails() {
    NodeInstance node = createNode();
    node.setNodeName("yb-universe-1-n1");
    node.setState(NodeInstance.State.USED);
    node.save();
    node.clearNodeDetails();
    assertEquals(NodeInstance.State.FREE, node.getState());
    assertEquals("", node.getNodeName());
    assertEquals(null, node.getUniverseMetadata());
    assertEquals(false, node.isManuallyDecommissioned());
  }

  @Test
  public void testReserveNode() {
    UUID cluserUuid = UUID.randomUUID();
    String universeNodeName = "fake-name";
    NodeInstance node = createNode();
    Map<UUID, Set<String>> azNodeNames =
        ImmutableMap.of(zone.getUuid(), Sets.newHashSet(universeNodeName));
    Map<String, NodeInstance> reservedInstances =
        NodeInstance.reserveNodes(cluserUuid, azNodeNames, node.getInstanceTypeCode());
    assertEquals(1, reservedInstances.size());
    assertTrue(reservedInstances.containsKey(universeNodeName));
    NodeInstance reservedInstance = reservedInstances.get(universeNodeName);
    assertEquals(node.getNodeUuid(), reservedInstance.getNodeUuid());
    assertEquals(NodeInstance.State.FREE, node.getState());
    assertTrue(StringUtils.isBlank(reservedInstance.getNodeName()));
    UUID cluserUuid1 = UUID.randomUUID();
    // No more nodes.
    assertThrows(
        RuntimeException.class,
        () -> NodeInstance.reserveNodes(cluserUuid1, azNodeNames, node.getInstanceTypeCode()));
    NodeInstance.releaseReservedNodes(cluserUuid);
    // Reserve nodes should succeed after the release.
    NodeInstance.reserveNodes(cluserUuid, azNodeNames, node.getInstanceTypeCode());
    assertEquals(1, reservedInstances.size());
    assertTrue(reservedInstances.containsKey(universeNodeName));
    reservedInstance = reservedInstances.get(universeNodeName);
    assertEquals(node.getNodeUuid(), reservedInstance.getNodeUuid());
    assertEquals(NodeInstance.State.FREE, reservedInstance.getState());
    assertTrue(StringUtils.isBlank(reservedInstance.getNodeName()));
  }

  @Test
  public void testCommitReserveNode() {
    UUID cluserUuid = UUID.randomUUID();
    String universeNodeName = "fake-name";
    NodeInstance node = createNode();
    // No nodes reserved yet.
    assertThrows(RuntimeException.class, () -> NodeInstance.commitReservedNodes(cluserUuid));
    Map<UUID, Set<String>> azNodeNames =
        ImmutableMap.of(zone.getUuid(), Sets.newHashSet(universeNodeName));
    NodeInstance.reserveNodes(cluserUuid, azNodeNames, node.getInstanceTypeCode());
    Map<String, NodeInstance> committedInstances = NodeInstance.commitReservedNodes(cluserUuid);
    NodeInstance committedInstance = committedInstances.get(universeNodeName);
    assertEquals(node.getNodeUuid(), committedInstance.getNodeUuid());
    assertEquals(NodeInstance.State.USED, committedInstance.getState());
    assertEquals(universeNodeName, committedInstance.getNodeName());
    UUID cluserUuid1 = UUID.randomUUID();
    // No more nodes.
    assertThrows(
        RuntimeException.class,
        () -> NodeInstance.reserveNodes(cluserUuid1, azNodeNames, node.getInstanceTypeCode()));
    NodeInstance.releaseReservedNodes(cluserUuid);
    // Should still throw as the changes are committed.
    assertThrows(
        RuntimeException.class,
        () -> NodeInstance.reserveNodes(cluserUuid1, azNodeNames, node.getInstanceTypeCode()));
  }

  @Test
  public void testReserveMultipleInstanceTypes() {
    UUID cluserUuid = UUID.randomUUID();
    String universeNodeName = "fake-name";
    String universeNodeName2 = "fake-name2";
    NodeInstance node = createNode();
    NodeInstance node2 = createNode("instance_type_2");
    Map<String, NodeInstance> reservedInstances =
        NodeInstance.reserveNodes(
            cluserUuid,
            Collections.singletonMap(zone.getUuid(), Sets.newHashSet(universeNodeName)),
            node.getInstanceTypeCode());
    assertEquals(1, reservedInstances.size());
    assertTrue(reservedInstances.containsKey(universeNodeName));
    NodeInstance reservedInstance = reservedInstances.get(universeNodeName);
    assertEquals(node.getNodeUuid(), reservedInstance.getNodeUuid());
    reservedInstances =
        NodeInstance.reserveNodes(
            cluserUuid,
            Collections.singletonMap(zone.getUuid(), Sets.newHashSet(universeNodeName2)),
            node2.getInstanceTypeCode(),
            false);
    assertEquals(1, reservedInstances.size());
    assertTrue(reservedInstances.containsKey(universeNodeName2));
    reservedInstance = reservedInstances.get(universeNodeName2);
    assertEquals(node2.getNodeUuid(), reservedInstance.getNodeUuid());
  }

  @Test
  public void testCommitReserveNodeTxnAttempt() {
    UUID cluserUuid = UUID.randomUUID();
    String universeNodeName = "fake-name";
    NodeInstance node = createNode();
    Map<UUID, Set<String>> azNodeNames =
        ImmutableMap.of(zone.getUuid(), Sets.newHashSet(universeNodeName));
    Map<String, NodeInstance> reservedInstances =
        NodeInstance.reserveNodes(cluserUuid, azNodeNames, node.getInstanceTypeCode());
    AtomicInteger count = new AtomicInteger();
    TransactionUtil.doInTxn(
        () -> {
          NodeInstance.commitReservedNodes(cluserUuid);
          if (count.getAndIncrement() == 0) {
            // This is a retryable exception.
            throw new PersistenceException("could not serialize access due to concurrent update");
          }
        },
        TransactionUtil.DEFAULT_RETRY_CONFIG);
    // This does not have any effect.
    NodeInstance.releaseReservedNodes(cluserUuid);
    NodeInstance nodeInstance =
        NodeInstance.getOrBadRequest(reservedInstances.get(universeNodeName).getNodeUuid());
    assertEquals(NodeInstance.State.USED, nodeInstance.getState());
  }

  @Test
  public void testCreateWithNodeConfigs() {
    Set<NodeConfig> nodeConfigs =
        ImmutableSet.of(
            new NodeConfig(Type.HOME_DIR_EXISTS, "true"),
            new NodeConfig(Type.MOUNT_POINTS_VOLUME, "/mnt/d0"));
    NodeInstance node = createNode("default_instance_type", nodeConfigs);
    String defaultNodeName = "";
    assertNotNull(node);
    assertEquals(defaultNodeName, node.getNodeName());
    assertEquals(zone.getUuid(), node.getZoneUuid());
    NodeInstanceFormData.NodeInstanceData details = node.getDetails();
    assertEquals("fake_ip", details.ip);
    assertEquals("default_instance_type", details.instanceType);
    assertEquals(region.getCode(), details.region);
    assertEquals(zone.getCode(), details.zone);
    assertEquals(defaultNodeName, details.nodeName);
    assertNull(details.nodeConfigs);
  }
}
