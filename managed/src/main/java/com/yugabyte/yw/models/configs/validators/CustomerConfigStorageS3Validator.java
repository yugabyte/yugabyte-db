// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.amazonaws.SDKGlobalConfiguration;
import com.amazonaws.SdkClientException;
import com.amazonaws.services.s3.AmazonS3;
import com.amazonaws.services.s3.model.AmazonS3Exception;
import com.amazonaws.services.s3.model.ListObjectsV2Result;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.models.configs.CloudClientsFactory;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data.RegionLocations;
import com.yugabyte.yw.models.helpers.CustomerConfigConsts;
import java.util.Arrays;
import java.util.Collection;
import javax.inject.Inject;
import org.apache.commons.lang.StringUtils;
import play.libs.Json;

public class CustomerConfigStorageS3Validator extends CustomerConfigStorageValidator {

  // Adding http here since S3-compatible storages may use it in endpoint.
  private static final Collection<String> S3_URL_SCHEMES =
      Arrays.asList(new String[] {"http", "https", "s3"});

  private final CloudClientsFactory factory;

  @Inject
  public CustomerConfigStorageS3Validator(
      BeanValidator beanValidator, CloudClientsFactory factory) {
    super(beanValidator, S3_URL_SCHEMES);
    this.factory = factory;
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
      // Disable cert checking while connecting with s3
      // Enabling it can potentially fail when s3 compatible storages like
      // Dell ECS are provided and custom certs are needed to connect
      // Reference: https://yugabyte.atlassian.net/browse/PLAT-2497
      System.setProperty(SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "true");

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
      System.setProperty(SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "false");
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
        s3UriPath = s3UriPath.substring(5);
        String[] bucketSplit = s3UriPath.split("/", 2);
        String bucketName = bucketSplit.length > 0 ? bucketSplit[0] : "";
        String prefix = bucketSplit.length > 1 ? bucketSplit[1] : "";

        // Only the bucket has been given, with no subdir.
        if (bucketSplit.length == 1) {
          if (!client.doesBucketExistV2(bucketName)) {
            String exceptionMsg = "S3 URI path " + s3Uri + " doesn't exist";
            throwBeanValidatorError(fieldName, exceptionMsg);
          }
        } else {
          ListObjectsV2Result result = client.listObjectsV2(bucketName, prefix);
          if (result.getKeyCount() == 0) {
            String exceptionMsg = "S3 URI path " + s3Uri + " doesn't exist";
            throwBeanValidatorError(fieldName, exceptionMsg);
          }
        }
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
