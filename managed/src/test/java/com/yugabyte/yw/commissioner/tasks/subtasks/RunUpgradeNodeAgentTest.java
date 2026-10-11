// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.RunUpgradeNodeAgent.Params;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class RunUpgradeNodeAgentTest extends FakeDBApplication {

  private Customer customer;
  private NodeAgent nodeAgent;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    nodeAgent = new NodeAgent();
    nodeAgent.setIp("10.20.30.40");
    nodeAgent.setName("host-n1");
    nodeAgent.setPort(9070);
    nodeAgent.setCustomerUuid(customer.getUuid());
    nodeAgent.setOsType(OSType.LINUX);
    nodeAgent.setArchType(ArchType.AMD64);
    nodeAgent.setVersion("2024.2.4.0");
    nodeAgent.setHome("/home/yugabyte/node-agent");
    nodeAgent.setConfig(new NodeAgent.Config());
    nodeAgent.setState(State.UPGRADED);
    nodeAgent.save();
  }

  @Test
  public void testUpgradeRefusedInUpgradedState() {
    Params params = new Params();
    params.nodeIp = nodeAgent.getIp();
    params.certsOnly = true;
    RunUpgradeNodeAgent task = AbstractTaskBase.createTask(RunUpgradeNodeAgent.class);
    task.initialize(params);

    IllegalStateException exception = assertThrows(IllegalStateException.class, task::run);
    assertTrue(exception.getMessage().contains("Upgrade is not allowed in UPGRADED state"));
  }
}
