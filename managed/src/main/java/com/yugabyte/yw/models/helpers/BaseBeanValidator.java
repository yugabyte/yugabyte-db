// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.google.common.collect.SetMultimap;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.BeanValidator.ErrorMessageBuilder;
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

  protected void throwBeanValidatorError(
      String fieldName, String exceptionMsg, String errorSource) {
    beanValidator
        .error()
        .forField(fieldFullName(fieldName), exceptionMsg)
        .forField("errorSource", errorSource)
        .throwError();
  }

  protected void throwMultipleBeanValidatorError(
      SetMultimap<String, String> errorsMap, String errorSource) {
    ErrorMessageBuilder builder = beanValidator.error().forField("errorSource", errorSource);
    for (String errorField : errorsMap.keySet()) {
      builder.forField(fieldFullName(errorField), String.join(", ", errorsMap.get(errorField)));
    }
    builder.throwError();
  }

  public static String fieldFullName(String fieldName) {
    if (StringUtils.isEmpty(fieldName)) {
      return "data";
    }
    return "data." + fieldName;
  }
}
