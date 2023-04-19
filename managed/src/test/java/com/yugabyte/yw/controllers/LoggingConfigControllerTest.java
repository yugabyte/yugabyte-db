package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

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
}
