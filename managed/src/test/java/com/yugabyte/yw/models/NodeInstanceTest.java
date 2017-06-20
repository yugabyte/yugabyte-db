// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.core.AllOf.allOf;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;

import java.util.List;
import java.util.UUID;

import org.junit.Before;
import org.junit.Test;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;


public class NodeInstanceTest extends FakeDBApplication {
  private Provider provider;
  private Region region;
  private AvailabilityZone zone;

  @Before
  public void setUp() {
    provider = ModelFactory.awsProvider(ModelFactory.testCustomer());
    region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    zone = AvailabilityZone.create(region, "az-1", "AZ 1", "subnet-1");
  }

  private NodeInstance createNode() {
    NodeInstanceFormData formData = new NodeInstanceFormData();
    formData.ip = "fake_ip";
    formData.region = region.code;
    formData.zone = zone.code;
    formData.instanceType = "default_instance_type";
    return NodeInstance.create(zone.uuid, formData);
  }

  @Test
  public void testCreate() {
    NodeInstance node = createNode();
    String defaultNodeName = "";

    assertNotNull(node);
    assertEquals(defaultNodeName, node.getNodeName());
    assertEquals(zone.uuid, node.zoneUuid);

    NodeInstanceFormData details = node.getDetails();
    assertEquals("fake_ip", details.ip);
    assertEquals("default_instance_type", details.instanceType);
    assertEquals(region.code, details.region);
    assertEquals(zone.code, details.zone);
    assertEquals(defaultNodeName, details.nodeName);
  }

  @Test
  public void testListByZone() {
    NodeInstance node = createNode();
    List<NodeInstance> nodes = null;
    // Return all instance types.
    nodes = NodeInstance.listByZone(zone.uuid, null);
    assertEquals(nodes.size(), 1);
    assertEquals(nodes.get(0).getDetailsJson(), node.getDetailsJson());

    // Return by instance type.
    nodes = NodeInstance.listByZone(zone.uuid, "default_instance_type");
    assertEquals(nodes.size(), 1);
    assertEquals(nodes.get(0).getDetailsJson(), node.getDetailsJson());

    // Check invalid instance type.
    nodes = NodeInstance.listByZone(zone.uuid, "fail");
    assertEquals(nodes.size(), 0);

    // Update node to in use and confirm no more fetching.
    node.inUse = true;
    node.save();
    nodes = NodeInstance.listByZone(zone.uuid, null);
    assertEquals(nodes.size(), 0);
  }

  @Test
  public void testDeleteNodeInstanceByProviderWithValidProvider() {
    NodeInstance node = createNode();
    List<NodeInstance> nodesListInitial = NodeInstance.listByZone(zone.uuid, null);
    int response = NodeInstance.deleteByProvider(provider.uuid);
    List<NodeInstance> nodesListFinal = NodeInstance.listByZone(zone.uuid, null);
    assertEquals(nodesListInitial.size(), 1);
    assertEquals(nodesListFinal.size(), 0);
    assertEquals(response, 1);
  }

  @Test
  public void testDeleteNodeInstanceByProviderWithInvalidProvider() {
    NodeInstance node = createNode();
    List<NodeInstance> nodesListInitial = NodeInstance.listByZone(zone.uuid, null);
    UUID invalidProviderUUID = UUID.randomUUID();
    int response = NodeInstance.deleteByProvider(invalidProviderUUID);
    List<NodeInstance> nodesListFinal = NodeInstance.listByZone(zone.uuid, null);
    assertEquals(nodesListInitial.size(), 1);
    assertEquals(nodesListFinal.size(), 1);
    assertEquals(response, 0);
  }

  // TODO: add tests for pickNodes
}
