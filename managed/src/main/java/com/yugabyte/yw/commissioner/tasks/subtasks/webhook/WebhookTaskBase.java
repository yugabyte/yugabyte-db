// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.webhook;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.forms.webhook.WebhookRequestBody;
import com.yugabyte.yw.forms.webhook.WebhookTaskParams;
import java.util.concurrent.CompletionStage;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;
import play.libs.ws.WSClient;
import play.libs.ws.WSRequest;
import play.libs.ws.WSResponse;

@Slf4j
public abstract class WebhookTaskBase extends AbstractTaskBase {

  public static final String WS_CLIENT_KEY = "yb.webhook.ws";
  private final WSClientRefresher wsClientRefresher;

  @Inject
  protected WebhookTaskBase(
      WSClientRefresher wsClientRefresher, BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
    this.wsClientRefresher = wsClientRefresher;
  }

  @Override
  protected WebhookTaskParams taskParams() {
    return (WebhookTaskParams) taskParams;
  }

  public WSResponse send(WebhookRequestBody body) throws Exception {
    WSClient wsClient = wsClientRefresher.getClient(WS_CLIENT_KEY);
    WSRequest request =
        wsClient
            .url(taskParams().hook.getUrl())
            .addHeader("Content-Type", "application/json")
            .addHeader("Accept", "application/json");

    log.debug(
        "Sending webhook request for url: {} with request body: {}",
        taskParams().hook.getUrl(),
        Json.toJson(body));
    CompletionStage<WSResponse> promise = request.post(Json.toJson(body));
    return promise.toCompletableFuture().get();
  }

  private ApiHelper getApiHelper() {
    WSClient wsClient = wsClientRefresher.getClient(WS_CLIENT_KEY);
    return new ApiHelper(wsClient, wsClientRefresher.getMaterializer());
  }
}
