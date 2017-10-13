// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.*;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import java.util.LinkedList;
import java.util.UUID;

import com.yugabyte.yw.common.FakeApiHelper;
import org.junit.Before;
import org.junit.Test;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;

import play.libs.Json;
import play.mvc.Result;

public class NodeInstanceControllerTest extends FakeDBApplication {
  private final String FAKE_IP = "fake_ip";
  private Customer customer;
  private Provider provider;
  private Region region;
  private AvailabilityZone zone;
  private NodeInstance node;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.awsProvider(customer);
    region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    zone = AvailabilityZone.create(region, "az-1", "AZ 1", "subnet-1");

    NodeInstanceFormData.NodeInstanceData nodeData = new NodeInstanceFormData.NodeInstanceData();
    nodeData.ip = FAKE_IP;
    nodeData.region = region.code;
    nodeData.zone = zone.code;
    nodeData.instanceType = "fake_instance_type";
    nodeData.sshUser = "ssh-user";
    node = NodeInstance.create(zone.uuid, nodeData);
    // Give it a name.
    node.setNodeName("fake_name");
    node.save();
  }

  private Result getNode(UUID nodeUuid) {
    String uri = "/api/customers/" + customer.uuid + "/nodes/" + nodeUuid + "/list";
    return FakeApiHelper.doRequest("GET", uri);
  }

  private Result listByZone(UUID zoneUuid) {
    String uri = "/api/customers/" + customer.uuid + "/zones/" + zoneUuid + "/nodes/list";
    return FakeApiHelper.doRequest("GET", uri);
  }

  private Result listByProvider(UUID providerUUID) {
    String uri = "/api/customers/" + customer.uuid + "/providers/" + providerUUID + "/nodes/list";
    return FakeApiHelper.doRequest("GET", uri);
  }

  private Result createNode(UUID zoneUuid) {
    String uri = "/api/customers/" + customer.uuid + "/zones/" + zoneUuid + "/nodes";
    NodeInstanceFormData formData = new NodeInstanceFormData();
    formData.nodes = new LinkedList<>();
    formData.nodes.add(node.getDetails());
    JsonNode body = Json.toJson(formData);
    return FakeApiHelper.doRequestWithBody("POST", uri, body);
  }

  private Result deleteNode(UUID customerUUID, UUID providerUUID, String instanceIP) {
    String uri= "/api/customers/" + customerUUID + "/providers/" + providerUUID + "/instances/" + instanceIP;
    return FakeApiHelper.doRequest("DELETE", uri);
  }

  private void checkOk(Result r) { assertEquals(OK, r.status()); }
  private void checkNotOk(Result r, String error) {
    assertNotEquals(OK, r.status());
    if (error != null) {
      JsonNode json = parseResult(r);
      assertEquals(error, json.get("error").asText());
    }
  }

  private void checkNodesMatch(JsonNode queryNode, NodeInstance dbNode) {
    assertEquals(dbNode.nodeUuid.toString(), queryNode.get("nodeUuid").asText());
    assertEquals(dbNode.zoneUuid.toString(), queryNode.get("zoneUuid").asText());
    assertEquals(dbNode.getDetailsJson(), queryNode.get("details").toString());
    assertEquals(dbNode.getDetails().sshUser, queryNode.get("details").get("sshUser").asText());
  }

  private void checkNodeValid(JsonNode nodeAsJson) { checkNodesMatch(nodeAsJson, node); }

  private JsonNode parseResult(Result r) {
    return Json.parse(contentAsString(r));
  }

  @Test
  public void testGetNodeWithValidUuid() {
    Result r = getNode(node.nodeUuid);
    checkOk(r);
    JsonNode json = parseResult(r);
    assertTrue(json.isObject());
    checkNodeValid(json);
  }

  @Test
  public void testGetNodeWithInvalidUuid() {
    Result r = getNode(UUID.randomUUID());
    checkNotOk(r, "Null content");
  }

  @Test
  public void testListByZoneSuccess() {
    Result r = listByZone(zone.uuid);
    checkOk(r);
    JsonNode json = parseResult(r);
    assertTrue(json.isArray());
    assertEquals(1, json.size());
    checkNodeValid(json.get(0));
  }

  @Test
  public void testListByProviderSuccess() {
    Result r = listByProvider(provider.uuid);
    checkOk(r);
    JsonNode json = parseResult(r);
    assertTrue(json.isArray());
    assertEquals(1, json.size());
    checkNodeValid(json.get(0));
  }
  @Test
  public void testListByZoneWrongZone() {
    UUID wrongUuid = UUID.randomUUID();
    Result r = listByZone(wrongUuid);
    String error =
      "Invalid com.yugabyte.yw.models.AvailabilityZoneUUID: " + wrongUuid.toString();
    checkNotOk(r, error);
  }

  @Test
  public void testListByZoneNoFreeNodes() {
    node.inUse = true;
    node.save();
    Result r = listByZone(zone.uuid);
    checkOk(r);

    JsonNode json = parseResult(r);
    assertEquals(0, json.size());

    node.inUse = false;
    node.save();
  }

  @Test
  public void testCreateSuccess() {
    Result r = createNode(zone.uuid);
    checkOk(r);
    JsonNode json = parseResult(r);
    assertThat(json, is(notNullValue()));
    assertTrue(json.isObject());
    JsonNode nodeJson = json.get(FAKE_IP);
    assertThat(nodeJson, is(notNullValue()));
    assertTrue(nodeJson.isObject());

    UUID uuid = UUID.fromString(nodeJson.get("nodeUuid").asText());
    NodeInstance dbNode = NodeInstance.get(uuid);
    assertTrue(dbNode != null);
    checkNodesMatch(nodeJson, dbNode);
  }

  @Test
  public void testCreateFailureInvalidZone() {
    UUID wrongUuid = UUID.randomUUID();
    Result r = createNode(wrongUuid);
    String error =
      "Invalid com.yugabyte.yw.models.AvailabilityZoneUUID: " + wrongUuid.toString();
    checkNotOk(r, error);
  }
  // Test for Delete Instance, use case is only for OnPrem, but test can be validated with AWS provider as well
  @Test
  public void testDeleteInstanceWithValidInstanceIP() {
    Result r = deleteNode(customer.uuid, provider.uuid, FAKE_IP);
    assertOk(r);
  }
  
  @Test
  public void testDeleteInstanceWithInvalidProviderValidInstanceIP() {
    UUID invalidProviderUUID = UUID.randomUUID();
    Result r = deleteNode(customer.uuid, invalidProviderUUID, FAKE_IP);
    assertBadRequest(r, "Invalid Provider UUID: " + invalidProviderUUID);
  }

  @Test
  public void testDeleteInstanceWithValidProviderInvalidInstanceIP() {
    Result r = deleteNode(customer.uuid, provider.uuid, "abc");
    assertBadRequest(r, "Node Not Found");
  }

  @Test
  public void testDeleteInstanceWithInvalidCustomerUUID() {
    UUID invalidCustomerUUID = UUID.randomUUID();
    Result r = deleteNode(invalidCustomerUUID, provider.uuid, "random_ip");
    assertBadRequest(r, "Invalid Customer UUID: " + invalidCustomerUUID);
  }
}
