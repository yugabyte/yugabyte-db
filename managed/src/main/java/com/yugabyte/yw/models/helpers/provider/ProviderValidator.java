// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers.provider;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.cloud.aws.AWSCloudImpl;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.configs.validators.AWSProviderValidator;
import com.yugabyte.yw.models.configs.validators.OnPremValidator;
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
    this.providerValidatorMap.put(
        CloudType.onprem.toString(), new OnPremValidator(beanValidator, runtimeConfGetter));
  }

  public void validate(Provider provider) {
    try {
      ProviderFieldsValidator providerFieldsValidator =
          providerValidatorMap.get(provider.getCode());
      if (providerFieldsValidator != null) {
        providerFieldsValidator.validate(provider);
      }
    } catch (RuntimeException e) {
      if (!(e instanceof PlatformServiceException)) {
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
      }
      throw e;
    }
  }

  public void validate(AvailabilityZone zone, String providerCode) {
    ProviderFieldsValidator providerFieldsValidator = providerValidatorMap.get(providerCode);
    if (providerFieldsValidator != null) {
      providerFieldsValidator.validate(zone);
    }
  }
}
