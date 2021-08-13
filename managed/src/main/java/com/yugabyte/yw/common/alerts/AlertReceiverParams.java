// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import com.fasterxml.jackson.annotation.JsonSubTypes;
import com.fasterxml.jackson.annotation.JsonTypeInfo;
import com.fasterxml.jackson.annotation.JsonTypeInfo.As;
import lombok.EqualsAndHashCode;
import lombok.ToString;

@EqualsAndHashCode
@ToString
@JsonTypeInfo(use = JsonTypeInfo.Id.NAME, include = As.PROPERTY, property = "targetType")
@JsonSubTypes({
  @JsonSubTypes.Type(value = AlertReceiverEmailParams.class, name = "Email"),
  @JsonSubTypes.Type(value = AlertReceiverSlackParams.class, name = "Slack")
})
public class AlertReceiverParams {
  // Specifies template string for the notification title.
  // If null then template from the alert is used (?).
  public String titleTemplate;

  // Specifies template string for the notification text/body/description.
  // If null then template from the alert is used.
  public String textTemplate;

  public void validate() throws YWValidateException {
    // Nothing to check yet.
  }
}
