// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonTypeName;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = false)
@JsonTypeName("PagerDuty")
@ApiModel(parent = AlertChannelParams.class)
public class AlertChannelPagerDutyParams extends AlertChannelParams {
  @NotNull
  @Size(min = 1)
  @ApiModelProperty(value = "API key", accessMode = READ_WRITE)
  private String apiKey;

  @NotNull
  @Size(min = 1)
  @ApiModelProperty(value = "Routing key", accessMode = READ_WRITE)
  private String routingKey;

  public AlertChannelPagerDutyParams() {
    setChannelType(ChannelType.PagerDuty);
  }
}
