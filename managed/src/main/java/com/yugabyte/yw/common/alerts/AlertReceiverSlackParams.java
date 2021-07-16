// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import com.fasterxml.jackson.annotation.JsonTypeName;
import lombok.EqualsAndHashCode;
import lombok.ToString;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.validator.routines.UrlValidator;

@EqualsAndHashCode(callSuper = false)
@ToString
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
}
