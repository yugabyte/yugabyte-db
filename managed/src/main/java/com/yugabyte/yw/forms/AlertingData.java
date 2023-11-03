/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;

@ApiModel(description = "Alerting configuration")
public class AlertingData {

  @ApiModelProperty(value = "Alert email address", example = "test@example.com")
  public String alertingEmail;

  @ApiModelProperty(value = "Send alerts to YB as well as to customer")
  public boolean sendAlertsToYb = false;

  @ApiModelProperty(value = "Alert interval, in milliseconds")
  public long checkIntervalMs = 0;

  @ApiModelProperty(value = "Status update of alert interval, in milliseconds")
  public long statusUpdateIntervalMs = 0;

  @ApiModelProperty(value = "Trigger an alert only for errors")
  public Boolean reportOnlyErrors = false;

  @ApiModelProperty(value = "Period, which is used to send active alert notifications")
  public long activeAlertNotificationIntervalMs = 0;
}
