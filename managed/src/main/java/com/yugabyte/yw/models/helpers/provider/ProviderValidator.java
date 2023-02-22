// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers.provider;

import com.google.inject.Inject;
import com.yugabyte.yw.cloud.aws.AWSCloudImpl;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.configs.validators.AWSProviderValidator;
import com.yugabyte.yw.models.configs.validators.ProviderFieldsValidator;
import com.yugabyte.yw.models.helpers.BaseBeanValidator;
import java.util.HashMap;
import java.util.Map;

public class ProviderValidator extends BaseBeanValidator {

  private final Map<String, ProviderFieldsValidator> providerValidatorMap = new HashMap<>();

  @Inject
  public ProviderValidator(
      BeanValidator beanValidator, AWSCloudImpl awsCloudImpl, RuntimeConfGetter runtimeConfGetter) {
    super(beanValidator);
    this.providerValidatorMap.put(
        CloudType.aws.toString(),
        new AWSProviderValidator(beanValidator, awsCloudImpl, runtimeConfGetter));
  }

  public void validate(Provider provider) {
    ProviderFieldsValidator providerFieldsValidator = providerValidatorMap.get(provider.getCode());
    if (providerFieldsValidator != null) {
      providerFieldsValidator.validate(provider);
    }
  }
}
