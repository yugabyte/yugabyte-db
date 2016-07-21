// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import play.libs.Json;
import play.libs.ws.WSClient;
import play.libs.ws.WSRequest;
import play.libs.ws.WSResponse;

import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ExecutionException;

/**
 * Helper class API specific stuff
 */

@Singleton
public class ApiHelper {

  @Inject
  WSClient wsClient;

  public JsonNode postRequest(String url, JsonNode data)  {
    CompletionStage<JsonNode> jsonPromise = wsClient.url(url)
      .post(data)
      .thenApply(WSResponse::asJson);

    return handleJSONPromise(jsonPromise);
  }

  public JsonNode getRequest(String url) {
    return getRequest(url, new HashMap<>());
  }

  public JsonNode getRequest(String url, Map<String, String> headers) {
    WSRequest request = requestWithHeaders(url, headers);

    CompletionStage<JsonNode> jsonPromise = request
      .get()
      .thenApply(WSResponse::asJson);
    return handleJSONPromise(jsonPromise);
  }

  private JsonNode handleJSONPromise(CompletionStage<JsonNode> jsonPromise) {
    try {
      return jsonPromise.toCompletableFuture().get();
    } catch (InterruptedException e) {
      return ApiResponse.errorJSON(e.getMessage());
    } catch (ExecutionException e) {
      return ApiResponse.errorJSON(e.getMessage());
    }
  }

  private WSRequest requestWithHeaders(String url, Map<String, String> headers) {
    WSRequest request = wsClient.url(url);

    if (!headers.isEmpty()) {
      for(Map.Entry<String, String> entry : headers.entrySet()) {
        request.setHeader(entry.getKey(), entry.getValue());
      }
    }
    return request;
  }
}
