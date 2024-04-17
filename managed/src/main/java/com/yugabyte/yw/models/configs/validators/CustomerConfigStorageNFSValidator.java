// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData.RegionLocations;
import com.yugabyte.yw.models.helpers.CustomerConfigConsts;
import java.util.regex.Pattern;
import javax.inject.Inject;
import org.apache.commons.lang3.StringUtils;

public class CustomerConfigStorageNFSValidator extends ConfigDataValidator {

  private static final String NFS_PATH_REGEXP = "^/|//|(/[\\w-]+)+$";

  private static final String NFS_BUCKET_REGEXP = "^([\\w-]+)+$";

  private Pattern pathPattern;
  private Pattern bucketPattern;

  @Inject
  public CustomerConfigStorageNFSValidator(BeanValidator beanValidator) {
    super(beanValidator);
    pathPattern = Pattern.compile(NFS_PATH_REGEXP);
    bucketPattern = Pattern.compile(NFS_BUCKET_REGEXP);
  }

  @Override
  public void validate(CustomerConfigData data) {
    CustomerConfigStorageNFSData nfsData = (CustomerConfigStorageNFSData) data;
    String value = nfsData.backupLocation;
    if (!pathPattern.matcher(value).matches()) {
      String errorMsg = "Invalid field value '" + value + "'.";
      throwBeanConfigDataValidatorError(CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME, errorMsg);
    }
    if (!bucketPattern.matcher(nfsData.nfsBucket).matches()) {
      String errorMsg = "Invalid field value '" + value + "'.";
      throwBeanConfigDataValidatorError("NFS_BUCKET", errorMsg);
    }
    if (nfsData.regionLocations != null) {
      for (RegionLocations location : nfsData.regionLocations) {
        if (StringUtils.isEmpty(location.region)) {
          throwBeanConfigDataValidatorError(
              CustomerConfigConsts.REGION_FIELDNAME, "This field cannot be empty.");
        }
        if (!pathPattern.matcher(location.location).matches()) {
          String errorMsg = "Invalid field value '" + value + "'.";
          throwBeanConfigDataValidatorError(
              CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME, errorMsg);
        }
      }
    }
  }
}
