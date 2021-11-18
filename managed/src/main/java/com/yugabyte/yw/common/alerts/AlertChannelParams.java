// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import com.fasterxml.jackson.annotation.JsonSubTypes;
import com.fasterxml.jackson.annotation.JsonTypeInfo;
import com.fasterxml.jackson.annotation.JsonTypeInfo.As;
import com.yugabyte.yw.common.BeanValidator;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode
@JsonTypeInfo(use = JsonTypeInfo.Id.NAME, include = As.PROPERTY, property = "channelType")
@JsonSubTypes({
  @JsonSubTypes.Type(value = AlertChannelEmailParams.class, name = "Email"),
  @JsonSubTypes.Type(value = AlertChannelSlackParams.class, name = "Slack"),
  @JsonSubTypes.Type(value = AlertChannelPagerDutyParams.class, name = "PagerDuty"),
  @JsonSubTypes.Type(value = AlertChannelWebHookParams.class, name = "WebHook")
})
public class AlertChannelParams {
  // Specifies template string for the notification title.
  // If null then template from the alert is used (?).
  private String titleTemplate;

  // Specifies template string for the notification text/body/description.
  // If null then template from the alert is used.
  private String textTemplate;

  public void validate(BeanValidator validator) {
    // Nothing to check yet.
  }
}
