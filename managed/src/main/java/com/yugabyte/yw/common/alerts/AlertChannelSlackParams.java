// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import com.fasterxml.jackson.annotation.JsonTypeName;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.Data;
import lombok.EqualsAndHashCode;
import org.hibernate.validator.constraints.URL;

@Data
@EqualsAndHashCode(callSuper = false)
@JsonTypeName("Slack")
public class AlertChannelSlackParams extends AlertChannelParams {

  @NotNull
  @Size(min = 1)
  private String username;

  @NotNull @URL private String webhookUrl;

  @URL private String iconUrl;
}
