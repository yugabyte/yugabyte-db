package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertNotNull;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static play.test.Helpers.contentAsString;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
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
    ArrayNode gflags = Json.newArray();
    ObjectNode flag1 = Json.newObject();
    flag1.put("name", "cdc_enable_replicate_intents");
    flag1.put("MASTER", "true");
    flag1.put("TSERVER", "true");
    ObjectNode flag2 = Json.newObject();
    flag2.put("name", "update_metrics_interval_ms");
    flag2.put("MASTER", "string");
    flag2.put("TSERVER", "15000");
    gflags.add(flag1);
    gflags.add(flag2);
    body.set("gflags", gflags);
    String url = "/api/v1/metadata" + "/version/1.1.1.1-b11" + "/validate_gflags";
    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "POST", url, defaultUser.createAuthToken(), body);
    AssertHelper.assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));

    ObjectNode expectedJson = Json.newObject();
    ObjectNode expectedFlag1Json = Json.newObject();
    expectedFlag1Json.put("name", "cdc_enable_replicate_intents");
    ObjectNode flag1MasterJson = Json.newObject();
    flag1MasterJson.put("exist", "true");
    expectedFlag1Json.set("MASTER", flag1MasterJson);
    ObjectNode flag1TserverJson = Json.newObject();
    flag1TserverJson.put("exist", "true");
    expectedJson.arrayNode().add(expectedFlag1Json);
    ObjectNode expectedFlag2Json = Json.newObject();
    expectedFlag2Json.put("name", "cdc_enable_replicate_intents");
    ObjectNode flag2MasterJson = Json.newObject();
    flag2MasterJson.put("exist", "true");
    expectedFlag2Json.set("MASTER", flag1MasterJson);
    ObjectNode flag2TserverJson = Json.newObject();
    flag2TserverJson.put("exist", "true");
    expectedJson.arrayNode().add(expectedFlag2Json);
    AssertHelper.assertJsonEqual(expectedJson, json);
  }

  @Test
  public void testValidateGFlagWithInvalidDatatype() {
    ObjectNode body = Json.newObject();
    ArrayNode gflags = Json.newArray();
    ObjectNode flag1 = Json.newObject();
    flag1.put("name", "cdc_enable_replicate_intents");
    flag1.put("MASTER", "string");
    flag1.put("TSERVER", "true");
    ObjectNode flag2 = Json.newObject();
    flag2.put("name", "update_metrics_interval_ms");
    flag2.put("MASTER", "string");
    flag2.put("TSERVER", "15000");
    gflags.add(flag1);
    gflags.add(flag2);
    body.set("gflags", gflags);
    String url = "/api/v1/metadata" + "/version/1.1.1.1-b11" + "/validate_gflags";
    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "POST", url, defaultUser.createAuthToken(), body);
    AssertHelper.assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));

    ObjectNode expectedJson = Json.newObject();
    ObjectNode expectedFlag1Json = Json.newObject();
    expectedFlag1Json.put("name", "cdc_enable_replicate_intents");
    ObjectNode flag1MasterJson = Json.newObject();
    flag1MasterJson.put("exist", "true");
    flag1MasterJson.put("error", "Given string is not a bool type");
    expectedFlag1Json.set("MASTER", flag1MasterJson);
    ObjectNode flag1TserverJson = Json.newObject();
    flag1TserverJson.put("exist", "true");
    flag1TserverJson.put("error", "Given string is not a int32 type");
    expectedJson.arrayNode().add(expectedFlag1Json);
    ObjectNode expectedFlag2Json = Json.newObject();
    expectedFlag2Json.put("name", "cdc_enable_replicate_intents");
    ObjectNode flag2MasterJson = Json.newObject();
    flag2MasterJson.put("exist", "true");
    expectedFlag2Json.set("MASTER", flag1MasterJson);
    ObjectNode flag2TserverJson = Json.newObject();
    flag2TserverJson.put("exist", "true");
    expectedJson.arrayNode().add(expectedFlag2Json);
    AssertHelper.assertJsonEqual(expectedJson, json);
  }

  @Test
  public void testValiadtedGFlagWithIncorrectGFlagName() {
    String gflagName = "invalid_gflag";
    ObjectNode body = Json.newObject();
    ArrayNode gflags = Json.newArray();
    ObjectNode flag1 = Json.newObject();
    flag1.put("name", gflagName);
    flag1.put("MASTER", "123");
    flag1.put("TSERVER", "123");
    ObjectNode flag2 = Json.newObject();
    flag2.put("name", "update_metrics_interval_ms");
    flag2.put("MASTER", "string");
    flag2.put("TSERVER", "15000");
    gflags.add(flag1);
    gflags.add(flag2);
    body.set("gflags", gflags);
    String url = "/api/v1/metadata" + "/version/1.1.1.1-b11" + "/validate_gflags";
    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "POST", url, defaultUser.createAuthToken(), body);
    AssertHelper.assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));

    ObjectNode expectedJson = Json.newObject();
    ObjectNode expectedFlag1Json = Json.newObject();
    expectedFlag1Json.put("name", gflagName);
    ObjectNode flag1MasterJson = Json.newObject();
    flag1MasterJson.put("exist", "false");
    expectedFlag1Json.set("MASTER", flag1MasterJson);
    ObjectNode flag1TserverJson = Json.newObject();
    flag1TserverJson.put("exist", "false");
    expectedJson.arrayNode().add(expectedFlag1Json);
    ObjectNode expectedFlag2Json = Json.newObject();
    expectedFlag2Json.put("name", "cdc_enable_replicate_intents");
    ObjectNode flag2MasterJson = Json.newObject();
    flag2MasterJson.put("exist", "true");
    expectedFlag2Json.set("MASTER", flag1MasterJson);
    ObjectNode flag2TserverJson = Json.newObject();
    flag2TserverJson.put("exist", "true");
    expectedJson.arrayNode().add(expectedFlag2Json);
    AssertHelper.assertJsonEqual(expectedJson, json);
  }

  @Test
  public void testValidateGFlagWithEmptyParams() {
    String url = "/api/v1/metadata" + "/version/1.1.1.1-b89" + "/validate_gflags";
    ObjectNode body = Json.newObject();
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", url, defaultUser.createAuthToken(), body));
    AssertHelper.assertBadRequest(result, "Please provide a valid list of gflags.");
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
