// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.webhook;

import com.yugabyte.yw.models.Webhook;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;

@ApiModel(description = "Webhook get response")
public class GetWebhookResponse {

  private final Webhook webhook;

  public GetWebhookResponse(Webhook webhook) {
    this.webhook = webhook;
  }

  @ApiModelProperty(value = "Webhook UUID")
  public UUID getUuid() {
    return webhook.getUuid();
  }

  @ApiModelProperty(value = "Webhook url")
  public String getUrl() {
    return webhook.getUrl();
  }
}
