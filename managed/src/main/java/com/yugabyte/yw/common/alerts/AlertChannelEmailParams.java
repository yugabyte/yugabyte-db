// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonTypeName;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.EmailHelper;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import javax.validation.Valid;
import lombok.Data;
import lombok.EqualsAndHashCode;
import org.apache.commons.validator.routines.EmailValidator;

@Data
@EqualsAndHashCode(callSuper = false)
// TODO: To mask/unmask sensitive fields while serializing to/deserializing from Json.
@JsonTypeName("Email")
@ApiModel(parent = AlertChannelParams.class)
public class AlertChannelEmailParams extends AlertChannelParams {

  @ApiModelProperty(value = "Use health check notification recipients", accessMode = READ_WRITE)
  private boolean defaultRecipients = false;

  @ApiModelProperty(value = "List of recipients", accessMode = READ_WRITE)
  private List<String> recipients;

  @ApiModelProperty(value = "Use health check notification SMTP settings", accessMode = READ_WRITE)
  private boolean defaultSmtpSettings = false;

  @ApiModelProperty(value = "SMTP settings", accessMode = READ_WRITE)
  @Valid
  private SmtpData smtpData;

  public AlertChannelEmailParams() {
    setChannelType(ChannelType.Email);
  }

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
