// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;

import org.hamcrest.CoreMatchers;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Matchers;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.runners.MockitoJUnitRunner;
import play.libs.Json;
import play.libs.ws.WSClient;
import play.libs.ws.WSRequest;
import play.libs.ws.WSResponse;

import java.util.HashMap;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.mockito.Matchers.anyString;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;

@RunWith(MockitoJUnitRunner.class)
public class ApiHelperTest {
  @Mock
  WSClient mockClient;
  @Mock
  WSRequest mockRequest;
  @Mock
  WSResponse mockResponse;

  @InjectMocks
  ApiHelper apiHelper;

  @Test
  public void testGetRequestValidJSONWithUrl() {
    CompletionStage<WSResponse> mockCompletion = CompletableFuture.completedFuture(mockResponse);
    ObjectNode jsonResponse = Json.newObject();
    jsonResponse.put("Foo", "Bar");
    when(mockClient.url(anyString())).thenReturn(mockRequest);
    when(mockRequest.get()).thenReturn(mockCompletion);
    when(mockResponse.asJson()).thenReturn(jsonResponse);
    JsonNode result = apiHelper.getRequest("http://foo.com/test");
    Mockito.verify(mockClient, times(1)).url("http://foo.com/test");
    assertEquals(result.get("Foo").asText(), "Bar");
  }

  @Test
  public void testGetRequestInvalidJSONWithUrl() {
    CompletionStage<WSResponse> mockCompletion = CompletableFuture.completedFuture(mockResponse);
    when(mockClient.url(anyString())).thenReturn(mockRequest);
    when(mockRequest.get()).thenReturn(mockCompletion);
    doThrow(new RuntimeException("Incorrect JSON")).when(mockResponse).asJson();
    JsonNode result = apiHelper.getRequest("http://foo.com/test");
    Mockito.verify(mockClient, times(1)).url("http://foo.com/test");
    assertThat(result.get("error").asText(), CoreMatchers.equalTo("java.lang.RuntimeException: Incorrect JSON"));
  }

  @Test
  public void testGetRequestWithHeaders() {
    CompletionStage<WSResponse> mockCompletion = CompletableFuture.completedFuture(mockResponse);
    ObjectNode jsonResponse = Json.newObject();
    jsonResponse.put("Foo", "Bar");
    when(mockClient.url(anyString())).thenReturn(mockRequest);
    when(mockRequest.get()).thenReturn(mockCompletion);
    when(mockResponse.asJson()).thenReturn(jsonResponse);

    HashMap<String, String> headers = new HashMap<>();
    headers.put("header", "sample");
    JsonNode result = apiHelper.getRequest("http://foo.com/test", headers);
    Mockito.verify(mockClient, times(1)).url("http://foo.com/test");
    Mockito.verify(mockRequest).setHeader("header", "sample");
    assertEquals(result.get("Foo").asText(), "Bar");
  }

  @Test
  public void testGetRequestWithParams() {
    CompletionStage<WSResponse> mockCompletion = CompletableFuture.completedFuture(mockResponse);
    ObjectNode jsonResponse = Json.newObject();
    jsonResponse.put("Foo", "Bar");
    when(mockClient.url(anyString())).thenReturn(mockRequest);
    when(mockRequest.get()).thenReturn(mockCompletion);
    when(mockResponse.asJson()).thenReturn(jsonResponse);

    HashMap<String, String> params = new HashMap<>();
    params.put("param", "foo");
    JsonNode result = apiHelper.getRequest("http://foo.com/test", new HashMap<String, String>(), params);
    Mockito.verify(mockClient, times(1)).url("http://foo.com/test");
    Mockito.verify(mockRequest).setQueryParameter("param", "foo");
    assertEquals(result.get("Foo").asText(), "Bar");
  }

  @Test
  public void testPostRequestWithValidURLAndData() {
    CompletionStage<WSResponse> mockCompletion = CompletableFuture.completedFuture(mockResponse);
    when(mockClient.url(anyString())).thenReturn(mockRequest);
    ObjectNode postData = Json.newObject();
    postData.put("Foo", "Bar");
    ObjectNode jsonResponse = Json.newObject();
    jsonResponse.put("Success", true);

    when(mockRequest.get()).thenReturn(mockCompletion);
    when(mockRequest.post(Matchers.any(JsonNode.class))).thenReturn(mockCompletion);
    when(mockResponse.asJson()).thenReturn(jsonResponse);
    JsonNode result = apiHelper.postRequest("http://foo.com/test", postData);
    Mockito.verify(mockClient, times(1)).url("http://foo.com/test");
    Mockito.verify(mockRequest, times(1)).post(postData);
    assertEquals(result.get("Success").asBoolean(), true);
  }
}
