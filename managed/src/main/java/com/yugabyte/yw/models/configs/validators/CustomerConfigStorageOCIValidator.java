// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.OCIUtil;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import java.util.Arrays;
import java.util.Collection;
import javax.inject.Inject;

public class CustomerConfigStorageOCIValidator extends CustomerConfigStorageValidator {

  private static final Collection<String> OCI_URL_SCHEMES = Arrays.asList("https", "s3");

  private final OCIUtil ociUtil;

  @Inject
  public CustomerConfigStorageOCIValidator(BeanValidator beanValidator, OCIUtil ociUtil) {
    super(beanValidator, OCI_URL_SCHEMES);
    this.ociUtil = ociUtil;
  }

  @Override
  public void validate(CustomerConfigData data) {
    super.validate(data);
    // TODO: Add OCI-specific validation (auth mode XOR, URL format, live bucket checks).
  }
}
