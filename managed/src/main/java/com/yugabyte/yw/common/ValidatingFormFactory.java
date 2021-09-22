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

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import javax.validation.ConstraintViolation;
import javax.validation.Validator;
import org.springframework.beans.BeansException;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;

@Singleton
public class ValidatingFormFactory {

  private final FormFactory formFactory;

  // Using Hibernate Validator that has many more validation annotations
  private final Validator validator;

  @Inject
  public ValidatingFormFactory(FormFactory formFactory, Validator validator) {
    this.formFactory = formFactory;
    this.validator = validator;
  }

  public <T> Form<T> getFormDataOrBadRequest(Class<T> clazz) {
    Form<T> formData = formFactory.form(clazz).bindFromRequest();
    if (formData.hasErrors()) {
      throw new PlatformServiceException(BAD_REQUEST, formData.errorsAsJson());
    }
    return formData;
  }

  public <T> T getFormDataOrBadRequest(JsonNode jsonNode, Class<T> clazz) {
    // Do this so that constraint get validated
    T bean = Json.fromJson(jsonNode, clazz);

    try {
      // TODO: Ability to ignore fail on unknown fields
      //      DataBinder dataBinder = new DataBinder(bean);
      //      dataBinder.setIgnoreUnknownFields(false);
      //      dataBinder.bind(new MutablePropertyValues(requestData));

      // TODO: Use messageApi to translate `error.required` to human readable message

      Map<String, List<String>> validationErrors =
          validator
              .validate(bean)
              .stream()
              .collect(
                  Collectors.groupingBy(
                      e -> e.getPropertyPath().toString(),
                      Collectors.mapping(ConstraintViolation::getMessage, toList())));
      if (!validationErrors.isEmpty()) {
        JsonNode errJson = Json.toJson(validationErrors);
        throw new PlatformServiceException(BAD_REQUEST, errJson);
      }
    } catch (BeansException ex) {
      throw new PlatformServiceException(BAD_REQUEST, ex.getMessage() + ": " + jsonNode.toString());
    }
    return bean;
  }
}
