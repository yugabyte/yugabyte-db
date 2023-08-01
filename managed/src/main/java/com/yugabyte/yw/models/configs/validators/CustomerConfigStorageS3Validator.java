// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.amazonaws.SDKGlobalConfiguration;
import com.amazonaws.SdkClientException;
import com.amazonaws.services.s3.AmazonS3;
import com.amazonaws.services.s3.model.AmazonS3Exception;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.AWSUtil;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.CloudUtil.ConfigLocationInfo;
import com.yugabyte.yw.common.CloudUtil.ExtraPermissionToValidate;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.configs.CloudClientsFactory;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data.RegionLocations;
import com.yugabyte.yw.models.helpers.CustomerConfigConsts;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class CustomerConfigStorageS3Validator extends CustomerConfigStorageValidator {

  // Adding http here since S3-compatible storages may use it in endpoint.
  private static final Collection<String> S3_URL_SCHEMES =
      Arrays.asList(new String[] {"http", "https", "s3"});

  private final CloudClientsFactory factory;
  private final AWSUtil awsUtil;
  private final RuntimeConfGetter runtimeConfGetter;
  private final List<ExtraPermissionToValidate> permissions =
      ImmutableList.of(ExtraPermissionToValidate.READ, ExtraPermissionToValidate.LIST);

  @Inject
  public CustomerConfigStorageS3Validator(
      BeanValidator beanValidator,
      CloudClientsFactory factory,
      RuntimeConfGetter runtimeConfGetter,
      AWSUtil awsUtil) {
    super(beanValidator, S3_URL_SCHEMES);
    this.factory = factory;
    this.runtimeConfGetter = runtimeConfGetter;
    this.awsUtil = awsUtil;
  }

  @Override
  public void validate(CustomerConfigData data) {
    super.validate(data);

    CustomerConfigStorageS3Data s3data = (CustomerConfigStorageS3Data) data;
    validateUrl(CustomerConfigConsts.AWS_HOST_BASE_FIELDNAME, s3data.awsHostBase, true, true);

    if (StringUtils.isEmpty(s3data.awsAccessKeyId)
        || StringUtils.isEmpty(s3data.awsSecretAccessKey)) {
      if (!s3data.isIAMInstanceProfile) {
        throwBeanValidatorError(
            CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME,
            "Aws credentials are null and IAM profile is not used.");
      }
    }
    try {
      if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.enforceCertVerificationBackupRestore)) {
        System.setProperty(SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "true");
      }

      AmazonS3 s3Client = null;
      String exceptionMsg = null;
      try {
        s3Client = factory.createS3Client(s3data);
      } catch (AmazonS3Exception s3Exception) {
        exceptionMsg = s3Exception.getErrorMessage();
        throwBeanValidatorError(CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME, exceptionMsg);
      }

      validateBucket(
          s3Client, CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME, s3data.backupLocation);
      if (s3data.regionLocations != null) {
        for (RegionLocations location : s3data.regionLocations) {
          if (StringUtils.isEmpty(location.region)) {
            throwBeanValidatorError(
                CustomerConfigConsts.REGION_FIELDNAME, "This field cannot be empty.");
          }
          validateUrl(
              CustomerConfigConsts.AWS_HOST_BASE_FIELDNAME, location.awsHostBase, true, true);
          validateUrl(
              CustomerConfigConsts.REGION_LOCATION_FIELDNAME, location.location, true, false);
          validateBucket(
              s3Client, CustomerConfigConsts.REGION_LOCATION_FIELDNAME, location.location);
        }
      }

    } finally {
      // Re-enable cert checking as it applies globally
      if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.enforceCertVerificationBackupRestore)) {
        System.setProperty(SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "false");
      }
    }
  }

  private void validateBucket(AmazonS3 client, String fieldName, String s3UriPath) {
    String s3Uri = s3UriPath;

    // Assuming bucket name will always start with s3:// otherwise that will be
    // invalid.
    if (s3UriPath.length() < 5 || !s3UriPath.startsWith("s3://")) {
      String exceptionMsg = "Invalid s3UriPath format: " + s3UriPath;
      throwBeanValidatorError(fieldName, exceptionMsg);
    } else {
      try {
        ConfigLocationInfo configLocationInfo = awsUtil.getConfigLocationInfo(s3UriPath);
        awsUtil.validateOnBucket(
            client, configLocationInfo.bucket, configLocationInfo.cloudPath, permissions);
      } catch (AmazonS3Exception s3Exception) {
        String exceptionMsg = s3Exception.getErrorMessage();
        if (exceptionMsg.contains("Denied") || exceptionMsg.contains("bucket"))
          exceptionMsg += " " + s3Uri;
        throwBeanValidatorError(fieldName, exceptionMsg);
      } catch (SdkClientException e) {
        throwBeanValidatorError(fieldName, e.getMessage());
      }
    }
  }
}
