// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.SetMultimap;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.BeanValidator.ErrorMessageBuilder;
import javax.inject.Inject;

public abstract class BaseBeanValidator {
  protected final BeanValidator beanValidator;

  @Inject
  public BaseBeanValidator(BeanValidator beanValidator) {
    this.beanValidator = beanValidator;
  }

  protected void throwBeanValidatorError(
      String fieldName, String exceptionMsg, JsonNode requestJson) {
    beanValidator.error().forRequest(requestJson).forField(fieldName, exceptionMsg).throwError();
  }

  protected void throwBeanValidatorError(
      String fieldName, String exceptionMsg, String errorSource, JsonNode requestJson) {
    beanValidator
        .error()
        .forRequest(requestJson)
        .forField(fieldName, exceptionMsg)
        .forField("errorSource", errorSource)
        .throwError();
  }

  protected void throwMultipleBeanValidatorError(
      SetMultimap<String, String> errorsMap, String errorSource, JsonNode requestJson) {
    ErrorMessageBuilder builder = beanValidator.error().forField("errorSource", errorSource);
    builder.forRequest(requestJson);
    for (String errorField : errorsMap.keySet()) {
      builder.forField(errorField, errorsMap.get(errorField));
    }
    builder.throwError();
  }
}
