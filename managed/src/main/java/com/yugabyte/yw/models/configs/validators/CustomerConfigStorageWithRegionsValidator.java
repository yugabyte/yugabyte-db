// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageWithRegionsData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageWithRegionsData.RegionLocation;
import com.yugabyte.yw.models.helpers.CustomerConfigConsts;
import java.util.Collection;
import javax.inject.Inject;
import org.apache.commons.lang3.StringUtils;

public class CustomerConfigStorageWithRegionsValidator extends CustomerConfigStorageValidator {

  @Inject
  public CustomerConfigStorageWithRegionsValidator(
      BeanValidator beanValidator, Collection<String> schemes) {
    super(beanValidator, schemes);
  }

  @Override
  public void validate(CustomerConfigData data) {
    CustomerConfigStorageWithRegionsData storageData = (CustomerConfigStorageWithRegionsData) data;
    super.validate(storageData);
    if (storageData.regionLocations != null) {
      for (RegionLocation location : storageData.regionLocations) {
        if (StringUtils.isEmpty(location.region)) {
          throwBeanValidatorError(
              CustomerConfigConsts.REGION_FIELDNAME, "This field cannot be empty.");
        }
        validateUrl(CustomerConfigConsts.REGION_LOCATION_FIELDNAME, location.location, true, false);
      }
    }
  }
}
