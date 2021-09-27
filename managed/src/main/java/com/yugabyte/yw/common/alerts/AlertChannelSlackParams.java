// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import com.fasterxml.jackson.annotation.JsonTypeName;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.EqualsAndHashCode;
import lombok.ToString;
import org.hibernate.validator.constraints.URL;

@EqualsAndHashCode(callSuper = false)
@ToString
@JsonTypeName("Slack")
public class AlertChannelSlackParams extends AlertChannelParams {

  @NotNull
  @Size(min = 1)
  public String username;

  @NotNull @URL public String webhookUrl;

  @URL public String iconUrl;
}
