// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonTypeName;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
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

  public AlertChannelWebHookParams() {
    setChannelType(ChannelType.WebHook);
  }
}
