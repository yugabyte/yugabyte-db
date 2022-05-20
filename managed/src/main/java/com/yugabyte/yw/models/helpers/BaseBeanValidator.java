// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.common.BeanValidator;
import javax.inject.Inject;
import org.apache.commons.lang3.StringUtils;

public abstract class BaseBeanValidator {
  protected final BeanValidator beanValidator;

  @Inject
  public BaseBeanValidator(BeanValidator beanValidator) {
    this.beanValidator = beanValidator;
  }

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
