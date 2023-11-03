// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.core.StringContains.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.NOT_FOUND;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.UNAUTHORIZED;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricKey;
import java.util.List;
import java.util.UUID;
import org.junit.function.ThrowingRunnable;
import play.libs.Json;
import play.mvc.Result;

public class AssertHelper {
  public static void assertOk(Result result) {
    assertEquals(contentAsString(result), OK, result.status());
  }

  public static void assertBadRequest(Result result, String errorStr) {
    assertEquals(BAD_REQUEST, result.status());
    assertErrorResponse(result, errorStr);
  }

  public static void assertInternalServerError(Result result, String errorStr) {
    assertEquals(INTERNAL_SERVER_ERROR, result.status());
    if (null != errorStr) {
      assertErrorResponse(result, errorStr);
    }
  }

  public static void assertUnauthorized(Result result, String errorStr) {
    assertEquals(UNAUTHORIZED, result.status());
    assertErrorResponse(result, errorStr);
  }

  public static void assertUnauthorizedNoException(Result result, String errorStr) {
    assertEquals(UNAUTHORIZED, result.status());
    assertEquals(errorStr, contentAsString(result));
  }

  public static void assertForbidden(Result result, String errorStr) {
    assertEquals(FORBIDDEN, result.status());
    assertEquals(errorStr, contentAsString(result));
  }

  public static void assertForbiddenWithException(Result result, String errorStr) {
    assertEquals(FORBIDDEN, result.status());
    assertErrorResponse(result, errorStr);
  }

  public static void assertNotFound(Result result, String errorStr) {
    assertEquals(NOT_FOUND, result.status());
    assertErrorResponse(result, errorStr);
  }

  public static void assertConflict(Result result, String errorStr) {
    assertEquals(CONFLICT, result.status());
    assertErrorResponse(result, errorStr);
  }

  public static void assertErrorResponse(Result result, String errorStr) {
    if (errorStr != null) {
      JsonNode json = Json.parse(contentAsString(result));
      assertThat(json.get("error").toString(), allOf(notNullValue(), containsString(errorStr)));
    }
  }

  public static void assertValue(JsonNode json, String key, String value) {
    JsonNode targetNode = json.path(key);
    assertFalse(targetNode.isMissingNode());
    if (targetNode.isObject()) {
      assertEquals(value, targetNode.toString());
    } else {
      assertEquals(value, targetNode.asText());
    }
  }

  public static void assertNoKey(JsonNode json, String key) {
    assertFalse(json.has(key));
  }

  // Allows specifying a JsonPath to the expected node.
  // For example "/foo/bar" locates node bar within node foo in json.
  public static void assertValueAtPath(JsonNode json, String key, String value) {
    JsonNode targetNode = json.at(key);
    assertFalse(targetNode.isMissingNode());
    if (targetNode.isObject()) {
      assertEquals(value, targetNode.toString());
    } else {
      assertEquals(value, targetNode.asText());
    }
  }

  public static void assertValues(JsonNode json, String key, List<String> values) {
    json.findValues(key).forEach((node) -> assertTrue(values.contains(node.asText())));
  }

  public static void assertArrayNode(JsonNode json, String key, List<String> expectedValues) {
    assertTrue(json.get(key).isArray());
    json.get(key).forEach((value) -> assertTrue(expectedValues.contains(value.asText())));
  }

  public static void assertErrorNodeValue(JsonNode json, String key, String value) {
    JsonNode errorJson = json.get("error");
    assertNotNull(errorJson);
    if (key == null) {
      assertThat(errorJson.toString(), errorJson.asText(), allOf(notNullValue(), equalTo(value)));
    } else {
      assertThat(errorJson.toString() + "[" + key + "]", errorJson.get(key), is(notNullValue()));
      assertThat(
          errorJson.toString(),
          errorJson.get(key).get(0).asText(),
          allOf(notNullValue(), equalTo(value)));
    }
  }

  public static void assertErrorNodeValue(JsonNode json, String value) {
    assertErrorNodeValue(json, null, value);
  }

  public static void assertJsonEqual(JsonNode expectedJson, JsonNode actualJson) {
    expectedJson
        .fieldNames()
        .forEachRemaining(field -> assertEquals(expectedJson.get(field), actualJson.get(field)));
  }

  public static void assertAuditEntry(int expectedNumEntries, UUID customerUUID) {
    int actual = Audit.getAll(customerUUID).size();
    assertEquals(expectedNumEntries, actual);
  }

  public static void assertYBPSuccess(Result result, String expectedMessage) {
    assertOk(result);
    YBPSuccess ybpSuccess = Json.fromJson(Json.parse(contentAsString(result)), YBPSuccess.class);
    assertEquals(expectedMessage, ybpSuccess.message);
  }

  /** If using @Transactional you will have to use assertPlatformExceptionInTransaction */
  public static Result assertPlatformException(ThrowingRunnable runnable) {
    return assertThrows(PlatformServiceException.class, runnable)
        .buildResult(fakeRequest().build());
  }

  public static Metric assertMetricValue(
      MetricService metricService, MetricKey metricKey, Double value) {
    Metric metric = metricService.get(metricKey);
    if (value != null) {
      assertNotNull(metric);
      assertEquals(value, metric.getValue());
    } else {
      assertNull(metric);
    }
    return metric;
  }
}
