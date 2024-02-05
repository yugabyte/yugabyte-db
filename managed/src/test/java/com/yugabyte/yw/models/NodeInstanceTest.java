// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import autovalue.shaded.com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
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
    NodeInstanceFormData.NodeInstanceData nodeData = new NodeInstanceFormData.NodeInstanceData();
    nodeData.ip = "fake_ip";
    nodeData.region = region.getCode();
    nodeData.zone = zone.getCode();
    nodeData.instanceType = "default_instance_type";
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
    node.setInUse(true);
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
    node.setInUse(true);
    node.save();
    node.clearNodeDetails();
    assertEquals(node.isInUse(), false);
    assertEquals(node.getNodeName(), "");
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
    assertEquals(false, node.isInUse());
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
    assertEquals(false, reservedInstance.isInUse());
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
    assertEquals(true, committedInstance.isInUse());
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
}
