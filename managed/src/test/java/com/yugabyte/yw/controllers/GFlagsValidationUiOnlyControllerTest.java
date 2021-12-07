package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertNotNull;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static play.test.Helpers.contentAsString;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.AssertHelper;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.GFlagDetails;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.naming.TestCaseName;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
public class GFlagsValidationUiOnlyControllerTest extends FakeDBApplication {

  private Customer defaultCustomer;
  private Users defaultUser;
  String gflagName = "cdc_enable_replicate_intents";

  @Before
  public void setUp() throws IOException {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer);

    // Mock gflags extraction from gflag metadata
    GFlagDetails flag1 = new GFlagDetails();
    flag1.name = "cdc_enable_replicate_intents";
    flag1.type = "bool";
    GFlagDetails flag2 = new GFlagDetails();
    flag2.name = "update_metrics_interval_ms";
    flag2.type = "int32";
    List<GFlagDetails> gflagList = new ArrayList<>(Arrays.asList(flag1, flag2));
    when(mockGFlagsValidation.extractGFlags(any(), any(), anyBoolean())).thenReturn(gflagList);
  }

  @Test
  @Parameters({
    "1.1.1.1-b11, MASTER",
    "1.1.1.1, MASTER",
    "1.1.1.1-b11, TSERVER",
    "1.1.1.1, TSERVER",
  })
  @TestCaseName("testGetGFlagsMetadataWithValidParamsWhen " + "version:{0} serverType:{1}")
  public void testGetGFlagsMetadataWithValidParams(String version, String serverType) {
    String url =
        "/api/v1/metadata"
            + "/version/"
            + version
            + "/gflag?name="
            + gflagName
            + "&server="
            + serverType;
    Result result = FakeApiHelper.doRequestWithAuthToken("GET", url, defaultUser.createAuthToken());
    AssertHelper.assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "name", gflagName);
  }

  @Test
  public void testGetGFlagMetadataWithInvalidGFlag() {
    String gflagName = "invalid_gflag";
    String url =
        "/api/v1/metadata" + "/version/1.1.1.1-b11" + "/gflag?name=" + gflagName + "&server=MASTER";
    Result result =
        assertPlatformException(
            () -> FakeApiHelper.doRequestWithAuthToken("GET", url, defaultUser.createAuthToken()));
    AssertHelper.assertBadRequest(result, gflagName + " is not present in metadata.");
  }

  @Test
  @Parameters({"1.1-b11", "1.1.1.1.1", "1.1.1-b11", "1"})
  @TestCaseName("testGetGFlagMetadataWithInvalidDBVersionWhen " + "version:{0}")
  public void testGetGFlagMetadataWithInvalidDBVersion(String version) {
    String url =
        "/api/v1/metadata" + "/version/" + version + "/gflag?name=" + gflagName + "&server=MASTER";
    Result result =
        assertPlatformException(
            () -> FakeApiHelper.doRequestWithAuthToken("GET", url, defaultUser.createAuthToken()));
    AssertHelper.assertBadRequest(
        result, "Incorrect version format. Valid formats: 1.1.1.1 or 1.1.1.1-b1");
  }

  @Test
  public void testGetGFlagMetadataWithInvalidServerType() {
    String invalidServerType = "invalidServerType";
    String url =
        "/api/v1/metadata"
            + "/version/1.1.1.1-b78"
            + "/gflag?name="
            + gflagName
            + "&server="
            + invalidServerType;
    Result result =
        assertPlatformException(
            () -> FakeApiHelper.doRequestWithAuthToken("GET", url, defaultUser.createAuthToken()));
    AssertHelper.assertBadRequest(result, "Given server type is not valid");
  }

  @Test
  public void testValidateGFlagWithValidParams() {
    ObjectNode body = Json.newObject();
    ObjectNode masterGFlags = Json.newObject();
    masterGFlags.put("cdc_enable_replicate_intents", "true");
    masterGFlags.put("update_metrics_interval_ms", "15000");
    body.set("MASTER", masterGFlags);
    ObjectNode tserverGFlags = Json.newObject();
    tserverGFlags.put("cdc_enable_replicate_intents", "true");
    tserverGFlags.put("update_metrics_interval_ms", "15000");
    body.set("TSERVER", tserverGFlags);

    String url = "/api/v1/metadata" + "/version/1.1.1.1-b11" + "/validate_gflags";
    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "POST", url, defaultUser.createAuthToken(), body);
    AssertHelper.assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));

    ObjectNode expectedJson = Json.newObject();
    ObjectNode expectedMasterGFlags = Json.newObject();
    expectedMasterGFlags.set("cdc_enable_replicate_intents", Json.newObject());
    expectedMasterGFlags.set("update_metrics_interval_ms", Json.newObject());
    expectedJson.set("MASTER", expectedMasterGFlags);
    ObjectNode expectedTserverGFlags = Json.newObject();
    expectedTserverGFlags.set("cdc_enable_replicate_intents", Json.newObject());
    expectedTserverGFlags.set("update_metrics_interval_ms", Json.newObject());
    expectedJson.set("TSERVER", expectedTserverGFlags);
    AssertHelper.assertJsonEqual(expectedJson, json);
  }

  @Test
  public void testValidateGFlagWithInvalidDatatype() {
    ObjectNode body = Json.newObject();
    ObjectNode masterGFlags = Json.newObject();
    masterGFlags.put("cdc_enable_replicate_intents", "string");
    masterGFlags.put("update_metrics_interval_ms", "string");
    body.set("MASTER", masterGFlags);
    ObjectNode tserverGFlags = Json.newObject();
    tserverGFlags.put("cdc_enable_replicate_intents", "true");
    tserverGFlags.put("update_metrics_interval_ms", "15000");
    body.set("TSERVER", tserverGFlags);

    String url = "/api/v1/metadata" + "/version/1.1.1.1-b11" + "/validate_gflags";
    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "POST", url, defaultUser.createAuthToken(), body);
    AssertHelper.assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));

    ObjectNode expectedJson = Json.newObject();
    ObjectNode expectedMasterGFlags = Json.newObject();
    ObjectNode int32Error = Json.newObject();
    int32Error.put("type", "Given string is not a int32 type");
    expectedMasterGFlags.set("update_metrics_interval_ms", int32Error);
    ObjectNode boolError = Json.newObject();
    boolError.put("type", "Given string is not a bool type");
    expectedMasterGFlags.set("cdc_enable_replicate_intents", boolError);
    expectedJson.set("MASTER", expectedMasterGFlags);
    ObjectNode expectedTserverGFlags = Json.newObject();
    expectedTserverGFlags.set("cdc_enable_replicate_intents", Json.newObject());
    expectedTserverGFlags.set("update_metrics_interval_ms", Json.newObject());
    expectedJson.set("TSERVER", expectedTserverGFlags);
    AssertHelper.assertJsonEqual(expectedJson, json);
  }

  @Test
  public void testValiadtedGFlagWithIncorrectGFlagName() {
    String gflagName = "invalid_gflag";
    ObjectNode body = Json.newObject();
    ObjectNode masterGFlags = Json.newObject();
    masterGFlags.put(gflagName, "123");
    masterGFlags.put("update_metrics_interval_ms", "15000");
    body.set("MASTER", masterGFlags);
    ObjectNode tserverGFlags = Json.newObject();
    tserverGFlags.put(gflagName, "123");
    tserverGFlags.put("update_metrics_interval_ms", "15000");
    body.set("TSERVER", tserverGFlags);

    String url = "/api/v1/metadata" + "/version/1.1.1.1-b11" + "/validate_gflags";
    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "POST", url, defaultUser.createAuthToken(), body);
    AssertHelper.assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));

    ObjectNode expectedJson = Json.newObject();
    ObjectNode expectedMasterGFlags = Json.newObject();
    ObjectNode gflagNameError = Json.newObject();
    gflagNameError.put("name", gflagName + " is not present as per current metadata");
    expectedMasterGFlags.set(gflagName, gflagNameError);
    expectedMasterGFlags.set("update_metrics_interval_ms", Json.newObject());
    expectedJson.set("MASTER", expectedMasterGFlags);
    ObjectNode expectedTserverGFlags = Json.newObject();
    expectedTserverGFlags.set(gflagName, gflagNameError);
    expectedTserverGFlags.set("update_metrics_interval_ms", Json.newObject());
    expectedJson.set("TSERVER", expectedTserverGFlags);
    AssertHelper.assertJsonEqual(expectedJson, json);
  }

  @Test
  public void testValidateGFlagWithEmptyParams() {
    String url = "/api/v1/metadata" + "/version/1.1.1.1-b89" + "/validate_gflags";
    ObjectNode body = Json.newObject();
    body.set("MASTER", Json.newObject());
    body.set("TSERVER", Json.newObject());
    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "POST", url, defaultUser.createAuthToken(), body);
    AssertHelper.assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    AssertHelper.assertJsonEqual(body, json);
  }

  @Test
  public void testListAllGFlags() {
    String gflagName = "cdc";
    String url =
        "/api/v1/metadata"
            + "/version/1.1.1.1-b78"
            + "/list_gflags?name="
            + gflagName
            + "&server=MASTER"
            + "&mostUsedGFlag=false";
    Result result = FakeApiHelper.doRequestWithAuthToken("GET", url, defaultUser.createAuthToken());
    AssertHelper.assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertNotNull(json);
  }
}
