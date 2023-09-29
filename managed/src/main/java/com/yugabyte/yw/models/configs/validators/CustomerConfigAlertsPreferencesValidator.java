// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.EmailHelper;
import com.yugabyte.yw.models.configs.data.CustomerConfigAlertsPreferencesData;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import javax.inject.Inject;
import net.logstash.logback.encoder.org.apache.commons.lang3.StringUtils;
import org.apache.commons.validator.routines.EmailValidator;

public class CustomerConfigAlertsPreferencesValidator extends ConfigDataValidator {

  @Inject
  public CustomerConfigAlertsPreferencesValidator(BeanValidator beanValidator) {
    super(beanValidator);
  }

  @Override
  public void validate(CustomerConfigData data) {
    CustomerConfigAlertsPreferencesData apData = (CustomerConfigAlertsPreferencesData) data;
    if (StringUtils.isNoneEmpty(apData.getAlertingEmail())) {
      EmailValidator emailValidator = EmailValidator.getInstance(false, false);
      for (String email : apData.getAlertingEmail().split(",")) {
        String emailOnly = EmailHelper.extractEmailAddress(email);
        if ((emailOnly == null) || !emailValidator.isValid(emailOnly)) {
          beanValidator
              .error()
              .forField("data.alertingEmail", "invalid email address " + email)
              .throwError();
        }
      }
    }
  }
}
