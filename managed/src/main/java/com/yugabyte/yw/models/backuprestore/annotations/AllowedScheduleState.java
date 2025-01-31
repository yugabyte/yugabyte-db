// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.backuprestore.annotations;

import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.backuprestore.validators.AllowedScheduleStateValidator;
import java.lang.annotation.Documented;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import javax.validation.Constraint;
import javax.validation.Payload;

@Target(ElementType.FIELD)
@Retention(RetentionPolicy.RUNTIME)
@Documented
@Constraint(validatedBy = AllowedScheduleStateValidator.class)
public @interface AllowedScheduleState {
  Schedule.State[] anyOf();

  String message() default "must be any of {anyOf}";

  Class<?>[] groups() default {};

  Class<? extends Payload>[] payload() default {};
}
