// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.common.alerts.AlertTemplateVariableService;
import com.yugabyte.yw.models.helpers.auth.HttpAuth;
import com.yugabyte.yw.models.helpers.auth.TokenAuth;
import com.yugabyte.yw.models.helpers.auth.UsernamePasswordAuth;
import java.util.concurrent.CompletionStage;
import play.libs.Json;
import play.libs.ws.WSAuthScheme;
import play.libs.ws.WSClient;
import play.libs.ws.WSRequest;
import play.libs.ws.WSResponse;

abstract class AlertChannelWebBase extends AlertChannelBase {

  private final WSClientRefresher wsClientRefresher;

  public AlertChannelWebBase(
      WSClientRefresher wsClientRefresher,
      AlertTemplateVariableService alertTemplateVariableService) {
    super(alertTemplateVariableService);
    this.wsClientRefresher = wsClientRefresher;
  }

  protected WSResponse sendRequest(String clientKey, String url, Object body) throws Exception {
    return sendRequest(clientKey, url, body, null);
  }

  protected WSResponse sendRequest(String clientKey, String url, Object body, HttpAuth httpAuth)
      throws Exception {
    WSClient wsClient = wsClientRefresher.getClient(clientKey);
    WSRequest request =
        wsClient
            .url(url)
            .addHeader("Content-Type", "application/json")
            .addHeader("Accept", "application/json");
    if (httpAuth != null) {
      switch (httpAuth.getType()) {
        case BASIC:
          UsernamePasswordAuth usernamePasswordAuth = (UsernamePasswordAuth) httpAuth;
          request.setAuth(
              usernamePasswordAuth.getUsername(),
              usernamePasswordAuth.getPassword(),
              WSAuthScheme.valueOf(usernamePasswordAuth.getType().name()));
          break;
        case TOKEN:
          TokenAuth tokenAuth = (TokenAuth) httpAuth;
          request.addHeader(tokenAuth.getTokenHeader(), tokenAuth.getTokenValue());
          break;
        default:
          // NONE
      }
    }
    CompletionStage<WSResponse> promise = request.post(Json.toJson(body));
    return promise.toCompletableFuture().get();
  }
}
