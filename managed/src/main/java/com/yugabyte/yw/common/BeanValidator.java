/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import static java.util.stream.Collectors.toList;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.modules.CustomObjectMapperModule;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import javax.validation.ConstraintViolation;
import javax.validation.Validator;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Singleton
public class BeanValidator {

  // Using Hibernate Validator that has many more validation annotations
  private final Validator validator;

  // More strict mapper for validation. Makes sure input does not have anything after a
  private final ObjectMapper objectMapper;

  @Inject
  public BeanValidator(Validator validator) {
    this.validator = validator;
    this.objectMapper = CustomObjectMapperModule.createDefaultMapper();
    this.objectMapper.configure(DeserializationFeature.FAIL_ON_TRAILING_TOKENS, true);
  }

  public void validate(Object bean) {
    validate(bean, StringUtils.EMPTY);
  }

  public void validate(Object bean, String fieldNamePrefix) {
    /* TODO It will not convert play constraints message codes (like `error.required`)
    to correct messages. Need work to make it work OR use javax.validation or
    hibernate validation annotations - which work out of the box. */
    Map<String, List<String>> validationErrors =
        validator.validate(bean).stream()
            .collect(
                Collectors.groupingBy(
                    e -> getFieldName(e.getPropertyPath().toString(), fieldNamePrefix),
                    Collectors.mapping(ConstraintViolation::getMessage, toList())));
    if (!validationErrors.isEmpty()) {
      JsonNode errJson = Json.toJson(validationErrors);
      throw new PlatformServiceException(BAD_REQUEST, errJson);
    }
  }

  public void validateValidJson(String fieldName, String fieldValue, String errorMessage) {
    try {
      objectMapper.readTree(fieldValue);
    } catch (Exception e) {
      error().forField(fieldName, errorMessage).throwError();
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
    int errorCode = BAD_REQUEST;
    Map<String, List<String>> validationErrors = new HashMap<>();

    public ErrorMessageBuilder forField(String fieldName, String message) {
      validationErrors.computeIfAbsent(fieldName, k -> new ArrayList<>()).add(message);
      return this;
    }

    public ErrorMessageBuilder global(String message) {
      return forField(StringUtils.EMPTY, message);
    }

    public ErrorMessageBuilder code(int errorCode) {
      this.errorCode = errorCode;
      return this;
    }

    public void throwError() {
      JsonNode errJson = Json.toJson(validationErrors);
      throw new PlatformServiceException(errorCode, errJson);
    }
  }
}
