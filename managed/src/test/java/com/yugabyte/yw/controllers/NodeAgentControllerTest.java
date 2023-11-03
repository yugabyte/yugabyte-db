// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertUnauthorized;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Lists;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.controllers.handlers.NodeAgentHandler;
import com.yugabyte.yw.forms.NodeAgentForm;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.NodeConfig;
import java.time.Instant;
import java.util.Date;
import java.util.Set;
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
  private NodeAgentHandler nodeAgentHandler;
  private NodeAgentManager nodeAgentManager;
  private SettableRuntimeConfigFactory runtimeConfigFactory;
  private ConfigHelper configHelper;
  private Customer customer;
  private Provider provider;
  private Region region;
  private AvailabilityZone zone;
  private Users user;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.onpremProvider(customer);
    nodeAgentHandler = app.injector().instanceOf(NodeAgentHandler.class);
    nodeAgentManager = app.injector().instanceOf(NodeAgentManager.class);
    runtimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
    configHelper = app.injector().instanceOf(ConfigHelper.class);
    nodeAgentHandler.enableConnectionValidation(false);
    RuntimeConfig<Provider> providerConfig = runtimeConfigFactory.forProvider(provider);
    String nodeAgentConfig = "yb.node_agent.preflight_checks.";
    providerConfig.setValue(nodeAgentConfig + "min_python_version", "2.7");
    providerConfig.setValue(nodeAgentConfig + "min_tmp_dir_space_mb", "100");
    providerConfig.setValue(nodeAgentConfig + "user", "fakeUser");
    providerConfig.setValue(nodeAgentConfig + "user_group", "fakeGroup");
    providerConfig.setValue(nodeAgentConfig + "min_prometheus_space_mb", "100");
    providerConfig.setValue(nodeAgentConfig + "min_home_dir_space_mb", "100");
    region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    zone = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    user = ModelFactory.testUser(customer);
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.publicKey = "/path/to/public.key";
    keyInfo.privateKey = "/path/to/private.key";
    keyInfo.vaultFile = "/path/to/vault_file";
    keyInfo.vaultPasswordFile = "/path/to/vault_password";
    keyInfo.sshPort = 22;
    AccessKey.create(provider.getUuid(), "access-code1", keyInfo);
    InstanceType.upsert(
        provider.getUuid(),
        "c5.xlarge",
        1 /* cores */,
        2.0 /* mem in GB */,
        new InstanceTypeDetails());
    ShellResponse response = ShellResponse.create(0, "{}");
    when(mockShellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
        .thenReturn(response);
    configHelper.loadConfigToDB(ConfigType.SoftwareVersion, ImmutableMap.of("version", "2.16.0.0"));
  }

  private Result registerNodeAgent(NodeAgentForm formData) {
    return doRequestWithAuthTokenAndBody(
        "POST",
        "/api/customers/" + customer.getUuid() + "/node_agents",
        user.createAuthToken(),
        Json.toJson(formData));
  }

  private Result getNodeAgent(UUID nodeAgentUuid, String jwt) {
    return doRequestWithJWT(
        "GET", "/api/customers/" + customer.getUuid() + "/node_agents/" + nodeAgentUuid, jwt);
  }

  private Result listNodeAgents() {
    return doRequestWithAuthToken(
        "GET", "/api/customers/" + customer.getUuid() + "/node_agents", user.createAuthToken());
  }

  private Result updateNodeState(UUID nodeAgentUuid, NodeAgentForm formData, String jwt) {
    String uri =
        "/api/customers/" + customer.getUuid() + "/node_agents/" + nodeAgentUuid + "/state";
    return doRequestWithJWTAndBody("PUT", uri, jwt, Json.toJson(formData));
  }

  private Result createNode(UUID zoneUuid, NodeInstanceData details, String jwt) {
    String uri = "/api/customers/" + customer.getUuid() + "/zones/" + zoneUuid + "/nodes";
    NodeInstanceFormData formData = new NodeInstanceFormData();
    formData.nodes = Lists.newArrayList(details);
    return doRequestWithJWTAndBody("POST", uri, jwt, Json.toJson(formData));
  }

  private Result unregisterNodeAgent(UUID nodeAgentUuid, String jwt) {
    return doRequestWithJWT(
        "DELETE", "/api/customers/" + customer.getUuid() + "/node_agents/" + nodeAgentUuid, jwt);
  }

  @Test
  public void testNodeAgentRegistrationWorkflow() {
    NodeAgentForm formData = new NodeAgentForm();
    formData.name = "test";
    formData.ip = "10.20.30.40";
    formData.version = "2.12.0";
    formData.osType = OSType.LINUX.name();
    formData.archType = ArchType.AMD64.name();
    formData.home = "/home/yugabyte/node-agent";
    // Register the node agent.
    Result result = registerNodeAgent(formData);
    assertOk(result);
    NodeAgent nodeAgent = Json.fromJson(Json.parse(contentAsString(result)), NodeAgent.class);
    assertNotNull(nodeAgent.getUuid());
    UUID nodeAgentUuid = nodeAgent.getUuid();
    nodeAgent = NodeAgent.getOrBadRequest(customer.getUuid(), nodeAgentUuid);
    assertEquals(State.REGISTERING, nodeAgent.getState());
    String jwt = nodeAgentManager.getClientToken(nodeAgentUuid, user.getUuid());
    result = assertPlatformException(() -> registerNodeAgent(formData));
    assertBadRequest(result, "Node agent is already registered");
    // Report live to the server.
    formData.state = State.READY.name();
    result = updateNodeState(nodeAgentUuid, formData, jwt);
    assertOk(result);
    nodeAgent = NodeAgent.getOrBadRequest(customer.getUuid(), nodeAgentUuid);
    assertEquals(State.READY, nodeAgent.getState());
    NodeInstanceData testNode = new NodeInstanceData();
    testNode.ip = "10.20.30.40";
    testNode.region = region.getCode();
    testNode.zone = zone.getCode();
    testNode.instanceType = "c5.xlarge";
    testNode.sshUser = "ssh-user";
    // Accepted value for NTP_SERVICE_STATUS is "running".
    testNode.nodeConfigs = getTestNodeConfigsSet();
    result = createNode(zone.getUuid(), testNode, jwt);
    assertOk(result);

    NodeConfig pamNode =
        testNode.nodeConfigs.stream()
            .filter(n -> n.type == NodeConfig.Type.PAM_LIMITS_WRITABLE)
            .findFirst()
            .get();
    NodeConfig errCheck = new NodeConfig(NodeConfig.Type.PAM_LIMITS_WRITABLE, "true");
    testNode.nodeConfigs.remove(pamNode);
    testNode.nodeConfigs.add(errCheck);
    // Set an unaccepted value.
    result = assertPlatformException(() -> createNode(zone.getUuid(), testNode, jwt));
    // Missing preflight checks should return an error
    assertEquals(result.status(), BAD_REQUEST);
    result = unregisterNodeAgent(nodeAgentUuid, jwt);
    assertOk(result);
    result = assertPlatformException(() -> getNodeAgent(nodeAgentUuid, jwt));
    assertUnauthorized(result, "Invalid token");
  }

  // @Test
  // TODO H2 does not support some JSON operators/functions.
  public void testListNodeAgents() {
    NodeAgentForm formData = new NodeAgentForm();
    formData.name = "test1";
    formData.ip = "10.20.30.41";
    // Make this version unmatched.
    formData.version = "2.12.0.0";
    formData.osType = OSType.LINUX.name();
    formData.archType = ArchType.AMD64.name();
    formData.home = "/home/yugabyte/node-agent";
    // Register the node agent.
    Result result = registerNodeAgent(formData);
    assertOk(result);
    formData.name = "test2";
    formData.ip = "10.20.30.42";
    // Make this version match.
    formData.version = "2.16.0.0";
    // Register the node agent.
    result = registerNodeAgent(formData);
    assertOk(result);
    NodeAgent nodeAgent = Json.fromJson(Json.parse(contentAsString(result)), NodeAgent.class);
    // Make this unreachable.
    nodeAgent.updateTimestamp(Date.from(Instant.now().minusSeconds(600)));
    result = listNodeAgents();
    try {
      ArrayNode arrayNode = (ArrayNode) Json.parse(contentAsString(result));
      for (JsonNode node : arrayNode) {
        if (node.get("name").asText().equals("test1")) {
          assertTrue("test1 is expected to be reachable", node.get("reachable").asBoolean());
          assertFalse(
              "test1 is expected to have mismatched version",
              node.get("versionMatched").asBoolean());
        } else {
          assertFalse("test2 is expected to be unreachable", node.get("reachable").asBoolean());
          assertTrue(
              "test2 is expected to have matched version", node.get("versionMatched").asBoolean());
        }
      }
    } catch (Exception e) {
      fail(e.getMessage());
    }
  }

  public Set<NodeConfig> getTestNodeConfigsSet() {
    Set<NodeConfig> nodeConfigs = Sets.newSet();
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.PROMETHEUS_NO_NODE_EXPORTER, "true"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.PYTHON_VERSION, "3.0.0"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.USER, "fakeUser"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.USER_GROUP, "fakeGroup"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.INTERNET_CONNECTION, "true"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.TMP_DIR_SPACE, "10000"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.PROMETHEUS_SPACE, "10000"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.PAM_LIMITS_WRITABLE, "false"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.HOME_DIR_SPACE, "10000"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.SSH_PORT, "{\"54422\":\"true\"}"));
    nodeConfigs.add(
        new NodeConfig(NodeConfig.Type.MOUNT_POINTS_WRITABLE, "{\"/home/yugabyte\":\"true\"}"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.NTP_SERVICE_STATUS, "true"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.CPU_CORES, "1"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.RAM_SIZE, "4096"));
    return nodeConfigs;
  }
}
