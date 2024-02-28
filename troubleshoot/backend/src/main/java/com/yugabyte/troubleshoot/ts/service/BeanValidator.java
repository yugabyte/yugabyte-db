/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.troubleshoot.ts.service;

import jakarta.validation.Validator;
import java.util.*;
import org.apache.commons.lang3.StringUtils;
import org.springframework.stereotype.Component;

@Component
public class BeanValidator {

  // Using Hibernate Validator that has many more validation annotations
  private final Validator validator;

  public BeanValidator(Validator validator) {
    this.validator = validator;
  }

  public void validate(Object bean) {
    validate(bean, StringUtils.EMPTY);
  }

  public void validate(Object bean, String fieldNamePrefix) {
    Map<String, ValidationException.ValidationErrors> validationErrors = new LinkedHashMap<>();
    validator
        .validate(bean)
        .forEach(
            e -> {
              String fullPath = getFieldName(e.getPropertyPath().toString(), fieldNamePrefix);
              validationErrors
                  .computeIfAbsent(fullPath, ValidationException.ValidationErrors::new)
                  .getErrors()
                  .add(e.getMessage());
            });
    if (!validationErrors.isEmpty()) {
      throw new ValidationException(new ArrayList<>(validationErrors.values()));
    }
  }

  private String getFieldName(String path, String prefix) {
    if (StringUtils.isEmpty(prefix)) {
      return path;
    }
    if (StringUtils.isEmpty(path)) {
      return prefix;
    }
    return prefix + "." + path;
  }

  public ErrorMessageBuilder error() {
    return new ErrorMessageBuilder();
  }

  public static class ErrorMessageBuilder {
    Map<String, ValidationException.ValidationErrors> validationErrors = new LinkedHashMap<>();

    public ErrorMessageBuilder forField(String fieldName, String message) {
      validationErrors
          .computeIfAbsent(fieldName, ValidationException.ValidationErrors::new)
          .getErrors()
          .add(message);
      return this;
    }

    public ErrorMessageBuilder global(String message) {
      return forField(StringUtils.EMPTY, message);
    }

    public void throwError() {
      throw new ValidationException(new ArrayList<>(validationErrors.values()));
    }
  }
}
