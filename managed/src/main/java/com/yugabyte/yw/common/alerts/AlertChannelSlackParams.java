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
public class AlertChannelSlackParams extends AlertChannelParams {

  public String username;

  public String webhookUrl;

  public String iconUrl;

  public void validate() throws YWValidateException {
    super.validate();

    if (StringUtils.isEmpty(username)) {
      throw new YWValidateException("Slack parameters: username is empty.");
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
