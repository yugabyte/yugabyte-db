// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import com.yugabyte.yw.common.WSClientRefresher;
import java.util.concurrent.CompletionStage;
import play.libs.Json;
import play.libs.ws.WSClient;
import play.libs.ws.WSResponse;

abstract class AlertChannelWebBase extends AlertChannelBase {

  private final WSClientRefresher wsClientRefresher;

  public AlertChannelWebBase(WSClientRefresher wsClientRefresher) {
    this.wsClientRefresher = wsClientRefresher;
  }

  protected WSResponse sendRequest(String clientKey, String url, Object body) throws Exception {
    WSClient wsClient = wsClientRefresher.getClient(clientKey);
    CompletionStage<WSResponse> promise =
        wsClient
            .url(url)
            .addHeader("Content-Type", "application/json")
            .addHeader("Accept", "application/json")
            .post(Json.toJson(body));
    return promise.toCompletableFuture().get();
  }
}
