package com.yugabyte.yw.metamaster;

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

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;

import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.models.Universe;
import com.yugabyte.yw.commissioner.models.Universe.NodeDetails;
import com.yugabyte.yw.commissioner.models.Universe.UniverseDetails;
import com.yugabyte.yw.commissioner.models.Universe.UniverseUpdater;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.metamaster.MetaMasterController;

import play.libs.Json;
import play.mvc.Result;

public class MetaMasterControllerTest extends FakeDBApplication {

  public static final Logger LOG = LoggerFactory.getLogger(MetaMasterControllerTest.class);

  @Test
  public void testGetNonExistentInstance() {
    LOG.info("Trying to lookup a universe that is not present.");
    String universeUUID = "11111111-2222-3333-4444-555555555555";
    Result result = route(fakeRequest("GET", "/metamaster/universe/" + universeUUID));
    // This should be a bad request.
    assertRestResult(result, false, BAD_REQUEST);
  }

  @Test
  public void testCRUDOperations() {
    UUID universeUUID = UUID.fromString("11111111-2222-3333-4444-555555555555");
    // Create the universe.
    Universe.create(universeUUID);
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDetails universeDetails = universe.universeDetails;

        // Create some subnets.
        List<String> subnets = new ArrayList<String>();
        subnets.add("subnet-1");
        subnets.add("subnet-2");
        subnets.add("subnet-3");
        universeDetails.subnets = subnets;

        // Add a desired number of nodes.
        universeDetails.numNodes = 5;
        for (int idx = 1; idx <= universeDetails.numNodes; idx++) {
          NodeDetails node = new NodeDetails();
          // Create the node name.
          node.instance_name = "host-n" + idx;
          // Set the cloud name.
          node.cloud = "aws";
          // Pick one of the VPCs in a round robin fashion.
          node.subnet_id = subnets.get(idx % subnets.size());
          // Add an ip address.
          node.private_ip = "host-n" + idx;
          // Make all nodes tservers.
          node.isTserver = true;
          // Make the first three nodes masters.
          if (idx <= 3) {
            node.isMaster = true;
          }
          // Set the node id.
          node.nodeIdx = idx;
          // Save the nodes.
          universeDetails.nodeDetailsMap.put(node.instance_name, node);
          LOG.info("Adding node " + node.instance_name + " to universe");
        }
      }
    };
    // Save the updates to the universe.
    Universe.save(universeUUID, updater);


    // Read the value back.
    Result result = route(fakeRequest("GET", "/metamaster/universe/" + universeUUID.toString()));
    LOG.info("Read universe, result: [" + contentAsString(result) + "]");
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
      assertTrue(masterNodeNames.contains(node.private_ip));
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
