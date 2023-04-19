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

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Http.Request;

@Singleton
public class ValidatingFormFactory {

  private final FormFactory formFactory;

  private final BeanValidator validator;

  @Inject
  public ValidatingFormFactory(FormFactory formFactory, BeanValidator validator) {
    this.formFactory = formFactory;
    this.validator = validator;
  }

  /*
   * Use AbstractPlatformController.parseJsonAndValidate instead.
   */
  @Deprecated
  public <T> Form<T> getFormDataOrBadRequest(Request request, Class<T> clazz) {
    Form<T> formData = formFactory.form(clazz).bindFromRequest(request);
    if (formData.hasErrors()) {
      throw new PlatformServiceException(BAD_REQUEST, formData.errorsAsJson());
    }
    return formData;
  }

  public <T> T getFormDataOrBadRequest(JsonNode jsonNode, Class<T> clazz) {
    // TODO: Ability to ignore fail on unknown fields
    //      DataBinder dataBinder = new DataBinder(bean);
    //      dataBinder.setIgnoreUnknownFields(false);
    //      dataBinder.bind(new MutablePropertyValues(requestData));
    T bean;
    try {
      bean = Json.fromJson(jsonNode, clazz);
    } catch (Exception e) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Failed to parse " + clazz.getSimpleName() + " object: " + e.getMessage());
    }
    if (bean == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Request payload is empty, expected " + clazz.getSimpleName() + " object");
    }
    // Do this so that constraint get validated
    validator.validate(bean);
    return bean;
  }
}
