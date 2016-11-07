package com.yugabyte.yw.api.controllers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

import com.yugabyte.yw.controllers.MetaMasterController;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.junit.Test;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;

import play.libs.Json;
import play.mvc.Result;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;


public class MetaMasterControllerTest extends FakeDBApplication {

  @Test
  public void testGetWithInvalidUniverse() {
    String universeUUID = "11111111-2222-3333-4444-555555555555";
    Result result = route(fakeRequest("GET", "/metamaster/universe/" + universeUUID));
    assertRestResult(result, false, BAD_REQUEST);
  }

  @Test
  public void testGetWithValidUniverse() {
    Universe u = Universe.create("Test Universe", UUID.randomUUID(), 0L);
    Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        universeDetails = new UniverseDefinitionTaskParams();
        universeDetails.userIntent = new UserIntent();

        // Create some subnets.
        List<String> subnets = new ArrayList<String>();
        subnets.add("subnet-1");
        subnets.add("subnet-2");
        subnets.add("subnet-3");

        // Add a desired number of nodes.
        universeDetails.userIntent.numNodes = 5;
        universeDetails.nodeDetailsSet = new HashSet<NodeDetails>();
        for (int idx = 1; idx <= universeDetails.userIntent.numNodes; idx++) {
          NodeDetails node = new NodeDetails();
          node.nodeName = "host-n" + idx;
          node.cloudInfo = new CloudSpecificInfo();
          node.cloudInfo.cloud = "aws";
          node.cloudInfo.subnet_id = subnets.get(idx % subnets.size());
          node.cloudInfo.private_ip = "host-n" + idx;
          node.isTserver = true;
          if (idx <= 3) {
            node.isMaster = true;
          }
          node.nodeIdx = idx;
          universeDetails.nodeDetailsSet.add(node);
        }
      }
    };
    // Save the updates to the universe.
    Universe.saveDetails(u.universeUUID, updater);


    // Read the value back.
    Result result = route(fakeRequest("GET", "/metamaster/universe/" + u.universeUUID.toString()));
    assertRestResult(result, true, OK);
    // Verify that the correct data is present.
    JsonNode jsonNode = Json.parse(contentAsString(result));
    MetaMasterController.MastersList masterList =
      Json.fromJson(jsonNode, MetaMasterController.MastersList.class);
    Set<String> masterNodeNames = new HashSet<String>();
    masterNodeNames.add("host-n1");
    masterNodeNames.add("host-n2");
    masterNodeNames.add("host-n3");
    for (MetaMasterController.MasterNode node : masterList.masters) {
      assertTrue(masterNodeNames.contains(node.cloudInfo.private_ip));
    }
  }

  private void assertRestResult(Result result, boolean expectSuccess, int expectStatus) {
    assertEquals(expectStatus, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    if (expectSuccess) {
      assertNull(json.get("error"));
    } else {
      assertNotNull(json.get("error"));
      assertFalse(json.get("error").asText().isEmpty());
    }
  }
}
