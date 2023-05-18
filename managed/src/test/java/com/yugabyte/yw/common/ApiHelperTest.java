// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.startsWith;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.io.IOException;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLConnection;
import java.net.URLStreamHandler;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentMatchers;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.libs.ws.WSClient;
import play.libs.ws.WSRequest;
import play.libs.ws.WSResponse;

@RunWith(MockitoJUnitRunner.class)
public class ApiHelperTest {
  @Mock WSClient mockClient;
  @Mock WSRequest mockRequest;
  @Mock WSResponse mockResponse;
  @Mock HttpURLConnection mockConnection;

  @InjectMocks ApiHelper apiHelper;

  @Test
  public void testGetRequestValidJSONWithUrl() {
    CompletionStage<WSResponse> mockCompletion = CompletableFuture.completedFuture(mockResponse);
    ObjectNode jsonResponse = Json.newObject();
    jsonResponse.put("Foo", "Bar");
    when(mockClient.url(anyString())).thenReturn(mockRequest);
    when(mockRequest.get()).thenReturn(mockCompletion);
    when(mockResponse.getBody()).thenReturn(jsonResponse.toString());
    JsonNode result = apiHelper.getRequest("http://foo.com/test");
    Mockito.verify(mockClient, times(1)).url("http://foo.com/test");
    assertEquals(result.get("Foo").asText(), "Bar");
  }

  @Test
  public void testGetRequestInvalidJSONWithUrl() {
    CompletionStage<WSResponse> mockCompletion = CompletableFuture.completedFuture(mockResponse);
    when(mockClient.url(anyString())).thenReturn(mockRequest);
    when(mockRequest.get()).thenReturn(mockCompletion);
    doReturn("Incorrect JSON").when(mockResponse).getBody();
    RuntimeException exception =
        assertThrows(RuntimeException.class, () -> apiHelper.getRequest("http://foo.com/test"));
    assertThat(exception.getMessage(), startsWith("com.fasterxml.jackson.core.JsonParseException"));
  }

  @Test
  public void testGetRequestWithHeaders() {
    CompletionStage<WSResponse> mockCompletion = CompletableFuture.completedFuture(mockResponse);
    ObjectNode jsonResponse = Json.newObject();
    jsonResponse.put("Foo", "Bar");
    when(mockClient.url(anyString())).thenReturn(mockRequest);
    when(mockRequest.get()).thenReturn(mockCompletion);
    when(mockResponse.getBody()).thenReturn(jsonResponse.toString());

    HashMap<String, String> headers = new HashMap<>();
    headers.put("header", "sample");
    JsonNode result = apiHelper.getRequest("http://foo.com/test", headers);
    Mockito.verify(mockClient, times(1)).url("http://foo.com/test");
    Mockito.verify(mockRequest).addHeader("header", "sample");
    Mockito.verify(mockRequest).setFollowRedirects(true);
    assertEquals(result.get("Foo").asText(), "Bar");
  }

  @Test
  public void testGetRequestWithParams() {
    CompletionStage<WSResponse> mockCompletion = CompletableFuture.completedFuture(mockResponse);
    ObjectNode jsonResponse = Json.newObject();
    jsonResponse.put("Foo", "Bar");
    when(mockClient.url(anyString())).thenReturn(mockRequest);
    when(mockRequest.get()).thenReturn(mockCompletion);
    when(mockResponse.getBody()).thenReturn(jsonResponse.toString());

    HashMap<String, String> params = new HashMap<>();
    params.put("param", "foo");
    JsonNode result =
        apiHelper.getRequest("http://foo.com/test", new HashMap<String, String>(), params);
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

    when(mockRequest.post(ArgumentMatchers.any(JsonNode.class))).thenReturn(mockCompletion);
    when(mockResponse.getBody()).thenReturn(jsonResponse.toString());
    JsonNode result = apiHelper.postRequest("http://foo.com/test", postData);
    Mockito.verify(mockClient, times(1)).url("http://foo.com/test");
    Mockito.verify(mockRequest, times(1)).post(postData);
    assertEquals(result.get("Success").asBoolean(), true);
  }

  private void testGetHeaderRequestHelper(String urlPath, boolean isSuccess) {
    final URLStreamHandler handler =
        new URLStreamHandler() {
          @Override
          protected URLConnection openConnection(final URL arg0) throws IOException {
            return mockConnection;
          }
        };
    ApiHelper mockApiHelper = spy(apiHelper);
    try {
      when(mockConnection.getResponseMessage()).thenReturn(isSuccess ? "OK" : "Not Found");
      URL url = new URL(null, urlPath, handler);
      when(mockApiHelper.getUrl(urlPath)).thenReturn(url);
    } catch (Exception e) {
      e.printStackTrace();
      assertNull(e.getMessage());
    }
    ObjectNode result = mockApiHelper.getHeaderStatus(urlPath);
    if (isSuccess) {
      assertEquals(result.get("status").asText(), "OK");
    } else {
      assertEquals(result.get("status").asText(), "Not Found");
    }
  }

  @Test
  public void testGetHeaderRequestOKL() {
    testGetHeaderRequestHelper("http://www.yugabyte.com", true);
  }

  @Test
  public void testGetHeaderRequestNonOK() {
    testGetHeaderRequestHelper("http://www.yugabyte.com", false);
  }

  @Test
  public void testGetHeaderRequestWithInvalidURL() {
    testGetHeaderRequestHelper("file:///my/yugabyte/com", false);
  }

  @Test
  public void testBuildUrl() {
    ApiHelper mockApiHelper = spy(apiHelper);
    String baseUrl = "http://test.com";
    String expectedResult = String.format("%s?testKey2=test2&testKey1=test1", baseUrl);
    String[] values1 = {"test1"};
    String[] values2 = {"test2"};
    Map<String, String[]> queryParams = new HashMap<>();
    queryParams.put("testKey1", values1);
    queryParams.put("testKey2", values2);
    assertEquals(mockApiHelper.buildUrl(baseUrl, queryParams), expectedResult);
  }
}
