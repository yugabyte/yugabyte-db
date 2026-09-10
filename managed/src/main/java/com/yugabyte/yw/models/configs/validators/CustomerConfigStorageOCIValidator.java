// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.google.common.collect.HashMultimap;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.SetMultimap;
import com.yugabyte.yw.common.AWSUtil;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.CloudUtil.CloudLocationInfo;
import com.yugabyte.yw.common.CloudUtil.ExtraPermissionToValidate;
import com.yugabyte.yw.common.OCIUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageOCIData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageOCIData.RegionLocations;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.helpers.CustomerConfigConsts;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import javax.inject.Inject;
import org.apache.commons.lang3.StringUtils;
import software.amazon.awssdk.core.exception.SdkClientException;
import software.amazon.awssdk.services.s3.S3Client;
import software.amazon.awssdk.services.s3.model.S3Exception;

public class CustomerConfigStorageOCIValidator extends CustomerConfigStorageValidator {

  private static final Collection<String> OCI_URL_SCHEMES =
      Arrays.asList(new String[] {"https", "s3"});

  // UrlValidator authority check for host bases / s3:// bucket hosts.
  private static final String AUTHORITY_CHARS_REGEX = "^[a-z0-9][a-z0-9._-]{1,}[a-z0-9]$";

  private final AWSUtil awsUtil;
  private final OCIUtil ociUtil;
  private final List<ExtraPermissionToValidate> permissions =
      ImmutableList.of(ExtraPermissionToValidate.READ, ExtraPermissionToValidate.LIST);

  @Inject
  public CustomerConfigStorageOCIValidator(
      BeanValidator beanValidator, AWSUtil awsUtil, OCIUtil ociUtil) {
    super(beanValidator, OCI_URL_SCHEMES, AUTHORITY_CHARS_REGEX);
    this.awsUtil = awsUtil;
    this.ociUtil = ociUtil;
  }

  @Override
  public void validate(CustomerConfigData data) {
    super.validate(data);

    CustomerConfigStorageOCIData ociData = (CustomerConfigStorageOCIData) data;
    boolean hasStaticCreds =
        StringUtils.isNotBlank(ociData.ociS3AccessKeyId)
            || StringUtils.isNotBlank(ociData.ociS3SecretAccessKey)
            || StringUtils.isNotBlank(ociData.ociS3HostBase);

    // Block config creation if both S3-compat creds and USE_OCI_IAM passed, or neither.
    if (!hasStaticCreds ^ ociData.useOciIam) {
      SetMultimap<String, String> validationErrorsMap = HashMultimap.create();
      String xorError =
          "Must pass only one of OCI S3-compatible credentials"
              + " (OCI_S3_ACCESS_KEY_ID/OCI_S3_SECRET_ACCESS_KEY/OCI_S3_HOST_BASE) or"
              + " 'USE_OCI_IAM'.";
      validationErrorsMap.put(CustomerConfigConsts.USE_OCI_IAM_FIELDNAME, xorError);
      validationErrorsMap.put(CustomerConfigConsts.OCI_S3_ACCESS_KEY_ID_FIELDNAME, xorError);
      validationErrorsMap.put(CustomerConfigConsts.OCI_S3_SECRET_ACCESS_KEY_FIELDNAME, xorError);
      validationErrorsMap.put(CustomerConfigConsts.OCI_S3_HOST_BASE_FIELDNAME, xorError);
      throwMultipleBeanConfigDataValidatorError(validationErrorsMap, "storageConfigValidation");
    }

    if (StringUtils.isBlank(ociData.ociRegion)) {
      throwBeanConfigDataValidatorError(
          CustomerConfigConsts.OCI_REGION_FIELDNAME, "This field is required.");
    }

    if (ociData.useOciIam) {
      validateIamConfig(ociData);
    } else {
      validateS3CompatibleConfig(ociData);
    }
  }

  private void validateIamConfig(CustomerConfigStorageOCIData ociData) {
    if (StringUtils.isBlank(ociData.ociNamespace)) {
      throwBeanConfigDataValidatorError(
          CustomerConfigConsts.OCI_NAMESPACE_FIELDNAME,
          "This field is required when USE_OCI_IAM is true.");
    }
    validateOciBackupLocation(
        CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME,
        ociData.backupLocation,
        ociData.ociNamespace);
    if (ociData.regionLocations != null) {
      for (RegionLocations location : ociData.regionLocations) {
        if (StringUtils.isEmpty(location.region)) {
          throwBeanConfigDataValidatorError(
              CustomerConfigConsts.REGION_FIELDNAME, "This field cannot be empty.");
        }
        validateOciBackupLocation(
            CustomerConfigConsts.REGION_LOCATION_FIELDNAME,
            location.location,
            ociData.ociNamespace);
      }
    }
    ociUtil.validateIamConfig(ociData);
  }

