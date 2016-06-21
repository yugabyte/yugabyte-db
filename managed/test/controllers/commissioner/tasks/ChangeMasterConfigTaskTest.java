// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

import java.util.Map;
import java.util.UUID;

import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;

import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;
import play.test.WithApplication;

public class ChangeMasterConfigTaskTest extends WithApplication {

  @Override
  protected Application provideApplication() {
    return new GuiceApplicationBuilder().configure((Map) Helpers.inMemoryDatabase()).build();
  }

  @Before
  public void setUp() {
  }

  @Ignore("JIRA-196: Needs yb.devops.home simulated.")
  public void testAddMasterTask() {
    ObjectNode createInstanceJson = Json.newObject();
    createInstanceJson.put("instanceUUID", UUID.randomUUID().toString());
    createInstanceJson.put("instanceName", "TestAdd");
    // Add an array of subnets.
    ArrayNode arrayNode = createInstanceJson.putArray("subnets");
    arrayNode.add("subnet1");
    arrayNode.add("subnet2");
    arrayNode.add("subnet3");
    // Create the POST task.
    Result result = route(fakeRequest("POST", "/commissioner/tasks").bodyJson(createInstanceJson));
    assertEquals(OK, result.status());
    assertNotNull(contentAsString(result));

    // Create the PUT task to trigger the change master config.
    createInstanceJson.remove("subnets");
    arrayNode = createInstanceJson.putArray("subnets");
    arrayNode.add("subnet4");
    arrayNode.add("subnet5");
    arrayNode.add("subnet6");
    createInstanceJson.put("create", false);
    result = route(fakeRequest("PUT", "/commissioner/tasks").bodyJson(createInstanceJson));
    assertEquals(OK, result.status());
    assertNotNull(contentAsString(result));
  }

  @Ignore("JIRA-196: Needs yb.devops.home simulated.")
  public void testLeaderRemovalTask() {
    ObjectNode createInstanceJson = Json.newObject();
    createInstanceJson.put("instanceUUID", UUID.randomUUID().toString());
    createInstanceJson.put("instanceName", "TestLeaderRemove");
    // Add an array of subnets.
    ArrayNode arrayNode = createInstanceJson.putArray("subnets");
    arrayNode.add("subnet1");
    arrayNode.add("subnet2");
    arrayNode.add("subnet3");
    // Create the POST task.
    Result result = route(fakeRequest("POST", "/commissioner/tasks").bodyJson(createInstanceJson));
    assertEquals(OK, result.status());
    assertNotNull(contentAsString(result));
    
    // Create the PUT task to trigger the change master config.
    // TODO(Bharat): Find the leader's subnet and remove that.
    createInstanceJson.remove("subnets");
    arrayNode = createInstanceJson.putArray("subnets");
    arrayNode.add("subnet1");
    arrayNode.add("subnet2");
    createInstanceJson.put("create", false);
    result = route(fakeRequest("PUT", "/commissioner/tasks").bodyJson(createInstanceJson));
    assertEquals(OK, result.status());
    assertNotNull(contentAsString(result));
  }
}
