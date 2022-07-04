// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertUnauthorized;
import static org.junit.Assert.assertNotNull;
import static org.mockito.Mockito.mock;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.Lists;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.controllers.handlers.NodeAgentHandler;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.NodeConfiguration;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.internal.util.collections.Sets;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class NodeAgentControllerTest extends FakeDBApplication {
  private Config appConfig;
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
    appConfig = mock(Config.class);
    nodeAgentHandler = new NodeAgentHandler(appConfig);
  }

  private Result registerNodeAgent(JsonNode bodyJson) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        "POST",
        "/api/customers/" + customer.uuid + "/node_agents",
        user.createAuthToken(),
        bodyJson);
  }

  private Result getNodeAgent(UUID nodeAgentUuid, String jwt) {
    return FakeApiHelper.doRequestWithJWT(
        "GET", "/api/customers/" + customer.uuid + "/node_agents/" + nodeAgentUuid, jwt);
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
  public void testNodeAgentWorkflow() {
    String ip = "10.20.30.40";
    String version = "2.12.0";
    JsonNode payload =
        Json.parse(
            "{\"customerUuid\" : \""
                + customer.uuid
                + "\", \"name\": \"test\", \"ip\": \""
                + ip
                + "\", \"version\": \""
                + version
                + "\", \"config\": {}}");
    Result result = registerNodeAgent(payload);
    assertOk(result);
    NodeAgent nodeAgent = Json.fromJson(Json.parse(contentAsString(result)), NodeAgent.class);
    assertNotNull(nodeAgent.uuid);
    String jwt = nodeAgentHandler.getClientToken(nodeAgent.uuid, user.uuid);
    result = assertPlatformException(() -> registerNodeAgent(payload));
    assertBadRequest(result, "Node agent is already registered");
    result = getNodeAgent(nodeAgent.uuid, jwt);
    assertOk(result);
    NodeInstanceData testNode = new NodeInstanceData();
    testNode.ip = "10.20.30.40";
    testNode.region = region.code;
    testNode.zone = zone.code;
    testNode.instanceType = "fake_instance_type";
    testNode.sshUser = "ssh-user";
    result = assertPlatformException(() -> createNode(zone.uuid, testNode, jwt));
    assertBadRequest(result, "Invalid configurations");
    NodeConfiguration nodeConfig = new NodeConfiguration();
    testNode.nodeConfigurations = Sets.newSet(nodeConfig);
    nodeConfig.setType(NodeConfiguration.Type.NTP_SERVICE_STATUS);
    nodeConfig.setValue("stopped");
    result = assertPlatformException(() -> createNode(zone.uuid, testNode, jwt));
    assertBadRequest(result, "Invalid configurations");
    // Accepted value for NTP_SERVICE_STATUS is "running".
    nodeConfig.setValue("running");
    result = createNode(zone.uuid, testNode, jwt);
    assertOk(result);
    result = unregisterNodeAgent(nodeAgent.uuid, jwt);
    assertOk(result);
    result = assertPlatformException(() -> getNodeAgent(nodeAgent.uuid, jwt));
    assertUnauthorized(result, "Invalid token");
  }
}
