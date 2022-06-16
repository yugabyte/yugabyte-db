// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import javax.inject.Inject;
import org.apache.commons.lang3.StringUtils;

public abstract class ConfigDataValidator {

  protected final BeanValidator beanValidator;

  @Inject
  public ConfigDataValidator(BeanValidator beanValidator) {
    this.beanValidator = beanValidator;
  }

  public abstract void validate(CustomerConfigData data);

  protected void throwBeanValidatorError(String fieldName, String exceptionMsg) {
    beanValidator.error().forField(fieldFullName(fieldName), exceptionMsg).throwError();
  }

  public static String fieldFullName(String fieldName) {
    if (StringUtils.isEmpty(fieldName)) {
      return "data";
    }
    return "data." + fieldName;
  }
}
