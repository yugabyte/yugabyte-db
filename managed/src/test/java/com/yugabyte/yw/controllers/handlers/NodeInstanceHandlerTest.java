// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers.handlers;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class NodeInstanceHandlerTest extends FakeDBApplication {

  private static final String NODE_IP = "10.0.0.10";
  private static final String NODE_AGENT_HOME = "/home/yugabyte/node-agent";

  private Customer customer;
  private Provider provider;
  private Region region;
  private AvailabilityZone zone;
  private NodeAgentManager mockNodeAgentManager;
  private NodeInstanceHandler handler;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.onpremProvider(customer);
    provider.getDetails().skipProvisioning = false;
    provider.save();
    region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    zone = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    mockNodeAgentManager = mock(NodeAgentManager.class);
    handler = new NodeInstanceHandler(mockCommissioner, mockNodeAgentManager);
  }

  private NodeInstance createNodeInstance(String ip) {
    NodeInstanceData nodeData = new NodeInstanceData();
    nodeData.ip = ip;
    nodeData.region = region.getCode();
    nodeData.zone = zone.getCode();
    nodeData.instanceType = "fake-instance-type";
    nodeData.sshUser = "centos";
    NodeInstance node = NodeInstance.create(zone.getUuid(), nodeData);
    node.setNodeName("fake-node");
    node.save();
    return node;
  }

  private NodeAgent createNodeAgent(String ip) {
    NodeAgent nodeAgent = new NodeAgent();
    nodeAgent.setIp(ip);
    nodeAgent.setName("fake-node");
    nodeAgent.setPort(9070);
    nodeAgent.setCustomerUuid(customer.getUuid());
    nodeAgent.setOsType(OSType.LINUX);
    nodeAgent.setArchType(ArchType.AMD64);
    nodeAgent.setVersion("2.25.1.0");
    nodeAgent.setHome(NODE_AGENT_HOME);
    nodeAgent.setConfig(new NodeAgent.Config());
    nodeAgent.setState(State.READY);
    nodeAgent.save();
    return nodeAgent;
  }

  @Test
  public void testDeleteInstancePurgesNodeAgentForNonManualOnprem() {
    assertTrue(provider.isNonManualOnprem());
    NodeInstance node = createNodeInstance(NODE_IP);
    UUID nodeUuid = node.getNodeUuid();
    createNodeAgent(NODE_IP);
    handler.deleteInstance(provider, node);
    assertFalse(NodeInstance.maybeGet(nodeUuid).isPresent());
    verify(mockNodeAgentManager, times(1)).purge(argThat(n -> NODE_IP.equals(n.getIp())));
  }

  @Test
  public void testDeleteInstanceSkipsPurgeForManualOnprem() {
    provider.getDetails().skipProvisioning = true;
    provider.save();
    assertTrue(provider.isManualOnprem());
    NodeInstance node = createNodeInstance(NODE_IP);
    UUID nodeUuid = node.getNodeUuid();
    createNodeAgent(NODE_IP);
    handler.deleteInstance(provider, node);
    assertFalse(NodeInstance.maybeGet(nodeUuid).isPresent());
    verify(mockNodeAgentManager, never()).purge(any());
  }
}
