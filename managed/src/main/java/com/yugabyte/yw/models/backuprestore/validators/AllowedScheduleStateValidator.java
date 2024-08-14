// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.backuprestore.validators;

import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.backuprestore.annotations.AllowedScheduleState;
import java.util.Arrays;
import javax.validation.ConstraintValidator;
import javax.validation.ConstraintValidatorContext;

public class AllowedScheduleStateValidator
    implements ConstraintValidator<AllowedScheduleState, Schedule.State> {
  private Schedule.State[] allowedStates;

  @Override
  public void initialize(AllowedScheduleState allowedScheduleState) {
    allowedStates = allowedScheduleState.anyOf();
  }

  @Override
  public boolean isValid(Schedule.State value, ConstraintValidatorContext context) {
    return value == null || Arrays.asList(allowedStates).contains(value);
  }
}
