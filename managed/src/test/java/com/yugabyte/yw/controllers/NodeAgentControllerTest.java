// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertUnauthorized;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static play.test.Helpers.contentAsString;

import com.google.common.collect.Lists;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.controllers.handlers.NodeAgentHandler;
import com.yugabyte.yw.forms.NodeAgentForm;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.NodeConfiguration;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.internal.util.collections.Sets;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class NodeAgentControllerTest extends FakeDBApplication {
  @Mock private Config mockAppConfig;
  @Mock private ConfigHelper mockConfigHelper;
  @Mock private PlatformScheduler mockPlatformScheduler;
  private NodeAgentHandler nodeAgentHandler;
  private Customer customer;
  private Provider provider;
  private Region region;
  private AvailabilityZone zone;
  private Users user;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.onpremProvider(customer);
    region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    zone = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    user = ModelFactory.testUser(customer);
    nodeAgentHandler = new NodeAgentHandler(mockAppConfig, mockConfigHelper, mockPlatformScheduler);
  }

  private Result registerNodeAgent(NodeAgentForm formData) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        "POST",
        "/api/customers/" + customer.uuid + "/node_agents",
        user.createAuthToken(),
        Json.toJson(formData));
  }

  private Result getNodeAgent(UUID nodeAgentUuid, String jwt) {
    return FakeApiHelper.doRequestWithJWT(
        "GET", "/api/customers/" + customer.uuid + "/node_agents/" + nodeAgentUuid, jwt);
  }

  private Result pingNodeAgent(UUID nodeAgentUuid) {
    return FakeApiHelper.doGetRequestNoAuth(
        "/api/customers/" + customer.uuid + "/node_agents/" + nodeAgentUuid + "/state");
  }

  private Result updateNodeState(UUID nodeAgentUuid, NodeAgentForm formData, String jwt) {
    String uri = "/api/customers/" + customer.uuid + "/node_agents/" + nodeAgentUuid + "/state";
    return FakeApiHelper.doRequestWithJWTAndBody("PUT", uri, jwt, Json.toJson(formData));
  }

  private Result updateNode(UUID nodeAgentUuid, String jwt) {
    String uri = "/api/customers/" + customer.uuid + "/node_agents/" + nodeAgentUuid;
    return FakeApiHelper.doRequestWithJWTAndBody("PUT", uri, jwt, Json.newObject());
  }

  private Result createNode(UUID zoneUuid, NodeInstanceData details, String jwt) {
    String uri = "/api/customers/" + customer.uuid + "/zones/" + zoneUuid + "/nodes";
    NodeInstanceFormData formData = new NodeInstanceFormData();
    formData.nodes = Lists.newArrayList(details);
    return FakeApiHelper.doRequestWithJWTAndBody("POST", uri, jwt, Json.toJson(formData));
  }

  private Result unregisterNodeAgent(UUID nodeAgentUuid, String jwt) {
    return FakeApiHelper.doRequestWithJWT(
        "DELETE", "/api/customers/" + customer.uuid + "/node_agents/" + nodeAgentUuid, jwt);
  }

  @Test
  public void testNodeAgentRegistrationWorkflow() {
    NodeAgentForm formData = new NodeAgentForm();
    formData.name = "test";
    formData.ip = "10.20.30.40";
    formData.version = "2.12.0";
    // Register the node agent.
    Result result = registerNodeAgent(formData);
    assertOk(result);
    NodeAgent nodeAgent = Json.fromJson(Json.parse(contentAsString(result)), NodeAgent.class);
    assertNotNull(nodeAgent.uuid);
    UUID nodeAgentUuid = nodeAgent.uuid;

    // Ping for node state.
    result = pingNodeAgent(nodeAgentUuid);
    assertOk(result);
    State state = Json.fromJson(Json.parse(contentAsString(result)), State.class);
    assertEquals(State.REGISTERING, state);
    String jwt = nodeAgentHandler.getClientToken(nodeAgentUuid, user.uuid);
    result = assertPlatformException(() -> registerNodeAgent(formData));
    assertBadRequest(result, "Node agent is already registered");
    result = getNodeAgent(nodeAgentUuid, jwt);
    assertOk(result);
    // Report live to the server.
    formData.state = State.LIVE;
    result = updateNodeState(nodeAgentUuid, formData, jwt);
    assertOk(result);
    // Ping for node state.
    result = pingNodeAgent(nodeAgentUuid);
    assertOk(result);
    state = Json.fromJson(Json.parse(contentAsString(result)), State.class);
    assertEquals(State.LIVE, state);
    NodeInstanceData testNode = new NodeInstanceData();
    testNode.ip = "10.20.30.40";
    testNode.region = region.code;
    testNode.zone = zone.code;
    testNode.instanceType = "fake_instance_type";
    testNode.sshUser = "ssh-user";
    // Missing node configurations in the payload.
    result = assertPlatformException(() -> createNode(zone.uuid, testNode, jwt));
    assertBadRequest(result, "Invalid configurations");
    NodeConfiguration nodeConfig = new NodeConfiguration();
    testNode.nodeConfigurations = Sets.newSet(nodeConfig);
    nodeConfig.setType(NodeConfiguration.Type.NTP_SERVICE_STATUS);
    // Set an unaccepted value.
    result = assertPlatformException(() -> createNode(zone.uuid, testNode, jwt));
    // Missing preflight checks should return an error
    assertBadRequest(result, "Invalid configurations");
    // Accepted value for NTP_SERVICE_STATUS is "running".
    testNode.nodeConfigurations = getTestNodeConfigurationsSet();
    result = createNode(zone.uuid, testNode, jwt);
    assertOk(result);
    result = unregisterNodeAgent(nodeAgentUuid, jwt);
    assertOk(result);
    result = assertPlatformException(() -> getNodeAgent(nodeAgentUuid, jwt));
    assertUnauthorized(result, "Invalid token");
  }

  @Test
  public void testNodeAgentUpgradeWorkflow() {
    NodeAgentForm formData = new NodeAgentForm();
    formData.name = "test";
    formData.ip = "10.20.30.40";
    formData.version = "2.12.0";
    Result result = registerNodeAgent(formData);
    assertOk(result);
    NodeAgent nodeAgent = Json.fromJson(Json.parse(contentAsString(result)), NodeAgent.class);
    assertNotNull(nodeAgent.uuid);
    UUID nodeAgentUuid = nodeAgent.uuid;
    // Ping for node state.
    result = pingNodeAgent(nodeAgentUuid);
    assertOk(result);
    State state = Json.fromJson(Json.parse(contentAsString(result)), State.class);
    assertEquals(State.REGISTERING, state);
    String jwt = nodeAgentHandler.getClientToken(nodeAgentUuid, user.uuid);
    result = assertPlatformException(() -> registerNodeAgent(formData));
    assertBadRequest(result, "Node agent is already registered");
    result = getNodeAgent(nodeAgentUuid, jwt);
    assertOk(result);
    // Report live to the server.
    formData.state = State.LIVE;
    result = updateNodeState(nodeAgentUuid, formData, jwt);
    assertOk(result);
    // Ping for node state.
    result = pingNodeAgent(nodeAgentUuid);
    assertOk(result);
    state = Json.fromJson(Json.parse(contentAsString(result)), State.class);
    assertEquals(State.LIVE, state);
    // Initiate upgrade in the server.
    nodeAgent = NodeAgent.getOrBadRequest(customer.uuid, nodeAgentUuid);
    nodeAgent.state = State.UPGRADE;
    nodeAgent.save();
    assertOk(result);
    // Ping for node state.
    result = pingNodeAgent(nodeAgentUuid);
    assertOk(result);
    state = Json.fromJson(Json.parse(contentAsString(result)), State.class);
    assertEquals(State.UPGRADE, state);
    // Report upgrading to the server.
    formData.state = State.UPGRADING;
    result = updateNodeState(nodeAgentUuid, formData, jwt);
    assertOk(result);
    // Ping for node state.
    result = pingNodeAgent(nodeAgentUuid);
    assertOk(result);
    state = Json.fromJson(Json.parse(contentAsString(result)), State.class);
    assertEquals(State.UPGRADING, state);
    // Reach out to the server to refresh certs.
    result = updateNode(nodeAgentUuid, jwt);
    assertOk(result);
    // Complete upgrading.
    formData.state = State.UPGRADED;
    result = updateNodeState(nodeAgentUuid, formData, jwt);
    assertOk(result);
    // Ping for node state.
    result = pingNodeAgent(nodeAgentUuid);
    assertOk(result);
    state = Json.fromJson(Json.parse(contentAsString(result)), State.class);
    assertEquals(State.UPGRADED, state);
    // Restart the node agent and report live to the server.
    formData.state = State.LIVE;
    result = updateNodeState(nodeAgentUuid, formData, jwt);
    assertOk(result);
    // Ping for node state.
    result = pingNodeAgent(nodeAgentUuid);
    assertOk(result);
    state = Json.fromJson(Json.parse(contentAsString(result)), State.class);
    assertEquals(State.LIVE, state);
    NodeInstanceData testNode = new NodeInstanceData();
    testNode.ip = "10.20.30.40";
    testNode.region = region.code;
    testNode.zone = zone.code;
    testNode.instanceType = "fake_instance_type";
    testNode.sshUser = "ssh-user";
    // JWT signed with the old key is invalid after the upgrade.
    result = assertPlatformException(() -> createNode(zone.uuid, testNode, jwt));
    assertUnauthorized(result, "Invalid token");
    // Get a new JWT after the update.
    String updatedJwt = nodeAgentHandler.getClientToken(nodeAgentUuid, user.uuid);
    NodeConfiguration nodeConfig = new NodeConfiguration();
    testNode.nodeConfigurations = Sets.newSet(nodeConfig);
    nodeConfig.setType(NodeConfiguration.Type.NTP_SERVICE_STATUS);
    // Missing preflight checks should return an error
    result = assertPlatformException(() -> createNode(zone.uuid, testNode, updatedJwt));
    assertBadRequest(result, "Invalid configurations");
    // Accepted value for NTP_SERVICE_STATUS is "running".
    testNode.nodeConfigurations = getTestNodeConfigurationsSet();
    result = createNode(zone.uuid, testNode, updatedJwt);
    assertOk(result);
    result = unregisterNodeAgent(nodeAgentUuid, updatedJwt);
    assertOk(result);
    result = assertPlatformException(() -> getNodeAgent(nodeAgentUuid, updatedJwt));
    assertUnauthorized(result, "Invalid token");
  }

  public Set<NodeConfiguration> getTestNodeConfigurationsSet() {
    Set<NodeConfiguration> nodeConfigurations = Sets.newSet();
    NodeConfiguration.TypeGroup.ALL
        .getRequiredConfigTypes()
        .forEach(t -> nodeConfigurations.add(new NodeConfiguration(t, "")));
    return nodeConfigurations;
  }
}
