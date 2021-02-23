/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;
import play.libs.F;

import javax.validation.Constraint;
import javax.validation.ConstraintValidator;
import javax.validation.Payload;
import java.lang.annotation.Retention;
import java.lang.annotation.Target;
import java.net.MalformedURLException;
import java.net.URL;

import static java.lang.annotation.ElementType.*;
import static java.lang.annotation.ElementType.TYPE_USE;
import static java.lang.annotation.RetentionPolicy.RUNTIME;
import static play.libs.F.Tuple;

public class YugabyteConstraints extends Constraints {
  @Target({METHOD, FIELD, ANNOTATION_TYPE, CONSTRUCTOR, PARAMETER, TYPE_USE})
  @Retention(RUNTIME)
  @Constraint(validatedBy = ValidURLValidator.class)
  @play.data.Form.Display(name = "constraint.invalidURL")
  public @interface ValidURL {
    String message() default ValidURLValidator.message;

    Class<?>[] groups() default {};

    Class<? extends Payload>[] payload() default {};
  }

  /** Validator for {@code @ValidUrl} fields. */
  public static class ValidURLValidator extends Validator<Object>
    implements ConstraintValidator<ValidURL, Object> {

    public static final String message = "Invalid URL provided";

    public void initialize(ValidURL constraintAnnotation) {}

    public boolean isValid(Object object) {
      if (!(object instanceof String)) {
        return false;
      }

      try {
        new URL((String) object);
      } catch (MalformedURLException e) {
        return false;
      }

      return true;
    }

    public F.Tuple<String, Object[]> getErrorMessageKey() {
      return Tuple(message, new Object[] {});
    }
  }
}
