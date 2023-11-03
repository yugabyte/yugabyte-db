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
import org.hibernate.validator.constraints.URL;

@Data
@EqualsAndHashCode(callSuper = false)
@JsonTypeName("Slack")
@ApiModel(parent = AlertChannelParams.class)
public class AlertChannelSlackParams extends AlertChannelParams {

  @NotNull
  @Size(min = 1)
  @ApiModelProperty(value = "Slack username", accessMode = READ_WRITE)
  private String username;

  @ApiModelProperty(value = "Slack webhook URL", accessMode = READ_WRITE)
  @NotNull
  @URL
  private String webhookUrl;

  @ApiModelProperty(value = "Slack icon URL", accessMode = READ_WRITE)
  @URL
  private String iconUrl;

  public AlertChannelSlackParams() {
    setChannelType(ChannelType.Slack);
  }
}
