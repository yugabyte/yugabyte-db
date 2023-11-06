// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.UNAUTHORIZED;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.tasks.params.DetachedNodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.LinkedList;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.mockito.Mockito;
import play.libs.Json;
import play.mvc.Result;

public class NodeInstanceControllerTest extends FakeDBApplication {
  private final String FAKE_IP = "fake_ip";
  private final String FAKE_IP_2 = "fake_ip_2";
  private final String FAKE_INSTANCE_TYPE = "fake_instance_type";
  private Customer customer;
  private Provider provider;
  private Region region;
  private AvailabilityZone zone;
  private NodeInstance node;

  ArgumentCaptor<TaskType> taskType;
  ArgumentCaptor<NodeTaskParams> taskParams;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer("tc", "Test Customer 1");
    ModelFactory.testUser(customer);
    provider = ModelFactory.awsProvider(customer);
    region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    zone = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");

    taskType = ArgumentCaptor.forClass(TaskType.class);
    taskParams = ArgumentCaptor.forClass(NodeTaskParams.class);

    NodeInstanceFormData.NodeInstanceData nodeData1 = new NodeInstanceFormData.NodeInstanceData();
    nodeData1.ip = FAKE_IP;
    nodeData1.region = region.getCode();
    nodeData1.zone = zone.getCode();
    nodeData1.instanceType = FAKE_INSTANCE_TYPE;
    nodeData1.sshUser = "ssh-user";
    node = NodeInstance.create(zone.getUuid(), nodeData1);
    // Give it a name.
    node.setNodeName("fake_name");
    node.save();
  }

  private Result getNode(UUID nodeUuid) {
    String uri = "/api/customers/" + customer.getUuid() + "/nodes/" + nodeUuid + "/list";
    return doRequest("GET", uri);
  }

  private Result getNodeDetails(UUID universeUUID, String nodeName) {
    String uri =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universeUUID
            + "/nodes/"
            + nodeName
            + "/details";
    return doRequest("GET", uri);
  }

  private Result listByZone(UUID zoneUuid) {
    String uri = "/api/customers/" + customer.getUuid() + "/zones/" + zoneUuid + "/nodes/list";
    return doRequest("GET", uri);
  }

  private Result listByProvider(UUID providerUUID) {
    String uri =
        "/api/customers/" + customer.getUuid() + "/providers/" + providerUUID + "/nodes/list";
    return doRequest("GET", uri);
  }

  private Result createNode(UUID zoneUuid, NodeInstanceFormData.NodeInstanceData details) {
    String uri = "/api/customers/" + customer.getUuid() + "/zones/" + zoneUuid + "/nodes";
    NodeInstanceFormData formData = new NodeInstanceFormData();
    formData.nodes = new LinkedList<>();
    formData.nodes.add(details);
    JsonNode body = Json.toJson(formData);
    return doRequestWithBody("POST", uri, body);
  }

  private Result deleteInstance(UUID customerUUID, UUID providerUUID, String instanceIP) {
    String uri =
        "/api/customers/"
            + customerUUID
            + "/providers/"
            + providerUUID
            + "/instances/"
            + instanceIP;
    return doRequest("DELETE", uri);
  }

  private Result performNodeAction(
      UUID customerUUID,
      UUID universeUUID,
      String nodeName,
      NodeActionType nodeAction,
      boolean mimicError) {
    String uri =
        "/api/customers/" + customerUUID + "/universes/" + universeUUID + "/nodes/" + nodeName;
    ObjectNode params = Json.newObject();
    if (mimicError) {
      params.put("foo", "bar");
    } else {
      params.put("nodeAction", nodeAction.name());
    }

    return doRequestWithBody("PUT", uri, params);
  }

  private Result performDetachedNodeAction(
      UUID customerUUID,
      UUID providerUUID,
      String nodeIP,
      NodeActionType nodeAction,
      boolean mimicError) {
    String uri =
        "/api/customers/" + customerUUID + "/providers/" + providerUUID + "/instances/" + nodeIP;
    ObjectNode params = Json.newObject();
    if (mimicError) {
      params.put("foo", "bar");
    } else {
      params.put("nodeAction", nodeAction.name());
    }
    return doRequestWithBody("POST", uri, params);
  }

  private void setNodeState(UUID universeUUID, String nodeName, NodeState state) {
    Universe.UniverseUpdater updater =
        new Universe.UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            NodeDetails node = universe.getNode(nodeName);
            node.state = state;
            universe.setUniverseDetails(universeDetails);
          }
        };
    Universe.saveDetails(universeUUID, updater);
  }

  private void checkOk(Result r) {
    assertEquals(OK, r.status());
  }

  private void checkNotOk(Result r, String error) {
    assertNotEquals(OK, r.status());
    if (error != null) {
      JsonNode json = parseResult(r);
      assertEquals(error, json.get("error").asText());
    }
  }

  private void checkNodesMatch(JsonNode queryNode, NodeInstance dbNode) {
    assertEquals(dbNode.getNodeUuid().toString(), queryNode.get("nodeUuid").asText());
    assertEquals(dbNode.getZoneUuid().toString(), queryNode.get("zoneUuid").asText());
    assertEquals(dbNode.getDetailsJson(), queryNode.get("details").toString());
    assertEquals(dbNode.getDetails().sshUser, queryNode.get("details").get("sshUser").asText());
  }

  private void checkNodeValid(JsonNode nodeAsJson) {
    checkNodesMatch(nodeAsJson, node);
  }

  private JsonNode parseResult(Result r) {
    return Json.parse(contentAsString(r));
  }

  @Test
  public void testGetNodeWithValidUuid() {
    Result r = getNode(node.getNodeUuid());
    checkOk(r);
    JsonNode json = parseResult(r);
    assertTrue(json.isObject());
    checkNodeValid(json);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testGetNodeWithInvalidUuid() {
    UUID uuid = UUID.randomUUID();
    Result r = assertPlatformException(() -> getNode(uuid));
    String expectedError = "Invalid node UUID: " + uuid;
    assertBadRequest(r, expectedError);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testGetNodeDetailsWithValidUuid() {
    Universe u =
        Universe.saveDetails(
            ModelFactory.createUniverse("node-allowed-actions", customer.getId()).getUniverseUUID(),
            ApiUtils.mockUniverseUpdater());
    Result r = getNodeDetails(u.getUniverseUUID(), "host-n1");
    checkOk(r);
    JsonNode json = parseResult(r);
    assertTrue(json.isObject());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testGetNodeDetailsWithInValidUuid() {
    UUID uuid = UUID.randomUUID();
    Result r = assertPlatformException(() -> getNodeDetails(uuid, "host-n1"));
    String expectedError = "Cannot find universe " + uuid;
    assertBadRequest(r, expectedError);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListByZoneSuccess() {
    Result r = listByZone(zone.getUuid());
    checkOk(r);
    JsonNode json = parseResult(r);
    assertTrue(json.isArray());
    assertEquals(1, json.size());
    checkNodeValid(json.get(0));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListByProviderSuccess() {
    Result r = listByProvider(provider.getUuid());
    checkOk(r);
    JsonNode json = parseResult(r);
    assertTrue(json.isArray());
    assertEquals(1, json.size());
    checkNodeValid(json.get(0));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListByZoneWrongZone() {
    UUID wrongUuid = UUID.randomUUID();
    Result r = assertPlatformException(() -> listByZone(wrongUuid));
    String expectedError = "Invalid AvailabilityZone UUID: " + wrongUuid.toString();
    checkNotOk(r, expectedError);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListByZoneNoFreeNodes() {
    node.setInUse(true);
    node.save();
    Result r = listByZone(zone.getUuid());
    checkOk(r);

    JsonNode json = parseResult(r);
    assertEquals(0, json.size());

    node.setInUse(false);
    node.save();
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateSuccess() {
    NodeInstanceFormData.NodeInstanceData testNode = new NodeInstanceFormData.NodeInstanceData();
    testNode.ip = FAKE_IP_2;
    testNode.region = region.getCode();
    testNode.zone = zone.getCode();
    testNode.instanceType = "fake_instance_type";
    testNode.sshUser = "ssh-user";
    Result successReq = createNode(zone.getUuid(), testNode);
    checkOk(successReq);
    JsonNode json = parseResult(successReq);
    assertThat(json, is(notNullValue()));
    assertTrue(json.isObject());
    JsonNode nodeJson = json.get(FAKE_IP_2);
    assertThat(nodeJson, is(notNullValue()));
    assertTrue(nodeJson.isObject());

    UUID uuid = UUID.fromString(nodeJson.get("nodeUuid").asText());
    NodeInstance dbNode = NodeInstance.get(uuid);
    assertTrue(dbNode != null);
    checkNodesMatch(nodeJson, dbNode);
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateFailureDuplicateIp() {
    Result failedReq = assertPlatformException(() -> createNode(zone.getUuid(), node.getDetails()));
    checkNotOk(failedReq, "Invalid nodes in request. Duplicate IP Addresses are not allowed.");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateFailureInvalidZone() {
    UUID wrongUuid = UUID.randomUUID();
    Result r = assertPlatformException(() -> createNode(wrongUuid, node.getDetails()));
    String error = "Invalid AvailabilityZone UUID: " + wrongUuid.toString();
    checkNotOk(r, error);
    assertAuditEntry(0, customer.getUuid());
  }

  // Test for Delete Instance, use case is only for OnPrem, but test can be validated with AWS
  // provider as well
  @Test
  public void testDeleteInstanceWithValidInstanceIP() {
    Result r = deleteInstance(customer.getUuid(), provider.getUuid(), FAKE_IP);
    assertOk(r);
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testDeleteInstanceWithInvalidProviderValidInstanceIP() {
    UUID invalidProviderUUID = UUID.randomUUID();
    Result r =
        assertPlatformException(
            () -> deleteInstance(customer.getUuid(), invalidProviderUUID, FAKE_IP));
    assertBadRequest(r, "Cannot find provider " + invalidProviderUUID);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testDeleteInstanceWithValidProviderInvalidInstanceIP() {
    Result r =
        assertPlatformException(
            () -> deleteInstance(customer.getUuid(), provider.getUuid(), "abc"));
    assertBadRequest(r, "Node Not Found");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testDeleteInstanceWithInvalidCustomerUUID() {
    UUID invalidCustomerUUID = UUID.randomUUID();
    Result r = deleteInstance(invalidCustomerUUID, provider.getUuid(), "random_ip");
    assertEquals(UNAUTHORIZED, r.status());

    String resultString = contentAsString(r);
    assertEquals(resultString, "Unable To Authenticate User");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testDeleteInstance() {
    Result r = deleteInstance(customer.getUuid(), provider.getUuid(), FAKE_IP);
    assertOk(r);
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testMissingNodeActionParam() {
    verify(mockCommissioner, times(0)).submit(any(), any());
    final Universe u = ModelFactory.createUniverse();
    Universe universe = Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    Result r =
        assertPlatformException(
            () ->
                performNodeAction(
                    customer.getUuid(),
                    universe.getUniverseUUID(),
                    "host-n1",
                    NodeActionType.DELETE,
                    true));
    assertBadRequest(r, "{\"nodeAction\":[\"must not be null\"]}");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testInvalidNodeAction() {
    for (NodeActionType nodeActionType : NodeActionType.values()) {
      Universe u = ModelFactory.createUniverse(nodeActionType.name(), customer.getId());
      verify(mockCommissioner, times(0)).submit(any(), any());
      Result r =
          assertPlatformException(
              () ->
                  performNodeAction(
                      customer.getUuid(), u.getUniverseUUID(), "fake-n1", nodeActionType, true));
      assertBadRequest(r, "Invalid Node fake-n1 for Universe");
      assertAuditEntry(0, customer.getUuid());
    }
  }

  @Test
  public void testValidNodeAction() {
    for (NodeActionType nodeActionType : NodeActionType.values()) {
      // Skip QUERY b/c it is UI-only flag.
      // Skip DELETE - tested in another test (testDisableStopRemove).
      if ((nodeActionType == NodeActionType.QUERY) || (nodeActionType == NodeActionType.DELETE)) {
        continue;
      }
      UUID fakeTaskUUID = UUID.randomUUID();
      when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
          .thenReturn(fakeTaskUUID);
      Universe u = ModelFactory.createUniverse(nodeActionType.name(), customer.getId());
      u = Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
      Result r =
          performNodeAction(
              customer.getUuid(), u.getUniverseUUID(), "host-n1", nodeActionType, false);
      verify(mockCommissioner, times(1)).submit(taskType.capture(), taskParams.capture());
      assertEquals(nodeActionType.getCommissionerTask(), taskType.getValue());
      assertOk(r);
      JsonNode json = Json.parse(contentAsString(r));
      assertValue(json, "taskUUID", fakeTaskUUID.toString());
      CustomerTask ct = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
      assertNotNull(ct);
      assertEquals(CustomerTask.TargetType.Node, ct.getTargetType());
      assertEquals(nodeActionType.getCustomerTask(), ct.getType());
      assertEquals("host-n1", ct.getTargetName());
      Mockito.reset(mockCommissioner);
    }
    assertAuditEntry(NodeActionType.values().length - 2, customer.getUuid());
  }

  @Test
  public void testDisableStopRemoveDelete() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Universe u =
        Universe.saveDetails(
            ModelFactory.createUniverse("disable-stop-remove-rf-3", customer.getId())
                .getUniverseUUID(),
            ApiUtils.mockUniverseUpdater());
    setNodeState(u.getUniverseUUID(), "host-n1", NodeState.Removed);

    NodeDetails curNode = u.getNode("host-n1");
    Result invalidRemove =
        assertPlatformException(
            () ->
                performNodeAction(
                    customer.getUuid(),
                    u.getUniverseUUID(),
                    curNode.nodeName,
                    NodeActionType.REMOVE,
                    false));
    assertBadRequest(
        invalidRemove, "Cannot REMOVE " + curNode.nodeName + ": It is in Removed state");

    setNodeState(u.getUniverseUUID(), "host-n1", NodeState.Live);
    // Another node is already removed (under-replicated), but quorum is maintained.
    setNodeState(u.getUniverseUUID(), "host-n2", NodeState.Stopped);

    invalidRemove =
        assertPlatformException(
            () ->
                performNodeAction(
                    customer.getUuid(),
                    u.getUniverseUUID(),
                    curNode.nodeName,
                    NodeActionType.REMOVE,
                    false));

    assertBadRequest(
        invalidRemove,
        "Cannot REMOVE "
            + curNode.nodeName
            + ": As it will under replicate the masters (count = 2, replicationFactor = 3)");

    setNodeState(u.getUniverseUUID(), "host-n1", NodeState.Stopped);

    setNodeState(u.getUniverseUUID(), "host-n2", NodeState.Stopped);

    invalidRemove =
        assertPlatformException(
            () ->
                performNodeAction(
                    customer.getUuid(),
                    u.getUniverseUUID(),
                    curNode.nodeName,
                    NodeActionType.REMOVE,
                    false));

    assertBadRequest(
        invalidRemove,
        "Cannot REMOVE "
            + curNode.nodeName
            + ": As it will under replicate the masters (count = 1, replicationFactor = 3)");

    setNodeState(u.getUniverseUUID(), "host-n1", NodeState.Live);

    Result invalidReboot =
        assertPlatformException(
            () ->
                performNodeAction(
                    customer.getUuid(),
                    u.getUniverseUUID(),
                    curNode.nodeName,
                    NodeActionType.REBOOT,
                    false));
    assertBadRequest(
        invalidReboot,
        "Cannot REBOOT "
            + curNode.nodeName
            + ": As it will under replicate the masters (count = 2, replicationFactor = 3)");

    // Changing to another node as n1 is in progress by previous operations.
    NodeDetails nodeToDelete = u.getNode("host-n3");
    Result invalidDelete =
        assertPlatformException(
            () ->
                performNodeAction(
                    customer.getUuid(),
                    u.getUniverseUUID(),
                    nodeToDelete.nodeName,
                    NodeActionType.DELETE,
                    false));
    assertBadRequest(
        invalidDelete, "Cannot DELETE " + nodeToDelete.nodeName + ": It is in Live state");

    Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> univ.getNode(nodeToDelete.nodeName).state = NodeState.Decommissioned);

    invalidDelete =
        assertPlatformException(
            () ->
                performNodeAction(
                    customer.getUuid(),
                    u.getUniverseUUID(),
                    nodeToDelete.nodeName,
                    NodeActionType.DELETE,
                    false));
    assertBadRequest(
        invalidDelete,
        "Cannot DELETE "
            + nodeToDelete.nodeName
            + ": Unable to have less nodes than RF (count = 3, replicationFactor = 3)");

    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testStartNodeActionPassesClustersAndRootCAInTaskParams() {
    NodeActionType nodeActionType = NodeActionType.START;
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = ModelFactory.createUniverse(nodeActionType.name(), customer.getId());
    assertNotNull(u.getUniverseDetails().clusters);
    u.getUniverseDetails().rootCA = UUID.randomUUID();

    u = Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    Result r =
        performNodeAction(
            customer.getUuid(), u.getUniverseUUID(), "host-n1", nodeActionType, false);
    verify(mockCommissioner, times(1)).submit(taskType.capture(), taskParams.capture());
    assertEquals(nodeActionType.getCommissionerTask(), taskType.getValue());
    assertOk(r);
    assertEquals(u.getUniverseDetails().clusters.size(), taskParams.getValue().clusters.size());
    assertTrue(taskParams.getValue().clusters.size() > 0);
    assertTrue(
        u.getUniverseDetails().clusters.get(0).equals(taskParams.getValue().clusters.get(0)));
    assertEquals(u.getUniverseDetails().rootCA, taskParams.getValue().rootCA);
    Mockito.reset(mockCommissioner);
  }

  @Test
  public void testDetachedNodeActionValid() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(DetachedNodeTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Result r =
        performDetachedNodeAction(
            customer.getUuid(),
            provider.getUuid(),
            FAKE_IP,
            NodeActionType.PRECHECK_DETACHED,
            false);
    ArgumentCaptor<DetachedNodeTaskParams> paramsCaptor =
        ArgumentCaptor.forClass(DetachedNodeTaskParams.class);
    assertOk(r);
    verify(mockCommissioner, times(1))
        .submit(Mockito.eq(TaskType.PrecheckNodeDetached), paramsCaptor.capture());
    DetachedNodeTaskParams params = paramsCaptor.getValue();
    assertEquals(params.getInstanceType(), FAKE_INSTANCE_TYPE);
    assertEquals(params.getNodeUuid(), node.getNodeUuid());
    assertEquals(params.getAzUuid(), node.getZoneUuid());
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testDetachedNodeActionInvalidProviderIP() {
    UUID invalidProviderUUID = UUID.randomUUID();
    Result r =
        assertPlatformException(
            () ->
                performDetachedNodeAction(
                    customer.getUuid(),
                    invalidProviderUUID,
                    FAKE_IP,
                    NodeActionType.PRECHECK_DETACHED,
                    false));

    assertBadRequest(r, "Cannot find provider " + invalidProviderUUID);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testDetachedNodeActionInvalidIP() {
    Result r =
        assertPlatformException(
            () ->
                performDetachedNodeAction(
                    customer.getUuid(),
                    provider.getUuid(),
                    FAKE_IP_2,
                    NodeActionType.PRECHECK_DETACHED,
                    false));

    assertBadRequest(r, "Node Not Found");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testDetachedNodeActionAlreadyInProgress() {
    CustomerTask.create(
        customer,
        node.getNodeUuid(),
        UUID.randomUUID(),
        CustomerTask.TargetType.Node,
        CustomerTask.TaskType.PrecheckNode,
        node.getNodeName());

    Result r =
        assertPlatformException(
            () ->
                performDetachedNodeAction(
                    customer.getUuid(),
                    provider.getUuid(),
                    FAKE_IP,
                    NodeActionType.PRECHECK_DETACHED,
                    false));

    assertBadRequest(r, "Node " + node.getNodeUuid() + " has incomplete tasks");
    assertAuditEntry(0, customer.getUuid());
  }
}
