// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import java.util.Objects;

import com.fasterxml.jackson.annotation.JsonSubTypes;
import com.fasterxml.jackson.annotation.JsonTypeInfo;
import com.fasterxml.jackson.annotation.JsonTypeInfo.As;

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

  // Whether we need to send the notification using other receivers.
  public boolean continueSend;

  public void validate() throws YWValidateException {
    // Nothing to check yet.
  }

  @Override
  public int hashCode() {
    return Objects.hash(continueSend, textTemplate, titleTemplate);
  }

  @Override
  public boolean equals(Object obj) {
    if (this == obj) {
      return true;
    }
    if (!(obj instanceof AlertReceiverParams)) {
      return false;
    }
    AlertReceiverParams other = (AlertReceiverParams) obj;
    return continueSend == other.continueSend
        && Objects.equals(textTemplate, other.textTemplate)
        && Objects.equals(titleTemplate, other.titleTemplate);
  }

  @Override
  public String toString() {
    return "AlertReceiverParams [titleTemplate="
        + titleTemplate
        + ", textTemplate="
        + textTemplate
        + ", continueSend="
        + continueSend
        + "]";
  }
}
