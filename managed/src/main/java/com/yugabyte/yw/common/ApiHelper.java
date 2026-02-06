// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import java.io.IOException;
import java.net.HttpURLConnection;
import java.net.URL;
import java.time.Duration;
import java.util.*;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import javax.annotation.Nullable;
import lombok.Getter;
import lombok.ToString;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.MapUtils;
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

  @Getter
  @ToString
  /** Response class for HTTP requests. */
  public static class HttpResponse {
    private final String url;
    private final int status;
    private final JsonNode body;

    private HttpResponse(String url, int status, JsonNode body) {
      this.url = url;
      this.status = status;
      this.body = body;
    }

    public JsonNode getBodyOrThrow() {
      if (status >= 200 && status < 300) {
        return body;
      }
      throw new PlatformServiceException(
          status, String.format("HTTP request to %s failed with status %d", url, status));
    }
  }

  // Common helper method to create WSRequest with headers.
  private WSRequest requestWithHeaders(
      String url,
      @Nullable Map<String, String> headers,
      @Nullable Map<String, String> queryParams,
      @Nullable Duration timeout) {
    WSRequest request = wsClient.url(url);
    request.setFollowRedirects(true);
    if (timeout != null && timeout.compareTo(Duration.ZERO) > 0) {
      request.setRequestTimeout(timeout);
    }
    if (MapUtils.isNotEmpty(headers)) {
      for (Map.Entry<String, String> entry : headers.entrySet()) {
        request.addHeader(entry.getKey(), entry.getValue());
      }
    }
    if (MapUtils.isNotEmpty(queryParams)) {
      for (Map.Entry<String, String> entry : queryParams.entrySet()) {
        request.addQueryParameter(entry.getKey(), entry.getValue());
      }
    }
    return request;
  }

  // Common helper method to invoke the processor function.
  private HttpResponse handleHttpRequest(
      WSRequest request, Function<WSRequest, CompletionStage<WSResponse>> processor) {
    CompletionStage<HttpResponse> responsePromise =
        processor
            .apply(request)
            .thenApply(
                r -> {
                  JsonNode body = Json.newObject();
                  try {
                    body = Json.parse(r.getBody());
                  } catch (RuntimeException e) {
                    // Suppress and report as HTTP status.
                    log.warn(
                        "Unexpected exception while parsing response body {} from {} - {}",
                        r.getBody(),
                        request.getUrl(),
                        e.getMessage());
                  }
                  return new HttpResponse(request.getUrl(), r.getStatus(), body);
                });
    try {
      return responsePromise.toCompletableFuture().get();
    } catch (Exception e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  public HttpResponse postHttpRequest(
      String url,
      JsonNode data,
      @Nullable Map<String, String> headers,
      @Nullable Duration timeout) {
    return handleHttpRequest(
        requestWithHeaders(url, headers, null /* queryParams */, timeout), r -> r.post(data));
  }

  public boolean postRequest(String url, @Nullable Map<String, String> headers) {
    try {
      return requestWithHeaders(url, headers, null /* queryParams */, null /* timeout */)
          .execute("POST")
          .thenApply(wsResponse -> wsResponse.getStatus() == 200)
          .toCompletableFuture()
          .get();
    } catch (Exception e) {
      log.error("POST request to {} failed: {}", url, e.getMessage());
      return false;
    }
  }

  public JsonNode postRequest(String url, JsonNode data, @Nullable Map<String, String> headers) {
    return handleHttpRequest(
            requestWithHeaders(url, headers, null /* queryParams */, null /* timeout */),
            r -> r.post(data))
        .getBodyOrThrow();
  }

  public JsonNode postRequestEncodedData(
      String url, String encodedData, @Nullable Map<String, String> headers) {
    return handleHttpRequest(
            requestWithHeaders(url, headers, null /* queryParams */, null /* timeout */),
            r -> r.post(encodedData))
        .getBodyOrThrow();
  }

  public HttpResponse putHttpRequest(
      String url,
      JsonNode data,
      @Nullable Map<String, String> headers,
      @Nullable Duration timeout) {
    return handleHttpRequest(
        requestWithHeaders(url, headers, null /* queryParams */, timeout), r -> r.put(data));
  }

  public JsonNode putRequest(String url, JsonNode data) {
    return putRequest(url, data, null);
  }

  public JsonNode putRequest(String url, JsonNode data, @Nullable Map<String, String> headers) {
    return handleHttpRequest(
            requestWithHeaders(url, headers, null /* queryParams */, null /* timeout */),
            r -> r.put(data))
        .getBodyOrThrow();
  }

  // Helper method to create URL object for a given web page string.
  @VisibleForTesting
  URL getUrl(String url) {
    try {
      return new URL(url);
    } catch (Exception e) {
      log.error("Invalid url: {} - {}", url, e.getMessage());
      return null;
    }
  }

  // Helper function to get the full body of the webpage via an http request to the given url.
  public String getBody(String url) {
    return getBody(url, new HashMap<>(), DEFAULT_GET_REQUEST_TIMEOUT);
  }

  private String getBody(
      String url, @Nullable Map<String, String> headers, @Nullable Duration timeout) {
    WSRequest request = requestWithHeaders(url, headers, null /* queryParams */, timeout);
    CompletionStage<String> jsonPromise = request.get().thenApply(WSResponse::getBody);
    String pageText = null;
    try {
      pageText = jsonPromise.toCompletableFuture().get();
    } catch (Exception e) {
      pageText = e.getMessage();
      log.error("Error occurred", e);
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

  public HttpResponse getHttpRequest(
      String url,
      @Nullable Map<String, String> headers,
      @Nullable Map<String, String> queryParams,
      @Nullable Duration timeout) {
    return handleHttpRequest(requestWithHeaders(url, headers, queryParams, timeout), r -> r.get());
  }

  public JsonNode getRequest(String url) {
    return getRequest(url, null);
  }

  public JsonNode getRequest(String url, @Nullable Map<String, String> headers) {
    return getRequest(url, headers, null);
  }

  public JsonNode getRequest(
      String url, @Nullable Map<String, String> headers, @Nullable Map<String, String> params) {
    return getRequest(url, headers, params, null);
  }

  public JsonNode getRequest(
      String url,
      @Nullable Map<String, String> headers,
      @Nullable Map<String, String> queryParams,
      @Nullable Duration timeout) {
    return getHttpRequest(url, headers, queryParams, timeout).getBodyOrThrow();
  }

  public HttpResponse deleteHttpRequest(
      String url, @Nullable Map<String, String> headers, @Nullable Map<String, String> params) {
    return handleHttpRequest(
        requestWithHeaders(url, headers, params, null /* timeout */), r -> r.delete());
  }

  public JsonNode deleteRequest(String url) {
    return deleteRequest(url, null);
  }

  public JsonNode deleteRequest(String url, @Nullable Map<String, String> headers) {
    return deleteRequest(url, headers, new HashMap<>());
  }

  public JsonNode deleteRequest(
      String url, @Nullable Map<String, String> headers, @Nullable Map<String, String> params) {
    return deleteHttpRequest(url, headers, params).getBodyOrThrow();
  }

  public String buildUrl(String baseUrl, @Nullable Map<String, String[]> queryParams) {
    if (MapUtils.isNotEmpty(queryParams)) {
      StringBuilder requestUrlBuilder = new StringBuilder(baseUrl);
      requestUrlBuilder.append("?");
      int paramsIdx = 0;
      for (Map.Entry<String, String[]> entry : queryParams.entrySet()) {
        if (paramsIdx > 0) {
          requestUrlBuilder.append("&");
        }
        requestUrlBuilder.append(entry.getKey()).append("=").append(entry.getValue()[0]);
        paramsIdx++;
      }
      return requestUrlBuilder.toString();
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

  public HttpResponse multipartHttpRequest(
      String url,
      @Nullable Map<String, String> headers,
      List<Http.MultipartFormData.Part<Source<ByteString, ?>>> partsList) {
    WSRequest request = wsClient.url(url);
    headers.forEach(request::addHeader);
    return handleHttpRequest(request, r -> r.post(Source.from(partsList)));
  }

  public JsonNode multipartRequest(
      String url,
      @Nullable Map<String, String> headers,
      List<Http.MultipartFormData.Part<Source<ByteString, ?>>> partsList) {
    return multipartHttpRequest(url, headers, partsList).getBodyOrThrow();
  }

  public CompletionStage<WSResponse> getSimpleRequest(
      String url, @Nullable Map<String, String> headers) {
    return requestWithHeaders(url, headers, null /* queryParams */, null /* timeout */).get();
  }

  public void closeClient() {
    if (this.wsClient != null) {
      try {
        this.wsClient.close();
      } catch (IOException e) {
        log.warn("Exception while closing wsClient. Ignored.", e);
      }
    }
  }
}
