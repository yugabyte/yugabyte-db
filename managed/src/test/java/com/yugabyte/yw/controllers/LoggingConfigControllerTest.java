package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
public class LoggingConfigControllerTest extends FakeDBApplication {

  @Test
  public void testSetAuditLoggingSettingsWithInvalidDateFormat() {

    String uri = "/api/audit_logging_config";
    ObjectNode body = Json.newObject();
    body.put("outputToStdout", false);
    body.put("outputToFile", true);
    body.put("maxHistory", 30);
    body.put("rolloverPattern", "xxxxxxx");

    Result result = assertPlatformException(() -> doRequestWithBody("POST", uri, body));
    JsonNode node = Json.parse(contentAsString(result));
    assertErrorNodeValue(node, "rolloverPattern", "Incorrect pattern");
  }

  @Test
  public void testSetLoggingSettingsWithMalformedMaxFileSize() {
    ObjectNode body = Json.newObject();
    body.put("level", "DEBUG");
    body.put("maxFileSize", "512 potatoes");

    Result result =
        assertPlatformException(() -> doRequestWithBody("POST", "/api/logging_config", body));
    assertBadRequest(result, "Incorrect maxFileSize 512 potatoes");
  }

  @Test
  public void testSetLoggingSettingsWithMalformedTotalSizeCap() {
    ObjectNode body = Json.newObject();
    body.put("level", "DEBUG");
    body.put("totalSizeCap", "lots");

    Result result =
        assertPlatformException(() -> doRequestWithBody("POST", "/api/logging_config", body));
    assertBadRequest(result, "Incorrect totalSizeCap lots");
  }

  @Test
  public void testSetLoggingSettingsWithTotalSizeCapBelowMaxFileSize() {
    ObjectNode body = Json.newObject();
    body.put("level", "DEBUG");
    body.put("maxFileSize", "8GB");
    body.put("totalSizeCap", "6GB");

    Result result =
        assertPlatformException(() -> doRequestWithBody("POST", "/api/logging_config", body));
    assertBadRequest(result, "totalSizeCap 6GB must not be smaller than maxFileSize 8GB");
  }

  @Test
  @Parameters({"0", "255MB"})
  public void testSetLoggingSettingsWithMaxFileSizeBelowMinimum(String maxFileSize) {
    ObjectNode body = Json.newObject();
    body.put("level", "DEBUG");
    body.put("maxFileSize", maxFileSize);

    Result result =
        assertPlatformException(() -> doRequestWithBody("POST", "/api/logging_config", body));
    assertBadRequest(result, "maxFileSize " + maxFileSize + " must be at least 256MB");
  }

  // logback treats a 0 cap as unbounded.
  @Test
  @Parameters({"0", "4GB"})
  public void testSetLoggingSettingsWithTotalSizeCapBelowMinimum(String totalSizeCap) {
    ObjectNode body = Json.newObject();
    body.put("level", "DEBUG");
    body.put("totalSizeCap", totalSizeCap);

    Result result =
        assertPlatformException(() -> doRequestWithBody("POST", "/api/logging_config", body));
    assertBadRequest(result, "totalSizeCap " + totalSizeCap + " must be at least 5GB");
  }

  @Test
  public void testSetLoggingSettingsWithTotalSizeCapBelowSavedMaxFileSize() {
    mutableConfigFactory.globalRuntimeConf().setValue("yb.logging.max_file_size", "8GB");
    ObjectNode body = Json.newObject();
    body.put("level", "DEBUG");
    body.put("totalSizeCap", "6GB");

    Result result =
        assertPlatformException(() -> doRequestWithBody("POST", "/api/logging_config", body));
    assertBadRequest(result, "totalSizeCap 6GB must not be smaller than maxFileSize 8GB");
  }
}
