// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.google.cloud.storage.Storage;
import com.google.cloud.storage.StorageException;
import com.google.common.collect.HashMultimap;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.SetMultimap;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.CloudUtil.CloudLocationInfo;
import com.yugabyte.yw.common.CloudUtil.ExtraPermissionToValidate;
import com.yugabyte.yw.common.GCPUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
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

  private static final String AUTHORITY_CHARS_REGEX = "^[a-z0-9][a-z0-9._-]{1,}[a-z0-9]$";

  private final CloudClientsFactory factory;
  private final GCPUtil gcpUtil;

  private final List<ExtraPermissionToValidate> permissions =
      ImmutableList.of(ExtraPermissionToValidate.READ, ExtraPermissionToValidate.LIST);

  @Inject
  public CustomerConfigStorageGCSValidator(
      BeanValidator beanValidator, CloudClientsFactory factory, GCPUtil gcpUtil) {
    super(beanValidator, GCS_URL_SCHEMES, AUTHORITY_CHARS_REGEX);
    this.factory = factory;
    this.gcpUtil = gcpUtil;
  }

  @Override
  public void validate(CustomerConfigData data) {
    super.validate(data);

    CustomerConfigStorageGCSData gcsData = (CustomerConfigStorageGCSData) data;

    // Should not contain neither or both json creds and use GCP IAM flag.
    if (StringUtils.isBlank(gcsData.gcsCredentialsJson) ^ (gcsData.useGcpIam)) {
      SetMultimap<String, String> validationErrorsMap = HashMultimap.create();
      validationErrorsMap.put(
          CustomerConfigConsts.USE_GCP_IAM_FIELDNAME,
          "Must pass only one of 'GCS_CREDENTIALS_JSON' or 'USE_GCP_IAM'.");
      validationErrorsMap.put(
          CustomerConfigConsts.GCS_CREDENTIALS_JSON_FIELDNAME,
          "Must pass only one of 'GCS_CREDENTIALS_JSON' or 'USE_GCP_IAM'.");
      throwMultipleBeanConfigDataValidatorError(validationErrorsMap, "storageConfigValidation");
    }

    Storage storage = null;
    try {
      storage = factory.createGcpStorage(gcsData);
    } catch (IOException ex) {
      throwBeanConfigDataValidatorError(
          CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME, ex.getMessage());
    }

    validateGCSUrl(
        storage,
        CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME,
        gcsData,
        YbcBackupUtil.DEFAULT_REGION_STRING);
    if (gcsData.regionLocations != null) {
      for (RegionLocations location : gcsData.regionLocations) {
        if (StringUtils.isEmpty(location.region)) {
          throwBeanConfigDataValidatorError(
              CustomerConfigConsts.REGION_FIELDNAME, "This field cannot be empty.");
        }
        validateUrl(CustomerConfigConsts.REGION_LOCATION_FIELDNAME, location.location, true, false);
        validateGCSUrl(
            storage, CustomerConfigConsts.REGION_LOCATION_FIELDNAME, gcsData, location.region);
      }
    }
  }

  private void validateGCSUrl(
      Storage storage, String fieldName, CustomerConfigStorageGCSData gcsData, String region) {
    String gsUriPath = gcpUtil.getRegionLocationsMap(gcsData).get(region);
    String protocol =
        gsUriPath.indexOf(':') >= 0 ? gsUriPath.substring(0, gsUriPath.indexOf(':')) : "";

    // Assuming bucket name will always start with gs:// or https:// otherwise that
    // will be invalid. See GCPUtil.getSplitLocationValue to understand how it is
    // processed.
    if (gsUriPath.length() < 5 || !GCS_URL_SCHEMES.contains(protocol)) {
      String exceptionMsg = "Invalid gsUriPath format: " + gsUriPath;
      throwBeanConfigDataValidatorError(fieldName, exceptionMsg);
    } else {
      CloudLocationInfo locationInfo = gcpUtil.getCloudLocationInfo(region, gcsData, gsUriPath);
      try {
        gcpUtil.validateOnBucket(storage, locationInfo.bucket, locationInfo.cloudPath, permissions);
      } catch (StorageException exp) {
        throwBeanConfigDataValidatorError(fieldName, exp.getMessage());
      }
    }
  }
}
