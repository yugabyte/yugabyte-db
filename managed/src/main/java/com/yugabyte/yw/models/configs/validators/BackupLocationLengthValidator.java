// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import javax.validation.ConstraintValidator;
import javax.validation.ConstraintValidatorContext;
import org.apache.commons.lang3.StringUtils;

public class BackupLocationLengthValidator
    implements ConstraintValidator<BackupLocationLengthConstraint, String> {

  @Override
  public void initialize(BackupLocationLengthConstraint constraintAnnotation) {}

  @Override
  public boolean isValid(String value, ConstraintValidatorContext context) {
    if (StringUtils.isBlank(value)) {
      return false;
    }
    // For NFS, length can be 1.
    if (value.startsWith("/")) {
      return value.length() > 0;
    }
    // For other cloud locations, length at least 5.
    return value.length() > 4;
  }
}
