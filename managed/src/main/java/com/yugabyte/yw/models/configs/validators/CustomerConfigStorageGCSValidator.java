// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.google.cloud.storage.Storage;
import com.google.cloud.storage.StorageException;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.CloudUtil.ConfigLocationInfo;
import com.yugabyte.yw.common.CloudUtil.ExtraPermissionToValidate;
import com.yugabyte.yw.common.GCPUtil;
import com.yugabyte.yw.models.configs.CloudClientsFactory;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData.RegionLocations;
import com.yugabyte.yw.models.helpers.CustomerConfigConsts;
import java.io.IOException;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import javax.inject.Inject;
import org.apache.commons.lang3.StringUtils;

public class CustomerConfigStorageGCSValidator extends CustomerConfigStorageValidator {

  private static final Collection<String> GCS_URL_SCHEMES =
      Arrays.asList(new String[] {"https", "gs"});

  private final CloudClientsFactory factory;
  private final GCPUtil gcpUtil;

  private final List<ExtraPermissionToValidate> permissions =
      ImmutableList.of(ExtraPermissionToValidate.READ, ExtraPermissionToValidate.LIST);

  @Inject
  public CustomerConfigStorageGCSValidator(
      BeanValidator beanValidator, CloudClientsFactory factory, GCPUtil gcpUtil) {
    super(beanValidator, GCS_URL_SCHEMES);
    this.factory = factory;
    this.gcpUtil = gcpUtil;
  }

  @Override
  public void validate(CustomerConfigData data) {
    super.validate(data);

    CustomerConfigStorageGCSData gcsData = (CustomerConfigStorageGCSData) data;
    if (!StringUtils.isEmpty(gcsData.gcsCredentialsJson)) {
      Storage storage = null;
      try {
        storage = factory.createGcpStorage(gcsData);
      } catch (IOException ex) {
        throwBeanValidatorError(CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME, ex.getMessage());
      }

      validateGCSUrl(
          storage, CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME, gcsData.backupLocation);
      if (gcsData.regionLocations != null) {
        for (RegionLocations location : gcsData.regionLocations) {
          if (StringUtils.isEmpty(location.region)) {
            throwBeanValidatorError(
                CustomerConfigConsts.REGION_FIELDNAME, "This field cannot be empty.");
          }
          validateUrl(
              CustomerConfigConsts.REGION_LOCATION_FIELDNAME, location.location, true, false);
          validateGCSUrl(
              storage, CustomerConfigConsts.REGION_LOCATION_FIELDNAME, location.location);
        }
      }
    }
  }

  private void validateGCSUrl(Storage storage, String fieldName, String gsUriPath) {
    String protocol =
        gsUriPath.indexOf(':') >= 0 ? gsUriPath.substring(0, gsUriPath.indexOf(':')) : "";

    // Assuming bucket name will always start with gs:// or https:// otherwise that
    // will be invalid. See GSPUtil.getSplitLocationValue to understand how it is
    // processed.
    if (gsUriPath.length() < 5 || !GCS_URL_SCHEMES.contains(protocol)) {
      String exceptionMsg = "Invalid gsUriPath format: " + gsUriPath;
      throwBeanValidatorError(fieldName, exceptionMsg);
    } else {
      ConfigLocationInfo locationInfo = gcpUtil.getConfigLocationInfo(gsUriPath);
      try {
        gcpUtil.validateOnBucket(storage, locationInfo.bucket, locationInfo.cloudPath, permissions);
      } catch (StorageException exp) {
        throwBeanValidatorError(fieldName, exp.getMessage());
      }
    }
  }
}
