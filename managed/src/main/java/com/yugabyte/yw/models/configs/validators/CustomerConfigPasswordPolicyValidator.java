// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs.validators;

import static com.yugabyte.yw.common.password.PasswordPolicyService.DEFAULT_MIN_LENGTH_FIPS_PARAM;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigPasswordPolicyData;
import com.yugabyte.yw.models.helpers.CommonUtils;
import javax.inject.Inject;

public class CustomerConfigPasswordPolicyValidator extends ConfigDataValidator {
  private final RuntimeConfGetter runtimeConfGetter;

  @Inject
  public CustomerConfigPasswordPolicyValidator(
      BeanValidator beanValidator, RuntimeConfGetter runtimeConfGetter) {
    super(beanValidator);
    this.runtimeConfGetter = runtimeConfGetter;
  }

  @Override
  public void validate(CustomerConfigData data) {
    CustomerConfigPasswordPolicyData ppData = (CustomerConfigPasswordPolicyData) data;
    boolean fipsEnabled = runtimeConfGetter.getStaticConf().getBoolean(CommonUtils.FIPS_ENABLED);
    int minFipsLength = runtimeConfGetter.getStaticConf().getInt(DEFAULT_MIN_LENGTH_FIPS_PARAM);
    if (fipsEnabled && ppData.getMinLength() < minFipsLength) {
      beanValidator
          .error()
          .forField(
              "data.minLength",
              "Minimal length should be not less than "
                  + minFipsLength
                  + " characters on FIPS enabled system")
          .throwError();
    }
    if (ppData.getMinUppercase()
            + ppData.getMinLowercase()
            + ppData.getMinDigits()
            + ppData.getMinSpecialCharacters()
        > ppData.getMinLength()) {
      beanValidator
          .error()
          .forField(
              "data",
              "Minimal length should be not less than the sum of minimal counts"
                  + " for upper case, lower case, digits and special characters")
          .throwError();
    }
  }
}
