// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import java.util.Objects;

import org.apache.commons.lang3.StringUtils;
import org.apache.commons.validator.routines.UrlValidator;

import com.fasterxml.jackson.annotation.JsonTypeName;

/*
 * Settings example:
 *
 *   - channel: '#yw-alerts'
 *    icon_url: https://avatars3.githubusercontent.com/u/3380462
 *    send_resolved: true
 *    title: '{{ template "slack_health.title.tmpl" . }}'
 *    text: '{{ template "slack_health.body.tmpl" . }}'
 */
@JsonTypeName("Slack")
public class AlertReceiverSlackParams extends AlertReceiverParams {

  public String channel;

  public String webhookUrl;

  public String iconUrl;

  public void validate() throws YWValidateException {
    super.validate();

    if (StringUtils.isEmpty(channel)) {
      throw new YWValidateException("Slack parameters: channel is empty.");
    }

    UrlValidator urlValidator = new UrlValidator();
    if (!urlValidator.isValid(webhookUrl)) {
      throw new YWValidateException("Slack parameters: incorrect WebHook url.");
    }
    if (StringUtils.isNotEmpty(iconUrl) && !urlValidator.isValid(iconUrl)) {
      throw new YWValidateException("Slack parameters: incorrect icon url.");
    }
  }

  @Override
  public int hashCode() {
    final int prime = 31;
    int result = super.hashCode();
    result = prime * result + Objects.hash(channel, iconUrl, webhookUrl);
    return result;
  }

  @Override
  public boolean equals(Object obj) {
    if (this == obj) {
      return true;
    }
    if (!super.equals(obj)) {
      return false;
    }
    if (!(obj instanceof AlertReceiverSlackParams)) {
      return false;
    }
    AlertReceiverSlackParams other = (AlertReceiverSlackParams) obj;
    return Objects.equals(channel, other.channel)
        && Objects.equals(iconUrl, other.iconUrl)
        && Objects.equals(webhookUrl, other.webhookUrl);
  }

  @Override
  public String toString() {
    return "AlertReceiverSlackParams [channel="
        + channel
        + ", webhookUrl="
        + webhookUrl
        + ", iconUrl="
        + iconUrl
        + ", titleTemplate="
        + titleTemplate
        + ", textTemplate="
        + textTemplate
        + ", continueSend="
        + continueSend
        + "]";
  }
}
