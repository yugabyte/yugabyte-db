// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigPasswordPolicyData;
import javax.inject.Inject;

public class CustomerConfigPasswordPolicyValidator extends ConfigDataValidator {

  @Inject
  public CustomerConfigPasswordPolicyValidator(BeanValidator beanValidator) {
    super(beanValidator);
  }

  @Override
  public void validate(CustomerConfigData data) {
    CustomerConfigPasswordPolicyData ppData = (CustomerConfigPasswordPolicyData) data;
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
