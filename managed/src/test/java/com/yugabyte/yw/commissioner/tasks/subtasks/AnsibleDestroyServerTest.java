// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AnsibleDestroyServerTest extends CommissionerBaseTest {

  private static final String NODE_AGENT_HOME = "/home/yugabyte/node-agent";
  private static final List<String> STOP_NODE_AGENT_CMD =
      ImmutableList.of(
          "/bin/bash",
          "-c",
          "sudo systemctl disable --now yb-node-agent.service && sudo rm -rf"
              + " /etc/systemd/system/yb-node-agent.service && sudo systemctl daemon-reload && sudo"
              + " rm -rf '"
              + NODE_AGENT_HOME
              + "'");

  private Provider provider;
  private Universe universe;
  private NodeDetails node;
  private Region region;
  private AvailabilityZone az;

  @Before
  public void setUp() {
    provider = onPremProvider;
    provider.getDetails().skipProvisioning = false;
    provider.getDetails().sshUser = "centos";
    provider.save();
    region = Region.create(provider, "r-" + System.nanoTime() % 1000000, "Region 1", "yb-image-1");
    az = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    universe = createOnPremUniverse();
    node = universe.getNodes().iterator().next();
    when(mockNodeAgentClient.getAndUpgradeOrThrow(any())).thenReturn(mock(NodeAgent.class));
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(ShellResponse.create(0, ""));
  }

  private Universe createOnPremUniverse() {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 1;
    userIntent.provider = provider.getUuid().toString();
    userIntent.providerType = CloudType.onprem;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.replicationFactor = 1;
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    Universe u =
        ModelFactory.createUniverse(
            "destroy-na-" + (System.nanoTime() % 1000000),
            defaultCustomer.getId(),
            CloudType.onprem);
    u =
        Universe.saveDetails(
            u.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));
    return Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> {
          for (NodeDetails n : univ.getNodes()) {
            n.azUuid = az.getUuid();
            n.cloudInfo.cloud = CloudType.onprem.toString();
            n.placementUuid = univ.getUniverseDetails().getPrimaryCluster().uuid;
          }
        });
  }

  private NodeAgent createNodeAgent(String ip) {
    NodeAgent nodeAgent = new NodeAgent();
    nodeAgent.setIp(ip);
    nodeAgent.setName(node.nodeName);
    nodeAgent.setPort(9070);
    nodeAgent.setCustomerUuid(defaultCustomer.getUuid());
    nodeAgent.setOsType(OSType.LINUX);
    nodeAgent.setArchType(ArchType.AMD64);
    nodeAgent.setVersion("2.25.1.0");
    nodeAgent.setHome(NODE_AGENT_HOME);
    nodeAgent.setConfig(new NodeAgent.Config());
    nodeAgent.setState(State.READY);
    nodeAgent.save();
    return nodeAgent;
  }

  private AnsibleDestroyServer.Params createTaskParams() {
    AnsibleDestroyServer.Params params = new AnsibleDestroyServer.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = node.nodeName;
    params.nodeUuid = node.nodeUuid;
    params.azUuid = node.azUuid;
    params.placementUuid = node.placementUuid;
    params.deleteNode = false;
    params.skipUpdateNodeState = true;
    return params;
  }

  private void runDestroy() {
    AnsibleDestroyServer task = AbstractTaskBase.createTask(AnsibleDestroyServer.class);
    task.initialize(createTaskParams());
    task.run();
  }

  @Test
  public void testMaybeUninstallNodeAgentNonManualOnprem() {
    assertTrue(provider.isNonManualOnprem());
    createNodeAgent(node.cloudInfo.private_ip);

    runDestroy();

    verify(mockNodeAgentClient, times(1)).getAndUpgradeOrThrow(eq(node.cloudInfo.private_ip));
    verify(mockNodeUniverseManager, times(1))
        .runCommand(
            any(NodeDetails.class),
            any(Universe.class),
            eq(STOP_NODE_AGENT_CMD),
            argThat(ShellProcessContext::isUseSshConnectionOnly));
    verify(mockNodeAgentManager, times(1))
        .purge(argThat(n -> node.cloudInfo.private_ip.equals(n.getIp())));
  }

  @Test
  public void testMaybeUninstallNodeAgentSshUserOverride() {
    createNodeAgent(node.cloudInfo.private_ip);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(), u -> u.getNode(node.nodeName).sshUserOverride = "ec2-user");
    node = universe.getNode(node.nodeName);

    runDestroy();

    verify(mockNodeUniverseManager, times(1))
        .runCommand(
            any(NodeDetails.class),
            any(Universe.class),
            eq(STOP_NODE_AGENT_CMD),
            argThat(ctx -> ctx.isUseSshConnectionOnly() && "ec2-user".equals(ctx.getSshUser())));
  }

  @Test
  public void testMaybeUninstallNodeAgentSkippedForManualOnprem() {
    provider.getDetails().skipProvisioning = true;
    provider.save();
    createNodeAgent(node.cloudInfo.private_ip);

    runDestroy();

    verify(mockNodeUniverseManager, never())
        .runCommand(any(), any(), eq(STOP_NODE_AGENT_CMD), any());
    verify(mockNodeAgentManager, never()).purge(any());
  }

  @Test
  public void testMaybeUninstallNodeAgentSkippedWhenNoNodeAgent() {
    runDestroy();

    verify(mockNodeUniverseManager, never())
        .runCommand(any(), any(), eq(STOP_NODE_AGENT_CMD), any());
    verify(mockNodeAgentManager, never()).purge(any());
  }

  @Test
  public void testMaybeUninstallNodeAgentSkippedForCloudProvider() {
    Region awsRegion =
        Region.create(
            defaultProvider, "ar-" + (System.nanoTime() % 1000000), "Region 1", "yb-image-1");
    AvailabilityZone awsAz =
        AvailabilityZone.createOrThrow(awsRegion, "aws-az-1", "AZ 1", "subnet-1");
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 1;
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.providerType = CloudType.aws;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.replicationFactor = 1;
    userIntent.regionList = ImmutableList.of(awsRegion.getUuid());
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    universe =
        Universe.saveDetails(
            ModelFactory.createUniverse(
                    "aws-destroy-na-" + (System.nanoTime() % 1000000), defaultCustomer.getId())
                .getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              for (NodeDetails n : u.getNodes()) {
                n.azUuid = awsAz.getUuid();
              }
            });
    node = universe.getNodes().iterator().next();
    createNodeAgent(node.cloudInfo.private_ip);

    AnsibleDestroyServer task = AbstractTaskBase.createTask(AnsibleDestroyServer.class);
    task.initialize(createTaskParams());
    task.run();

    verify(mockNodeManager, times(1)).nodeCommand(eq(NodeManager.NodeCommandType.Destroy), any());
    verify(mockNodeUniverseManager, never())
        .runCommand(any(), any(), eq(STOP_NODE_AGENT_CMD), any());
  }

  @Test
  public void testMaybeUninstallNodeAgentIgnoresSshFailure() {
    createNodeAgent(node.cloudInfo.private_ip);
    doThrow(new RuntimeException("ssh failed"))
        .when(mockNodeUniverseManager)
        .runCommand(any(), any(), eq(STOP_NODE_AGENT_CMD), any());

    runDestroy();

    verify(mockNodeUniverseManager, times(1))
        .runCommand(any(), any(), eq(STOP_NODE_AGENT_CMD), any());
    // Best-effort stop must not block purge.
    verify(mockNodeAgentManager, times(1))
        .purge(argThat(n -> node.cloudInfo.private_ip.equals(n.getIp())));
  }
}
