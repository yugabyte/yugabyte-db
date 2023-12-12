// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import java.net.HttpURLConnection;
import java.net.URL;
import java.time.Duration;
import java.util.*;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ExecutionException;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.pekko.stream.javadsl.Source;
import org.apache.pekko.util.ByteString;
import play.libs.Json;
import play.libs.ws.WSClient;
import play.libs.ws.WSRequest;
import play.libs.ws.WSResponse;
import play.mvc.Http;

/** Helper class API specific stuff */
@Singleton
@Slf4j
public class ApiHelper {

  private static final Duration DEFAULT_GET_REQUEST_TIMEOUT = Duration.ofSeconds(10);

  @Getter(onMethod_ = {@VisibleForTesting})
  private final WSClient wsClient;

  @Inject
  public ApiHelper(WSClient wsClient) {
    this.wsClient = wsClient;
  }

  public boolean postRequest(String url) {
    return postRequest(url, Collections.emptyMap());
  }

  public boolean postRequest(String url, Map<String, String> headers) {
    try {
      WSRequest request = wsClient.url(url);
      if (!headers.isEmpty()) {
        for (Map.Entry<String, String> entry : headers.entrySet()) {
          request.addHeader(entry.getKey(), entry.getValue());
        }
      }
      return request
          .execute("POST")
          .thenApply(wsResponse -> wsResponse.getStatus() == 200)
          .toCompletableFuture()
          .get();
    } catch (Exception e) {
      return false;
    }
  }

  public JsonNode postRequest(String url, JsonNode data) {
    return postRequest(url, data, new HashMap<>());
  }

  public JsonNode postRequest(String url, JsonNode data, Map<String, String> headers) {
    WSRequest request = requestWithHeaders(url, headers);
    CompletionStage<String> jsonPromise = request.post(data).thenApply(WSResponse::getBody);
    return handleJSONPromise(jsonPromise);
  }

  public JsonNode putRequest(String url, JsonNode data, Map<String, String> headers) {
    WSRequest request = requestWithHeaders(url, headers);
    CompletionStage<String> jsonPromise = request.put(data).thenApply(WSResponse::getBody);
    return handleJSONPromise(jsonPromise);
  }

  // Helper method to creaete url object for given webpage string.
  public URL getUrl(String url) {
    try {
      return new URL(url);
    } catch (Exception e) {
      return null;
    }
  }

  // Helper function to get the full body of the webpage via an http request to the given url.
  public String getBody(String url) {
    return getBody(url, new HashMap<>(), DEFAULT_GET_REQUEST_TIMEOUT);
  }

  public String getBody(String url, Map<String, String> headers, Duration timeout) {
    WSRequest request = requestWithHeaders(url, headers);
    request.setRequestTimeout(timeout);
    CompletionStage<String> jsonPromise = request.get().thenApply(WSResponse::getBody);
    String pageText = null;
    try {
      pageText = jsonPromise.toCompletableFuture().get();
    } catch (Exception e) {
      pageText = e.getMessage();
      e.printStackTrace();
    }
    return pageText;
  }

  // API to get the header response for a http request to the given url.
  public ObjectNode getHeaderStatus(String url) {
    ObjectNode objNode = Json.newObject();
    try {
      URL urlObj = getUrl(url);
      if (urlObj != null) {
        objNode.put("status", ((HttpURLConnection) urlObj.openConnection()).getResponseMessage());
      } else {
        objNode.put("status", "Could not connect to URL " + url);
      }
    } catch (Exception e) {
      objNode.put("status", e.getMessage());
    }

    return objNode;
  }

  public JsonNode getRequest(String url) {
    return getRequest(url, new HashMap<>());
  }

  public JsonNode getRequest(String url, Map<String, String> headers) {
    return getRequest(url, headers, new HashMap<>());
  }

  public JsonNode getRequest(String url, Map<String, String> headers, Map<String, String> params) {
    WSRequest request = requestWithHeaders(url, headers);
    request.setFollowRedirects(true);
    if (!params.isEmpty()) {
      for (Map.Entry<String, String> entry : params.entrySet()) {
        request.setQueryParameter(entry.getKey(), entry.getValue());
      }
    }
    CompletionStage<String> jsonPromise = request.get().thenApply(WSResponse::getBody);
    return handleJSONPromise(jsonPromise);
  }

  private JsonNode handleJSONPromise(CompletionStage<String> jsonPromise) {
    try {
      String jsonString = jsonPromise.toCompletableFuture().get();
      return Json.parse(jsonString);
    } catch (InterruptedException | ExecutionException e) {
      log.warn("Unexpected exception while parsing response", e);
      return ApiResponse.errorJSON(e.getMessage());
    } catch (RuntimeException e) {
      log.warn("Unexpected exception while parsing response", e);
      throw e;
    }
  }

  private WSRequest requestWithHeaders(String url, Map<String, String> headers) {
    WSRequest request = wsClient.url(url);
    if (!headers.isEmpty()) {
      for (Map.Entry<String, String> entry : headers.entrySet()) {
        request.addHeader(entry.getKey(), entry.getValue());
      }
    }
    return request;
  }

  public String buildUrl(String baseUrl, Map<String, String[]> queryParams) {
    if (queryParams.size() > 0) {
      baseUrl += "?";
      StringBuilder requestUrlBuilder = new StringBuilder(baseUrl);
      for (Map.Entry<String, String[]> entry : queryParams.entrySet()) {
        requestUrlBuilder
            .append(entry.getKey())
            .append("=")
            .append(entry.getValue()[0])
            .append("&");
      }

      baseUrl = requestUrlBuilder.toString();
      baseUrl = baseUrl.substring(0, baseUrl.length() - 1);
    }

    return baseUrl;
  }

  public String replaceProxyLinks(String responseBody, UUID universeUUID, String proxyAddr) {
    String prefix = String.format("/universes/%s/proxy/%s/", universeUUID.toString(), proxyAddr);
    return responseBody
        .replaceAll("src='/", String.format("src='%s", prefix))
        .replaceAll("src=\"/", String.format("src=\"%s", prefix))
        .replaceAll("href=\"/", String.format("href=\"%s", prefix))
        .replaceAll("href='/", String.format("href='%s", prefix))
        .replaceAll("http://", String.format("/universes/%s/proxy/", universeUUID.toString()));
  }

  public JsonNode multipartRequest(
      String url,
      Map<String, String> headers,
      List<Http.MultipartFormData.Part<Source<ByteString, ?>>> partsList) {
    WSRequest request = wsClient.url(url);
    headers.forEach(request::addHeader);
    CompletionStage<String> post =
        request.post(Source.from(partsList)).thenApply(WSResponse::getBody);
    return handleJSONPromise(post);
  }

  public CompletionStage<WSResponse> getSimpleRequest(String url, Map<String, String> headers) {
    WSRequest request = requestWithHeaders(url, headers).setFollowRedirects(true);
    return request.get();
  }
}
