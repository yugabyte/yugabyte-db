// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import java.util.List;
import java.util.Objects;

import org.apache.commons.validator.routines.EmailValidator;

import com.fasterxml.jackson.annotation.JsonTypeName;
import com.yugabyte.yw.common.EmailHelper;

// TODO: To mask/unmask sensitive fields while serializing to/deserializing from Json.
@JsonTypeName("Email")
public class AlertReceiverEmailParams extends AlertReceiverParams {

  public List<String> recipients;

  public SmtpData smtpData;

  @Override
  public void validate() throws YWValidateException {
    super.validate();

    if ((recipients == null) || recipients.isEmpty()) {
      throw new YWValidateException("Email parameters: destinations are empty.");
    }

    // Local addresses are not allowed + we don't check TLDs.
    EmailValidator emailValidator = EmailValidator.getInstance(false, false);
    for (String email : recipients) {
      String emailOnly = EmailHelper.extractEmailAddress(email);
      if ((emailOnly == null) || !emailValidator.isValid(emailOnly)) {
        throw new YWValidateException(
            "Email parameters: destinations contain invalid email address " + email);
      }
    }

    if (smtpData == null) {
      throw new YWValidateException("Email parameters: SMTP configuration is incorrect.");
    }
  }

  @Override
  public int hashCode() {
    final int prime = 31;
    int result = super.hashCode();
    result = prime * result + Objects.hash(recipients, smtpData);
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
    if (!(obj instanceof AlertReceiverEmailParams)) {
      return false;
    }
    AlertReceiverEmailParams other = (AlertReceiverEmailParams) obj;
    return Objects.equals(recipients, other.recipients) && Objects.equals(smtpData, other.smtpData);
  }

  @Override
  public String toString() {
    return "AlertReceiverEmailParams [recipients=" + recipients + ", smtpData=" + smtpData + "]";
  }
}
