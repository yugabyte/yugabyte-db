// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import java.util.List;
import java.util.UUID;
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
    NodeInstance node = createNode();
    List<NodeInstance> nodesListInitial = NodeInstance.listByZone(zone.getUuid(), null);
    int response = NodeInstance.deleteByProvider(provider.getUuid());
    List<NodeInstance> nodesListFinal = NodeInstance.listByZone(zone.getUuid(), null);
    assertEquals(nodesListInitial.size(), 1);
    assertEquals(nodesListFinal.size(), 0);
    assertEquals(response, 1);
  }

  @Test
  public void testDeleteNodeInstanceByProviderWithInvalidProvider() {
    NodeInstance node = createNode();
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

  // TODO: add tests for pickNodes
}
