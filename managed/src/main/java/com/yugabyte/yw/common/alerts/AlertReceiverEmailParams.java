// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import com.fasterxml.jackson.annotation.JsonTypeName;
import com.yugabyte.yw.common.EmailHelper;
import java.util.List;
import lombok.EqualsAndHashCode;
import lombok.ToString;
import org.apache.commons.validator.routines.EmailValidator;

@EqualsAndHashCode(callSuper = false)
@ToString
// TODO: To mask/unmask sensitive fields while serializing to/deserializing from Json.
@JsonTypeName("Email")
public class AlertReceiverEmailParams extends AlertReceiverParams {

  public boolean defaultRecipients = false;

  public List<String> recipients;

  public boolean defaultSmtpSettings = false;

  public SmtpData smtpData;

  @Override
  public void validate() throws YWValidateException {
    super.validate();

    boolean emptyRecipients = (recipients == null) || recipients.isEmpty();
    if (defaultRecipients == !emptyRecipients) {
      throw new YWValidateException(
          "Email parameters: only one of defaultRecipients and recipients[] should be set.");
    }

    if (defaultSmtpSettings == (smtpData != null)) {
      throw new YWValidateException(
          "Email parameters: only one of defaultSmtpSettings and smtpData should be set.");
    }

    if (!emptyRecipients) {
      // Local addresses are not allowed + we don't check TLDs.
      EmailValidator emailValidator = EmailValidator.getInstance(false, false);
      for (String email : recipients) {
        String emailOnly = EmailHelper.extractEmailAddress(email);
        if ((emailOnly == null) || !emailValidator.isValid(emailOnly)) {
          throw new YWValidateException(
              "Email parameters: destinations contain invalid email address " + email);
        }
      }
    }
  }
}
