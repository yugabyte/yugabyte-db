// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.google.common.collect.HashMultimap;
import com.google.common.collect.SetMultimap;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.helpers.BaseBeanValidator;
import java.util.Map;
import javax.inject.Inject;
import org.apache.commons.lang3.StringUtils;

public abstract class ConfigDataValidator extends BaseBeanValidator {
  @Inject
  public ConfigDataValidator(BeanValidator beanValidator) {
    super(beanValidator);
  }

  public abstract void validate(CustomerConfigData data);

  protected void throwBeanConfigDataValidatorError(String fieldName, String exceptionMsg) {
    throwBeanValidatorError(fieldFullName(fieldName), exceptionMsg, null);
  }

  protected void throwBeanConfigDataValidatorError(
      String fieldName, String exceptionMsg, String errorSource) {
    throwBeanValidatorError(fieldFullName(fieldName), exceptionMsg, errorSource, null);
  }

  protected void throwMultipleBeanConfigDataValidatorError(
      SetMultimap<String, String> errorsMap, String errorSource) {
    SetMultimap<String, String> fullFieldErrors = HashMultimap.create();
    for (Map.Entry<String, String> entry : errorsMap.entries()) {
      fullFieldErrors.put(fieldFullName(entry.getKey()), entry.getValue());
    }
    throwMultipleBeanValidatorError(fullFieldErrors, errorSource, null);
  }

  // Adds "data." prefix to the given fieldName. The configuration
  // variables to be validated are under the data key for the
  // CustomerConfig.
  public static String fieldFullName(String fieldName) {
    if (StringUtils.isEmpty(fieldName)) {
      return "data";
    }
    return "data." + fieldName;
  }
}
