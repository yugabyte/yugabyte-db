// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import com.fasterxml.jackson.annotation.JsonTypeName;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = false)
@JsonTypeName("PagerDuty")
public class AlertChannelPagerDutyParams extends AlertChannelParams {
  @NotNull
  @Size(min = 1)
  private String apiKey;

  @NotNull
  @Size(min = 1)
  private String routingKey;
}
