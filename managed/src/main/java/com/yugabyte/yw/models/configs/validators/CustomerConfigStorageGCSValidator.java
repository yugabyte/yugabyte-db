// Copyright (c) YugabyteDB, Inc.

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
import javax.annotation.Nullable;
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
    validate(data, null);
  }

  /**
   * A config saved with both a JSON key and {@code USE_GCP_IAM}, which an earlier YBA accepted,
   * keeps taking edits such as a rename while it keeps that combination, but cannot turn on
   * federation.
   */
  @Override
  public void validate(CustomerConfigData data, @Nullable CustomerConfigData stored) {
    super.validate(data);

    CustomerConfigStorageGCSData gcsData = (CustomerConfigStorageGCSData) data;
    CustomerConfigStorageGCSData storedGcs = (CustomerConfigStorageGCSData) stored;

    // Should not contain neither or both json creds and use GCP IAM flag.
    if ((StringUtils.isBlank(gcsData.gcsCredentialsJson) && !gcsData.useGcpIam)
        || newlyViolates(CustomerConfigStorageGCSValidator::hasKeyWithGcpIam, gcsData, storedGcs)) {
      SetMultimap<String, String> validationErrorsMap = HashMultimap.create();
      validationErrorsMap.put(
          CustomerConfigConsts.USE_GCP_IAM_FIELDNAME,
          "Must pass only one of 'GCS_CREDENTIALS_JSON' or 'USE_GCP_IAM'.");
      validationErrorsMap.put(
          CustomerConfigConsts.GCS_CREDENTIALS_JSON_FIELDNAME,
          "Must pass only one of 'GCS_CREDENTIALS_JSON' or 'USE_GCP_IAM'.");
      throwMultipleBeanConfigDataValidatorError(validationErrorsMap, "storageConfigValidation");
    }

    // Federation runs only through USE_GCP_IAM, and a JSON key takes priority over it on the DB
    // nodes, so a config saved with a key and USE_GCP_IAM, which the check above lets keep both,
    // cannot turn on federation. Federation with a JSON key and without USE_GCP_IAM is accepted:
    // the key is then the credential of YBA and the DB nodes alike, which BackupHelper counts as
    // YBA's own (see BackupHelper.hasCredentialsOfItsOwn).
    if (newlyViolates(
        CustomerConfigStorageGCSValidator::hasFederationWithKeyAndGcpIam, gcsData, storedGcs)) {
      throwBeanConfigDataValidatorError(
          CustomerConfigConsts.USE_GCP_IAM_FIELDNAME,
          "USE_CROSS_CLOUD_FEDERATION cannot be turned on while both GCS_CREDENTIALS_JSON and"
              + " USE_GCP_IAM are set. Remove GCS_CREDENTIALS_JSON to federate through"
              + " USE_GCP_IAM.");
    }

    // useGcpIam (GKE workload identity or cross-cloud federation): no universe/provider context at
    // config create/edit to resolve an audience, and same-cloud IAM can't be exercised from YBA.
    // Skip the bucket/cred check (URL already validated); the node/YBC do the real check.
    if (gcsData.useGcpIam) {
      return;
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
        gcpUtil.validateOnBucket(
            storage,
            locationInfo.bucket,
            locationInfo.cloudPath,
            permissions,
            gcsData.immutableStorage);
      } catch (StorageException exp) {
        throwBeanConfigDataValidatorError(fieldName, exp.getMessage());
      }
    }
  }

  private static boolean hasKeyWithGcpIam(CustomerConfigStorageGCSData gcsData) {
    return gcsData.useGcpIam && StringUtils.isNotBlank(gcsData.gcsCredentialsJson);
  }

  private static boolean hasFederationWithKeyAndGcpIam(CustomerConfigStorageGCSData gcsData) {
    return gcsData.useCrossCloudFederation && hasKeyWithGcpIam(gcsData);
  }
}
