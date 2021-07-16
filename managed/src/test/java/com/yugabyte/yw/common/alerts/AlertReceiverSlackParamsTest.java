// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class AlertReceiverSlackParamsTest {

  @Test
  // @formatter:off
  @Parameters({
    "null, null, null, Slack parameters: channel is empty.",
    "channel, null, null, Slack parameters: incorrect WebHook url.",
    "channel, incorrect url, null, Slack parameters: incorrect WebHook url.",
    "channel, http://www.google.com, null, null",
    "channel, http://www.google.com, incorrect url, Slack parameters: incorrect icon url.",
    "channel, http://www.google.com, http://www.google.com, null",
  })
  // @formatter:on
  public void testValidate(
      @Nullable String channel,
      @Nullable String webHookUrl,
      @Nullable String iconUrl,
      @Nullable String expectedError)
      throws YWValidateException {
    AlertReceiverSlackParams params = new AlertReceiverSlackParams();
    params.channel = channel;
    params.webhookUrl = webHookUrl;
    params.iconUrl = iconUrl;
    if (expectedError != null) {
      YWValidateException ex =
          assertThrows(
              YWValidateException.class,
              () -> {
                params.validate();
              });
      assertEquals(expectedError, ex.getMessage());
    } else {
      params.validate();
    }
  }
}
