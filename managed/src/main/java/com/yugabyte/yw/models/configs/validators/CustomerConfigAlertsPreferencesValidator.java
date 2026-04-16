// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.EmailHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.configs.data.CustomerConfigAlertsPreferencesData;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import javax.inject.Inject;
import net.logstash.logback.encoder.org.apache.commons.lang3.StringUtils;
import org.apache.commons.validator.routines.EmailValidator;

public class CustomerConfigAlertsPreferencesValidator extends ConfigDataValidator {
  public static final int MIN_CHECK_INTERVAL_MS = 300000;

  private final RuntimeConfGetter runtimeConfGetter;

  @Inject
  public CustomerConfigAlertsPreferencesValidator(
      BeanValidator beanValidator, RuntimeConfGetter runtimeConfGetter) {
    super(beanValidator);
    this.runtimeConfGetter = runtimeConfGetter;
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
    long minCheckInterval =
        runtimeConfGetter.getStaticConf().getLong("yb.health.check_interval_ms");
    if (apData.getCheckIntervalMs() > 0 && apData.getCheckIntervalMs() < minCheckInterval) {
      beanValidator
          .error()
          .forField(
              "data.checkIntervalMs",
              "Check interval can't be less than " + minCheckInterval + "ms")
          .throwError();
    }
    if (apData.getStatusUpdateIntervalMs() > 0
        && apData.getStatusUpdateIntervalMs() < apData.getCheckIntervalMs()) {
      beanValidator
          .error()
          .forField(
              "data.statusUpdateIntervalMs",
              "Status update interval can't be less than check interval")
          .throwError();
    }
  }
}
