// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import play.libs.Json;
import play.mvc.Result;

import java.util.List;

import static org.hamcrest.CoreMatchers.*;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.*;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

public class AssertHelper {
  public static void assertOk(Result result) {
        assertEquals(OK, result.status());
    }

  public static void assertBadRequest(Result result, String errorStr) {
    assertEquals(BAD_REQUEST, result.status());
    assertErrorResponse(result, errorStr);
  }

  public static void assertInternalServerError(Result result, String errorStr) {
    assertEquals(INTERNAL_SERVER_ERROR, result.status());
    assertErrorResponse(result, errorStr);
  }

  public static void assertErrorResponse(Result result, String errorStr) {
    if (errorStr != null) {
        JsonNode json = Json.parse(contentAsString(result));
        assertThat(json.get("error").toString(), allOf(notNullValue(),
                containsString(errorStr)));
    }
  }

  public static void assertValue(JsonNode json, String key, String value) {
    JsonNode targetNode = json.path(key);
    assertFalse(targetNode.isMissingNode());
    assertEquals(targetNode.asText(), value);
  }

  public static void assertValues(JsonNode json, String key, List<String> values) {
    json.findValues(key).forEach((node) -> assertTrue(values.contains(node.asText())));
  }

  public static void assertErrorNodeValue(JsonNode json, String key, String value) {
    JsonNode errorJson = json.get("error");
    assertNotNull(errorJson);
    if (key == null) {
      assertThat(errorJson.asText(), allOf(notNullValue(), equalTo(value)));
    } else {
      assertThat(errorJson.get(key).get(0).asText(), allOf(notNullValue(), equalTo(value)));
    }
  }

  public static void assertErrorNodeValue(JsonNode json, String value) {
    assertErrorNodeValue(json, null, value);
  }
}
