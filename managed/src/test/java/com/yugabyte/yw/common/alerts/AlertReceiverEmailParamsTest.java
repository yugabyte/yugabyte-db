// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.common.EmailHelper;
import java.util.ArrayList;
import java.util.Collections;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class AlertReceiverEmailParamsTest {

  @Test
  // @formatter:off
  @Parameters({
    "null, false, true, "
        + "Email parameters: only one of defaultRecipients and recipients[] should be set.",
    "test@test, false, true, "
        + "Email parameters: destinations contain invalid email address test@test",
    "test@test.com; test2@test2.com, false, true, null",
    "test1@test1.com; test2@test2.com; test@test, false, true, "
        + "Email parameters: destinations contain invalid email address test@test",
    "test@test.com, true, true, "
        + "Email parameters: only one of defaultSmtpSettings and smtpData should be set.",
    "test@test.com, true, false, null",
  })
  // @formatter:on
  public void testValidate(
      @Nullable String destinations,
      boolean setSmtpData,
      boolean useDefaultSmtp,
      @Nullable String expectedError)
      throws YWValidateException {
    AlertReceiverEmailParams params = new AlertReceiverEmailParams();
    params.recipients =
        destinations != null
            ? new ArrayList<>(
                EmailHelper.splitEmails(destinations, EmailHelper.DEFAULT_EMAIL_SEPARATORS))
            : Collections.emptyList();
    params.smtpData = setSmtpData ? new SmtpData() : null;
    params.defaultSmtpSettings = useDefaultSmtp;
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
