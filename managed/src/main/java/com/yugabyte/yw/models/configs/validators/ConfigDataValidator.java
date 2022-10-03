// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.helpers.BaseBeanValidator;
import javax.inject.Inject;

public abstract class ConfigDataValidator extends BaseBeanValidator {
  @Inject
  public ConfigDataValidator(BeanValidator beanValidator) {
    super(beanValidator);
  }

  public abstract void validate(CustomerConfigData data);
}
