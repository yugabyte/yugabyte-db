// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonTypeName;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.helpers.auth.HttpAuth;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import javax.validation.constraints.NotNull;
import lombok.Data;
import lombok.EqualsAndHashCode;
import org.hibernate.validator.constraints.URL;

@Data
@EqualsAndHashCode(callSuper = false)
@JsonTypeName("WebHook")
@ApiModel(parent = AlertChannelParams.class)
public class AlertChannelWebHookParams extends AlertChannelParams {
  @ApiModelProperty(value = "Webhook URL", accessMode = READ_WRITE)
  @NotNull
  @URL
  private String webhookUrl;

  @ApiModelProperty(value = "Authentication Details", accessMode = READ_WRITE)
  private HttpAuth httpAuth;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. " + "Send resolved alert notification",
      accessMode = READ_WRITE)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.0.0")
  private boolean sendResolved = true;

  public AlertChannelWebHookParams() {
    setChannelType(ChannelType.WebHook);
  }
}
