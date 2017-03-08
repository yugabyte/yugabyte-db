// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.junit.Before;
import org.junit.Test;

import java.util.ArrayList;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThat;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.core.AllOf.allOf;
import static org.junit.Assert.assertTrue;

public class NodeDetailsTest {
  private NodeDetails nd;

  @Before
  public void setUp() {
    nd = ApiUtils.getDummyNodeDetails(1, NodeDetails.NodeState.Running);
  }

  @Test
  public void testToString() {
    assertThat(nd.toString(), allOf(notNullValue(),
                                    equalTo("name: host-n1.cloudInfo: az-1.test-region.aws, type: c3-large, " +
                                              "ip: host-n1, isMaster: false, isTserver: true, state: Running")));
  }

  @Test
  public void testIsActive() {
    ArrayList<NodeDetails.NodeState> activeStates = new ArrayList<>();
    activeStates.add(NodeDetails.NodeState.ToBeAdded);
    activeStates.add(NodeDetails.NodeState.Provisioned);
    activeStates.add(NodeDetails.NodeState.SoftwareInstalled);
    activeStates.add(NodeDetails.NodeState.UpgradeSoftware);
    activeStates.add(NodeDetails.NodeState.UpdateGFlags);
    activeStates.add(NodeDetails.NodeState.Running);
    activeStates.forEach((state) -> {
      nd.state = state;
      assertTrue(nd.isActive());
    });

    ArrayList<NodeDetails.NodeState> inactiveStates = new ArrayList<>();
    inactiveStates.add(NodeDetails.NodeState.ToBeDecommissioned);
    inactiveStates.add(NodeDetails.NodeState.BeingDecommissioned);
    inactiveStates.add(NodeDetails.NodeState.Destroyed);
    inactiveStates.forEach((state) -> {
      nd.state = state;
      assertFalse(nd.isActive());
    });
    assertFalse(nd.isActive());
  }
}