  private void validateS3CompatibleConfig(CustomerConfigStorageOCIData ociData) {
    if (StringUtils.isBlank(ociData.ociS3AccessKeyId)
        || StringUtils.isBlank(ociData.ociS3SecretAccessKey)) {
      throwBeanConfigDataValidatorError(
          CustomerConfigConsts.OCI_S3_ACCESS_KEY_ID_FIELDNAME,
          "OCI S3-compatible credentials are null and USE_OCI_IAM is not used.");
    }
    if (StringUtils.isBlank(ociData.ociS3HostBase)) {
      throwBeanConfigDataValidatorError(
          CustomerConfigConsts.OCI_S3_HOST_BASE_FIELDNAME,
          "This field is required for OCI S3-compatible storage configs.");
    }
    validateUrl(
        CustomerConfigConsts.OCI_S3_HOST_BASE_FIELDNAME, ociData.ociS3HostBase, false, true);
    validateOciBackupLocation(
        CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME,
        ociData.backupLocation,
        ociData.ociNamespace);

    CustomerConfigStorageS3Data s3Data = OCIUtil.toS3CompatibleData(ociData);
    try {
      validateS3Bucket(
          CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME,
          s3Data,
          YbcBackupUtil.DEFAULT_REGION_STRING);
      if (ociData.regionLocations != null) {
        for (RegionLocations location : ociData.regionLocations) {
          if (StringUtils.isEmpty(location.region)) {
            throwBeanConfigDataValidatorError(
                CustomerConfigConsts.REGION_FIELDNAME, "This field cannot be empty.");
          }
          if (StringUtils.isBlank(location.ociS3HostBase)) {
            throwBeanConfigDataValidatorError(
                CustomerConfigConsts.OCI_S3_HOST_BASE_FIELDNAME,
                "This field is required for each OCI region location"
                    + " (Object Storage buckets are regional).");
          }
          validateUrl(
              CustomerConfigConsts.OCI_S3_HOST_BASE_FIELDNAME, location.ociS3HostBase, false, true);
          validateUrl(
              CustomerConfigConsts.REGION_LOCATION_FIELDNAME, location.location, true, false);
          validateOciBackupLocation(
              CustomerConfigConsts.REGION_LOCATION_FIELDNAME,
              location.location,
              ociData.ociNamespace);
          validateS3Bucket(CustomerConfigConsts.REGION_LOCATION_FIELDNAME, s3Data, location.region);
        }
      }
    } catch (S3Exception e) {
      throwBeanConfigDataValidatorError(
          CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME, e.getMessage());
    }
  }

  /** Accepts {@code s3://bucket[/path]} or native HTTPS Object Storage URLs. */
  private void validateOciBackupLocation(String fieldName, String location, String ociNamespace) {
    if (StringUtils.isBlank(location)) {
      throwBeanConfigDataValidatorError(fieldName, "This field cannot be empty.");
      return;
    }
    if (location.startsWith(AWSUtil.AWS_S3_LOCATION_PREFIX)) {
      String[] split = OCIUtil.getSplitLocationValue(location);
      if (split.length == 0 || StringUtils.isBlank(split[0])) {
        throwBeanConfigDataValidatorError(fieldName, "Invalid OCI S3 URI path format: " + location);
      }
      return;
    }
    if (location.startsWith(OCIUtil.OCI_NATIVE_LOCATION_PREFIX)) {
      OCIUtil.OciNativeUrlParts parts;
      try {
        parts = OCIUtil.parseNativeOciUrl(location);
      } catch (PlatformServiceException e) {
        throwBeanConfigDataValidatorError(fieldName, e.getMessage());
        return;
      }
      if (StringUtils.isNotBlank(ociNamespace)
          && !StringUtils.equals(parts.namespace, ociNamespace)) {
        throwBeanConfigDataValidatorError(
            CustomerConfigConsts.OCI_NAMESPACE_FIELDNAME,
            String.format(
                "OCI_NAMESPACE '%s' must match namespace '%s' in the backup location URL.",
                ociNamespace, parts.namespace));
      }
      return;
    }
    throwBeanConfigDataValidatorError(fieldName, "Invalid OCI backup location format: " + location);
  }

  private void validateS3Bucket(
      String fieldName, CustomerConfigStorageS3Data s3Data, String region) {
    String s3UriPath = awsUtil.getRegionLocationsMap(s3Data).get(region);
    if (s3UriPath.length() < 5 || !s3UriPath.startsWith(AWSUtil.AWS_S3_LOCATION_PREFIX)) {
      throwBeanConfigDataValidatorError(fieldName, "Invalid OCI S3 URI path format: " + s3UriPath);
    }
    try (S3Client client = awsUtil.createS3Client(s3Data, region)) {
      CloudLocationInfo configLocationInfo =
          awsUtil.getCloudLocationInfo(region, s3Data, s3UriPath);
      awsUtil.validateOnBucket(
          client,
          configLocationInfo.bucket,
          configLocationInfo.cloudPath,
          permissions,
          s3Data.immutableStorage);
    } catch (S3Exception s3Exception) {
      String exceptionMsg = s3Exception.getMessage();
      if (exceptionMsg != null
          && (exceptionMsg.contains("Denied") || exceptionMsg.contains("bucket"))) {
        exceptionMsg += " " + s3UriPath;
      }
      throwBeanConfigDataValidatorError(fieldName, exceptionMsg);
    } catch (SdkClientException e) {
      throwBeanConfigDataValidatorError(fieldName, e.getMessage());
    }
  }
}
