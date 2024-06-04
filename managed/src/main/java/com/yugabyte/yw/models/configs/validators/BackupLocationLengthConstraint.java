// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import java.lang.annotation.Documented;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import javax.validation.Constraint;
import javax.validation.Payload;

@Documented
@Constraint(validatedBy = BackupLocationLengthValidator.class)
@Target({ElementType.FIELD})
@Retention(RetentionPolicy.RUNTIME)
public @interface BackupLocationLengthConstraint {
  String message() default "Violates backup location length";

  Class<?>[] groups() default {};

  Class<? extends Payload>[] payload() default {};
}
