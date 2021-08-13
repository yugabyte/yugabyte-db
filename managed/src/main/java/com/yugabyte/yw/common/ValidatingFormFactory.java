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

@Singleton
public class ValidatingFormFactory {
  private final FormFactory formFactory;

  @Inject
  public ValidatingFormFactory(FormFactory formFactory) {
    this.formFactory = formFactory;
  }

  public <T> Form<T> getFormDataOrBadRequest(Class<T> clazz) {
    Form<T> formData = formFactory.form(clazz).bindFromRequest();
    if (formData.hasErrors()) {
      throw new YWServiceException(BAD_REQUEST, formData.errorsAsJson());
    }
    return formData;
  }

  public <T> T getFormDataOrBadRequest(JsonNode jsonNode, Class<T> clazz) {
    // Do this so that constraint get validated
    Form<T> formData = formFactory.form(clazz).bind(jsonNode);
    if (formData.hasErrors()) {
      throw new YWServiceException(BAD_REQUEST, formData.errorsAsJson());
    }
    return Json.fromJson(jsonNode, clazz);
  }
}
