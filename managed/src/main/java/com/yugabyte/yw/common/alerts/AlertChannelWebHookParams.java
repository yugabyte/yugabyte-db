// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import com.fasterxml.jackson.annotation.JsonTypeName;
import javax.validation.constraints.NotNull;
import lombok.Data;
import lombok.EqualsAndHashCode;
import org.hibernate.validator.constraints.URL;

@Data
@EqualsAndHashCode(callSuper = false)
@JsonTypeName("WebHook")
public class AlertChannelWebHookParams extends AlertChannelParams {
  @NotNull @URL private String webhookUrl;
}
