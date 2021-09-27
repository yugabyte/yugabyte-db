// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import com.fasterxml.jackson.annotation.JsonSubTypes;
import com.fasterxml.jackson.annotation.JsonTypeInfo;
import com.fasterxml.jackson.annotation.JsonTypeInfo.As;
import com.yugabyte.yw.common.BeanValidator;
import lombok.EqualsAndHashCode;
import lombok.ToString;

@EqualsAndHashCode
@ToString
@JsonTypeInfo(use = JsonTypeInfo.Id.NAME, include = As.PROPERTY, property = "channelType")
@JsonSubTypes({
  @JsonSubTypes.Type(value = AlertChannelEmailParams.class, name = "Email"),
  @JsonSubTypes.Type(value = AlertChannelSlackParams.class, name = "Slack")
})
public class AlertChannelParams {
  // Specifies template string for the notification title.
  // If null then template from the alert is used (?).
  public String titleTemplate;

  // Specifies template string for the notification text/body/description.
  // If null then template from the alert is used.
  public String textTemplate;

  public void validate(BeanValidator validator) {
    // Nothing to check yet.
  }
}
