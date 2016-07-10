package controllers.metamaster;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;

import helpers.FakeDBApplication;
import play.libs.Json;
import play.mvc.Http.RequestBuilder;
import play.mvc.Result;

public class MetaMasterControllerTest extends FakeDBApplication {

  public static final Logger LOG = LoggerFactory.getLogger(MetaMasterControllerTest.class);

  @Test
  public void testGetNonExistentInstance() {
    String instanceUUID = "11111111-2222-3333-4444-555555555555";
    Result result = route(fakeRequest("GET", "/metamaster/instance/" + instanceUUID));
    // This should be a bad request.
    assertRestResult(result, false, BAD_REQUEST);
  }

  @Test
  public void testCRUDOperations() {
    String instanceUUID = "11111111-2222-3333-4444-555555555555";
    // Add 3 master host port as the params.
    ObjectNode paramsJson = getMastersJson("host", 7000, 3);

    // Create the metamaster entry.
    RequestBuilder req = fakeRequest("POST", "/metamaster/instance/" + instanceUUID);
    LOG.info("Sending request to URI [" + req.path() + "], params: " +  paramsJson.toString());
    Result result = route(req.bodyJson(paramsJson));
    // Make sure it succeeds.
    assertRestResult(result, true, OK);

    // Read the value back.
    result = route(fakeRequest("GET", "/metamaster/instance/" + instanceUUID));
    LOG.info("Created cluster, result: [" + contentAsString(result) + "]");
    assertRestResult(result, true, OK);

    // Verify that the correct data is present.
    JsonNode json = Json.parse(contentAsString(result));
    String masters = json.get("masters").textValue();
    assertEquals(masters, getMasters("host", 7000, 3));

    // Update the masters to have a different value.
    paramsJson = getMastersJson("newhost", 7000, 3);
    // Update the metamaster entry.
    req = fakeRequest("POST", "/metamaster/instance/" + instanceUUID);
    LOG.info("Sending request to URI [" + req.path() + "], params: " +  paramsJson.toString());
    result = route(req.bodyJson(paramsJson));
    // Make sure it succeeds.
    assertRestResult(result, true, OK);

    // Read the updated value back.
    result = route(fakeRequest("GET", "/metamaster/instance/" + instanceUUID));
    LOG.info("Reading masters for cluster, result: [" + contentAsString(result) + "]");
    assertRestResult(result, true, OK);

    // Verify that the updated data is present.
    json = Json.parse(contentAsString(result));
    masters = json.get("masters").textValue();
    assertEquals(masters, getMasters("newhost", 7000, 3));

    // Delete the value.
    result = route(fakeRequest("DELETE", "/metamaster/instance/" + instanceUUID));
    LOG.info("Deleting masters for cluster, result: [" + contentAsString(result) + "]");
    assertRestResult(result, true, OK);

    // Read the updated value and verify there is no data present.
    result = route(fakeRequest("GET", "/metamaster/instance/" + instanceUUID));
    LOG.info("Reading masters for cluster, result: [" + contentAsString(result) + "]");
    assertRestResult(result, false, BAD_REQUEST);
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

  private ObjectNode getMastersJson(String hostPrefix, int port, int numMasters) {
    ObjectNode paramsJson = Json.newObject();
    ArrayNode arrayNode = paramsJson.putArray("masters");
    for (int i = 0; i < numMasters; i++) {
      ObjectNode obj = arrayNode.addObject();
      obj.put("host", hostPrefix + i);
      obj.put("port", port);
    }
    return paramsJson;
  }

  private String getMasters(String hostPrefix, int port, int numMasters) {
    StringBuilder sb = new StringBuilder();
    for (int i = 0; i < numMasters; i++) {
      if (i > 0) {
        sb.append(",");
      }
      sb.append(hostPrefix + i);
      sb.append(":");
      sb.append(port);
    }
    return sb.toString();
  }
}
