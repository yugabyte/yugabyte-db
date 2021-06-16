// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import java.util.ArrayList;
import java.util.Collections;

import org.junit.Test;
import org.junit.runner.RunWith;

import com.yugabyte.yw.common.EmailHelper;

import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;

@RunWith(JUnitParamsRunner.class)
public class AlertReceiverEmailParamsTest {

  @Test
  // @formatter:off
  @Parameters({
    "null, false, Email parameters: destinations are empty.",
    "test@test, false, Email parameters: destinations contain invalid email address test@test",
    "test@test.com; test2@test2.com, false, Email parameters: SMTP configuration is incorrect.",
    "test1@test1.com; test2@test2.com; test@test, false, "
        + "Email parameters: destinations contain invalid email address test@test",
    "test@test.com, false, Email parameters: SMTP configuration is incorrect.",
    "test@test.com, true, null",
  })
  // @formatter:on
  public void testValidate(
      @Nullable String destinations, boolean setSmtpData, @Nullable String expectedError)
      throws YWValidateException {
    AlertReceiverEmailParams params = new AlertReceiverEmailParams();
    params.recipients =
        destinations != null
            ? new ArrayList<>(
                EmailHelper.splitEmails(destinations, EmailHelper.DEFAULT_EMAIL_SEPARATORS))
            : Collections.emptyList();
    params.smtpData = setSmtpData ? new SmtpData() : null;
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
