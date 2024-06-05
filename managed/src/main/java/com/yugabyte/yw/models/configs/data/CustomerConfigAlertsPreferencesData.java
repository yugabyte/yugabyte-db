// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.data;

import io.swagger.annotations.ApiModelProperty;
import javax.validation.constraints.Min;
import javax.validation.constraints.Size;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = false)
public class CustomerConfigAlertsPreferencesData extends CustomerConfigAlertsData {
  @ApiModelProperty(value = "Alert email address", example = "test@example.com")
  @Size(max = 4000)
  private String alertingEmail;

  @ApiModelProperty(value = "Send alerts to YB as well as to customer")
  private boolean sendAlertsToYb = false;

  @ApiModelProperty(value = "Alert interval, in milliseconds")
  @Min(0)
  private long checkIntervalMs = 0;

  @ApiModelProperty(value = "Status update of alert interval, in milliseconds")
  @Min(0)
  private long statusUpdateIntervalMs = 0;

  @ApiModelProperty(value = "Trigger an alert only for errors")
  private Boolean reportOnlyErrors = false;

  @ApiModelProperty(value = "Period, which is used to send active alert notifications")
  @Min(0)
  private long activeAlertNotificationIntervalMs = 0;
}
