// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertUnauthorized;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.test.Helpers.contentAsString;

import com.google.common.collect.Lists;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
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
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.NodeConfig;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicReference;
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
  private SettableRuntimeConfigFactory runtimeConfigFactory;
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
    runtimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
    nodeAgentHandler.enableConnectionValidation(false);
    RuntimeConfig<Provider> providerConfig = runtimeConfigFactory.forProvider(provider);
    String nodeAgentConfig = "yb.node_agent.preflight_checks.";
    providerConfig.setValue(nodeAgentConfig + "internet_connection", "true");
    providerConfig.setValue(nodeAgentConfig + "python_version", "2.7");
    providerConfig.setValue(nodeAgentConfig + "prometheus_no_node_exporter", "true");
    providerConfig.setValue(nodeAgentConfig + "ports", "true");
    providerConfig.setValue(nodeAgentConfig + "ntp_service", "true");
    providerConfig.setValue(nodeAgentConfig + "min_tmp_dir_space_mb", "100");
    providerConfig.setValue(nodeAgentConfig + "user", "fakeUser");
    providerConfig.setValue(nodeAgentConfig + "user_group", "fakeGroup");
    providerConfig.setValue(nodeAgentConfig + "min_prometheus_space_mb", "100");
    providerConfig.setValue(nodeAgentConfig + "pam_limits_writable", "true");
    providerConfig.setValue(nodeAgentConfig + "mount_points", "true");
    providerConfig.setValue(nodeAgentConfig + "min_home_dir_space_mb", "100");
    region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    zone = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    user = ModelFactory.testUser(customer);
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.publicKey = "/path/to/public.key";
    keyInfo.privateKey = "/path/to/private.key";
    keyInfo.vaultFile = "/path/to/vault_file";
    keyInfo.vaultPasswordFile = "/path/to/vault_password";
    AccessKey.create(provider.uuid, "access-code1", keyInfo);
    InstanceType.upsert(
        provider.uuid, "c5.xlarge", 1 /* cores */, 2.0 /* mem in GB */, new InstanceTypeDetails());
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
    testNode.instanceType = "c5.xlarge";
    testNode.sshUser = "ssh-user";
    // Missing node configurations in the payload.
    result = assertPlatformException(() -> createNode(zone.uuid, testNode, jwt));
    assertEquals(result.status(), BAD_REQUEST);
    // Accepted value for NTP_SERVICE_STATUS is "running".
    testNode.nodeConfigs = getTestNodeConfigsSet();
    result = createNode(zone.uuid, testNode, jwt);
    assertOk(result);

    NodeConfig pamNode =
        testNode
            .nodeConfigs
            .stream()
            .filter(n -> n.type == NodeConfig.Type.PAM_LIMITS_WRITABLE)
            .findFirst()
            .get();
    NodeConfig errCheck = new NodeConfig(NodeConfig.Type.PAM_LIMITS_WRITABLE, "false");
    testNode.nodeConfigs.remove(pamNode);
    testNode.nodeConfigs.add(errCheck);
    // Set an unaccepted value.
    result = assertPlatformException(() -> createNode(zone.uuid, testNode, jwt));
    // Missing preflight checks should return an error
    assertEquals(result.status(), BAD_REQUEST);
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
    String certPath = nodeAgent.config.get(NodeAgent.CERT_DIR_PATH_PROPERTY);
    // Ping for node state.
    result = pingNodeAgent(nodeAgentUuid);
    assertOk(result);
    State state = Json.fromJson(Json.parse(contentAsString(result)), State.class);
    assertEquals(State.REGISTERING, state);
    AtomicReference<String> jwtRef =
        new AtomicReference<>(nodeAgentHandler.getClientToken(nodeAgentUuid, user.uuid));
    result = assertPlatformException(() -> registerNodeAgent(formData));
    assertBadRequest(result, "Node agent is already registered");
    result = getNodeAgent(nodeAgentUuid, jwtRef.get());
    assertOk(result);
    // Report live to the server.
    formData.state = State.LIVE;
    result = updateNodeState(nodeAgentUuid, formData, jwtRef.get());
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
    result = updateNodeState(nodeAgentUuid, formData, jwtRef.get());
    assertOk(result);
    // Ping for node state.
    result = pingNodeAgent(nodeAgentUuid);
    assertOk(result);
    state = Json.fromJson(Json.parse(contentAsString(result)), State.class);
    assertEquals(State.UPGRADING, state);
    // Reach out to the server to refresh certs.
    result = updateNode(nodeAgentUuid, jwtRef.get());
    assertOk(result);
    nodeAgent = Json.fromJson(Json.parse(contentAsString(result)), NodeAgent.class);
    assertEquals(certPath, nodeAgent.config.get(NodeAgent.CERT_DIR_PATH_PROPERTY));
    // Complete upgrading.
    formData.state = State.UPGRADED;
    result = updateNodeState(nodeAgentUuid, formData, jwtRef.get());
    assertOk(result);
    nodeAgent = Json.fromJson(Json.parse(contentAsString(result)), NodeAgent.class);
    assertNotEquals(certPath, nodeAgent.config.get(NodeAgent.CERT_DIR_PATH_PROPERTY));
    certPath = nodeAgent.config.get(NodeAgent.CERT_DIR_PATH_PROPERTY);
    // Ping for node state.
    result = pingNodeAgent(nodeAgentUuid);
    assertOk(result);
    state = Json.fromJson(Json.parse(contentAsString(result)), State.class);
    assertEquals(State.UPGRADED, state);
    // Restart the node agent and report live to the server.
    formData.state = State.LIVE;
    // Old key is invalid.
    assertThrows(
        "Invalid token",
        PlatformServiceException.class,
        () -> updateNodeState(nodeAgentUuid, formData, jwtRef.get()));
    jwtRef.set(nodeAgentHandler.getClientToken(nodeAgentUuid, user.uuid));
    result = updateNodeState(nodeAgentUuid, formData, jwtRef.get());
    assertOk(result);
    nodeAgent = Json.fromJson(Json.parse(contentAsString(result)), NodeAgent.class);
    assertEquals(certPath, nodeAgent.config.get(NodeAgent.CERT_DIR_PATH_PROPERTY));
    // Ping for node state.
    result = pingNodeAgent(nodeAgentUuid);
    assertOk(result);
    state = Json.fromJson(Json.parse(contentAsString(result)), State.class);
    assertEquals(State.LIVE, state);
    NodeInstanceData testNode = new NodeInstanceData();
    testNode.ip = "10.20.30.40";
    testNode.region = region.code;
    testNode.zone = zone.code;
    testNode.instanceType = "c5.xlarge";
    testNode.sshUser = "ssh-user";
    // Get a new JWT after the update.
    String updatedJwt = nodeAgentHandler.getClientToken(nodeAgentUuid, user.uuid);
    NodeConfig nodeConfig = new NodeConfig(NodeConfig.Type.NTP_SERVICE_STATUS, "true");
    testNode.nodeConfigs = Sets.newSet(nodeConfig);
    // Missing preflight checks should return an error.
    result = assertPlatformException(() -> createNode(zone.uuid, testNode, updatedJwt));
    assertEquals(result.status(), BAD_REQUEST);
    // Accepted value for NTP_SERVICE_STATUS is "running".
    testNode.nodeConfigs = getTestNodeConfigsSet();
    result = createNode(zone.uuid, testNode, updatedJwt);
    assertOk(result);
    result = unregisterNodeAgent(nodeAgentUuid, updatedJwt);
    assertOk(result);
    result = assertPlatformException(() -> getNodeAgent(nodeAgentUuid, updatedJwt));
    assertUnauthorized(result, "Invalid token");
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
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.PAM_LIMITS_WRITABLE, "true"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.HOME_DIR_SPACE, "10000"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.PORTS, "{\"54422\":\"true\"}"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.MOUNT_POINTS, "{\"/home/yugabyte\":\"true\"}"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.NTP_SERVICE_STATUS, "true"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.CPU_CORES, "1"));
    nodeConfigs.add(new NodeConfig(NodeConfig.Type.RAM_SIZE, "4096"));
    return nodeConfigs;
  }
}
