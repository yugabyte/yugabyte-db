// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UpgradeNodeAgent.Params;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.util.List;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UpgradeNodeAgentTest extends FakeDBApplication {

  private static final String STOPPED_NODE_NAME = "host-n2";

  private Customer customer;
  private Universe defaultUniverse;
  private UpgradeNodeAgent upgradeNodeAgent;
  private String certificatePath;

  @Before
  public void setUp() throws IOException, NoSuchAlgorithmException {
    customer = ModelFactory.testCustomer();
    Provider provider = ModelFactory.awsProvider(customer);
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
    // Set the node state to Stopped to test targeted upgrades and Live-only filtering.
    setNodeState(STOPPED_NODE_NAME, NodeState.Stopped);
    for (NodeDetails node : defaultUniverse.getNodes()) {
      createNodeAgent(node);
    }
    certificatePath = createTempFile("upgrade_node_agent_test", "ca.crt", "test-cert-data");
    upgradeNodeAgent = new UpgradeNodeAgent(app.injector().instanceOf(BaseTaskDependencies.class));
  }

  private void setNodeState(String nodeName, NodeState nodeState) {
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> universe.getNode(nodeName).state = nodeState);
  }

  private NodeAgent createNodeAgent(NodeDetails node) {
    NodeAgent nodeAgent = new NodeAgent();
    nodeAgent.setIp(node.cloudInfo.private_ip);
    nodeAgent.setName(node.nodeName);
    nodeAgent.setPort(9070);
    nodeAgent.setCustomerUuid(customer.getUuid());
    nodeAgent.setOsType(OSType.LINUX);
    nodeAgent.setArchType(ArchType.AMD64);
    nodeAgent.setVersion("2024.2.4.0");
    nodeAgent.setHome("/home/yugabyte/node-agent");
    nodeAgent.setConfig(new NodeAgent.Config());
    nodeAgent.setState(State.READY);
    nodeAgent.save();
    return nodeAgent;
  }

  private Params createTaskParams() {
    Params params = new Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.clusters = defaultUniverse.getUniverseDetails().clusters;
    return params;
  }

  private List<NodeDetails> getEligibleNodes(Params params) {
    upgradeNodeAgent.initialize(params);
    return upgradeNodeAgent.getEligibleNodesForUpgrade(defaultUniverse);
  }

  private CertificateInfo createCert(CertConfigType certType)
      throws IOException, NoSuchAlgorithmException {
    return ModelFactory.createCertificateInfo(customer.getUuid(), certificatePath, certType);
  }

  @Test
  public void testValidateParamsSelfSignedSucceeds() throws Exception {
    Params params = createTaskParams();
    params.certificateUuid = createCert(CertConfigType.SelfSigned).getUuid();
    upgradeNodeAgent.initialize(params);
    upgradeNodeAgent.validateParams(true /* isFirstTry */);
  }

  @Test
  public void testValidateParamsCustomCertHostPathSucceeds() throws Exception {
    Params params = createTaskParams();
    params.certificateUuid = createCert(CertConfigType.CustomCertHostPath).getUuid();
    upgradeNodeAgent.initialize(params);
    upgradeNodeAgent.validateParams(true /* isFirstTry */);
  }

  @Test
  public void testValidateParamsUnsupportedCertTypeFails() throws Exception {
    Params params = createTaskParams();
    params.certificateUuid = createCert(CertConfigType.HashicorpVault).getUuid();
    upgradeNodeAgent.initialize(params);
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> upgradeNodeAgent.validateParams(true /* isFirstTry */));
    assertTrue(exception.getMessage().contains("certificate is supported for node agent upgrade"));
  }

  @Test
  public void testValidateParamsWithoutCertificateSucceeds() {
    upgradeNodeAgent.initialize(createTaskParams());
    upgradeNodeAgent.validateParams(true /* isFirstTry */);
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
    upgradeNodeAgent.initialize(createTaskParams());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> upgradeNodeAgent.getEligibleNodesForUpgrade(defaultUniverse));
    assertTrue(
        exception.getMessage().contains("Some nodes are not eligible for upgrading node agent"));
  }

  @Test
  public void testAllNodesWithOnlyLiveNodesSuccess() {
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

  @Test
  public void testLiveNodeWithoutNodeAgentFailsValidation() {
    for (NodeDetails node : defaultUniverse.getNodes()) {
      setNodeState(node.getNodeName(), NodeState.Live);
    }
    NodeDetails nodeWithoutAgent = defaultUniverse.getNodes().iterator().next();
    NodeAgent.getAll(customer.getUuid()).stream()
        .filter(na -> na.getIp().equals(nodeWithoutAgent.cloudInfo.private_ip))
        .forEach(NodeAgent::delete);
    upgradeNodeAgent.initialize(createTaskParams());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> upgradeNodeAgent.getEligibleNodesForUpgrade(defaultUniverse));
    assertTrue(
        exception.getMessage().contains("Some nodes are not eligible for upgrading node agent"));
  }
}
