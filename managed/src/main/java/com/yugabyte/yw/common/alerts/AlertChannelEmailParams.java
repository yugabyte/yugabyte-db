// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import com.fasterxml.jackson.annotation.JsonTypeName;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.EmailHelper;
import java.util.List;
import lombok.Data;
import lombok.EqualsAndHashCode;
import org.apache.commons.validator.routines.EmailValidator;

@Data
@EqualsAndHashCode(callSuper = false)
// TODO: To mask/unmask sensitive fields while serializing to/deserializing from Json.
@JsonTypeName("Email")
public class AlertChannelEmailParams extends AlertChannelParams {

  private boolean defaultRecipients = false;

  private List<String> recipients;

  private boolean defaultSmtpSettings = false;

  private SmtpData smtpData;

  @Override
  public void validate(BeanValidator validator) {
    super.validate(validator);

    boolean emptyRecipients = (recipients == null) || recipients.isEmpty();
    if (defaultRecipients == !emptyRecipients) {
      validator
          .error()
          .forField("params", "only one of defaultRecipients and recipients[] should be set.")
          .throwError();
    }

    if (defaultSmtpSettings == (smtpData != null)) {
      validator
          .error()
          .forField("params", "only one of defaultSmtpSettings and smtpData should be set.")
          .throwError();
    }

    if (!emptyRecipients) {
      // Local addresses are not allowed + we don't check TLDs.
      EmailValidator emailValidator = EmailValidator.getInstance(false, false);
      for (String email : recipients) {
        String emailOnly = EmailHelper.extractEmailAddress(email);
        if ((emailOnly == null) || !emailValidator.isValid(emailOnly)) {
          validator
              .error()
              .forField("params.recipients", "invalid email address " + email)
              .throwError();
        }
      }
    }
  }
}
