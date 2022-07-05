// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import com.yugabyte.yw.models.helpers.CustomerConfigConsts;
import java.util.regex.Pattern;
import javax.inject.Inject;

public class CustomerConfigStorageNFSValidator extends ConfigDataValidator {

  private static final String NFS_PATH_REGEXP = "^/|//|(/[\\w-]+)+$";

  private Pattern pattern;

  @Inject
  public CustomerConfigStorageNFSValidator(BeanValidator beanValidator) {
    super(beanValidator);
    pattern = Pattern.compile(NFS_PATH_REGEXP);
  }

  @Override
  public void validate(CustomerConfigData data) {
    CustomerConfigStorageNFSData nfsData = (CustomerConfigStorageNFSData) data;
    String value = nfsData.backupLocation;
    if (!pattern.matcher(value).matches()) {
      String errorMsg = "Invalid field value '" + value + "'.";
      throwBeanValidatorError(CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME, errorMsg);
    }
  }
}
