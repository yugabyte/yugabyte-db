// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.PRECONDITION_FAILED;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.amazonaws.SdkClientException;
import com.amazonaws.auth.AWSCredentials;
import com.amazonaws.auth.AWSCredentialsProvider;
import com.amazonaws.auth.AWSCredentialsProviderChain;
import com.amazonaws.auth.AWSSessionCredentials;
import com.amazonaws.auth.AWSStaticCredentialsProvider;
import com.amazonaws.auth.BasicAWSCredentials;
import com.amazonaws.auth.EC2ContainerCredentialsProviderWrapper;
import com.amazonaws.auth.InstanceProfileCredentialsProvider;
import com.amazonaws.auth.WebIdentityTokenCredentialsProvider;
import com.amazonaws.auth.profile.ProfileCredentialsProvider;
import com.amazonaws.client.builder.AwsClientBuilder.EndpointConfiguration;
import com.amazonaws.regions.DefaultAwsRegionProviderChain;
import com.amazonaws.regions.Region;
import com.amazonaws.services.identitymanagement.AmazonIdentityManagement;
import com.amazonaws.services.identitymanagement.AmazonIdentityManagementClient;
import com.amazonaws.services.identitymanagement.AmazonIdentityManagementClientBuilder;
import com.amazonaws.services.identitymanagement.model.GetRoleRequest;
import com.amazonaws.services.identitymanagement.model.Role;
import com.amazonaws.services.s3.AmazonS3;
import com.amazonaws.services.s3.AmazonS3Client;
import com.amazonaws.services.s3.AmazonS3ClientBuilder;
import com.amazonaws.services.s3.model.AmazonS3Exception;
import com.amazonaws.services.s3.model.Bucket;
import com.amazonaws.services.s3.model.DeleteObjectsRequest;
import com.amazonaws.services.s3.model.DeleteObjectsRequest.KeyVersion;
import com.amazonaws.services.s3.model.GetBucketLocationRequest;
import com.amazonaws.services.s3.model.ListObjectsV2Result;
import com.amazonaws.services.s3.model.S3Object;
import com.amazonaws.services.s3.model.S3ObjectSummary;
import com.amazonaws.services.securitytoken.AWSSecurityTokenService;
import com.amazonaws.services.securitytoken.AWSSecurityTokenServiceClient;
import com.amazonaws.services.securitytoken.AWSSecurityTokenServiceClientBuilder;
import com.amazonaws.services.securitytoken.model.AssumeRoleRequest;
import com.amazonaws.services.securitytoken.model.AssumeRoleResult;
import com.amazonaws.services.securitytoken.model.AssumeRoleWithWebIdentityRequest;
import com.amazonaws.services.securitytoken.model.AssumeRoleWithWebIdentityResult;
import com.amazonaws.services.securitytoken.model.Credentials;
import com.amazonaws.services.securitytoken.model.GetCallerIdentityRequest;
import com.amazonaws.services.securitytoken.model.GetCallerIdentityResult;
import com.amazonaws.util.EC2MetadataUtils;
import com.amazonaws.util.EC2MetadataUtils.IAMSecurityCredential;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data.IAMConfiguration;

import io.ebean.annotation.EnumValue;

import java.io.File;
import java.io.InputStream;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.sql.Timestamp;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TimeZone;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.collections.MapUtils;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.ybc.CloudStoreSpec;

@Singleton
@Slf4j
public class AWSUtil implements CloudUtil {

  @Inject RuntimeConfigFactory runtimeConfigFactory;
  @Inject IAMTemporaryCredentialsProvider iamCredsProvider;

  public static final String AWS_ACCESS_KEY_ID_FIELDNAME = "AWS_ACCESS_KEY_ID";
  public static final String AWS_SECRET_ACCESS_KEY_FIELDNAME = "AWS_SECRET_ACCESS_KEY";
  private static final String AWS_REGION_SPECIFIC_HOST_BASE_FORMAT = "s3.%s.amazonaws.com";
  private static final String AWS_STANDARD_HOST_BASE_PATTERN = "s3([.](.+)|)[.]amazonaws[.]com";
  public static final String AWS_DEFAULT_REGION = "us-east-1";
  public static final String AWS_DEFAULT_ENDPOINT = "s3.amazonaws.com";
  public static final String AWS_DEFAULT_REGIONAL_STS_ENDPOINT = "sts.us-east-1.amazonaws.com";

  public static final String YBC_AWS_ACCESS_KEY_ID_FIELDNAME = "AWS_ACCESS_KEY_ID";
  public static final String YBC_AWS_SECRET_ACCESS_KEY_FIELDNAME = "AWS_SECRET_ACCESS_KEY";
  public static final String YBC_AWS_PATH_STYLE_ACCESS = "PATH_STYLE_ACCESS";
  public static final String YBC_AWS_ENDPOINT_FIELDNAME = "AWS_ENDPOINT";
  public static final String YBC_AWS_DEFAULT_REGION_FIELDNAME = "AWS_DEFAULT_REGION";
  public static final String YBC_AWS_ACCESS_TOKEN_FIELDNAME = "AWS_ACCESS_TOKEN";

  private static final String ZULU_TIME_FORMAT = "yyyy-MM-dd'T'HH:mm:ss'Z'";
  private static final Pattern standardHostBaseCompiled =
      Pattern.compile(AWS_STANDARD_HOST_BASE_PATTERN);

  // This method is a way to check if given S3 config can extract objects.
  @Override
  public boolean canCredentialListObjects(CustomerConfigData configData, List<String> locations) {
    if (CollectionUtils.isEmpty(locations)) {
      return true;
    }
    for (String location : locations) {
      try {
        AmazonS3 s3Client = createS3Client((CustomerConfigStorageS3Data) configData);
        String[] bucketSplit = getSplitLocationValue(location);
        String bucketName = bucketSplit.length > 0 ? bucketSplit[0] : "";
        String prefix = bucketSplit.length > 1 ? bucketSplit[1] : "";
        if (bucketSplit.length == 1) {
          Boolean doesBucketExist = s3Client.doesBucketExistV2(bucketName);
          if (!doesBucketExist) {
            throw new RuntimeException(
                String.format("No S3 bucket found with name %s", bucketName));
          }
        } else {
          ListObjectsV2Result result = s3Client.listObjectsV2(bucketName, prefix);
          if (result.getKeyCount() == 0) {
            log.error("No objects exists within bucket {}", bucketName);
          }
        }
      } catch (SdkClientException e) {
        log.error(
            String.format(
                "Credential cannot list objects in the specified backup location %s: {}", location),
            e.getMessage());
        return false;
      }
    }
    return true;
  }

  @Override
  public void checkStoragePrefixValidity(String configLocation, String backupLocation) {
    String[] configLocationSplit = getSplitLocationValue(configLocation);
    String[] backupLocationSplit = getSplitLocationValue(backupLocation);
    // Buckets should be same in any case.
    if (!StringUtils.equals(configLocationSplit[0], backupLocationSplit[0])) {
      throw new PlatformServiceException(
          PRECONDITION_FAILED,
          String.format(
              "Config bucket %s and backup location bucket %s do not match",
              configLocationSplit[0], backupLocationSplit[0]));
    }
  }

  @Override
  public void deleteKeyIfExists(CustomerConfigData configData, String defaultBackupLocation)
      throws Exception {
    String[] splitLocation = getSplitLocationValue(defaultBackupLocation);
    String bucketName = splitLocation[0];
    String objectPrefix = splitLocation[1];
    String keyLocation =
        objectPrefix.substring(0, objectPrefix.lastIndexOf('/')) + KEY_LOCATION_SUFFIX;
    try {
      AmazonS3 s3Client = createS3Client((CustomerConfigStorageS3Data) configData);
      ListObjectsV2Result listObjectsResult = s3Client.listObjectsV2(bucketName, keyLocation);
      if (listObjectsResult.getKeyCount() == 0) {
        log.info("Specified Location " + keyLocation + " does not contain objects");
        return;
      } else {
        log.debug("Retrieved blobs info for bucket " + bucketName + " with prefix " + keyLocation);
        retrieveAndDeleteObjects(listObjectsResult, bucketName, s3Client);
      }
    } catch (AmazonS3Exception e) {
      log.error("Error while deleting key object from bucket " + bucketName, e.getErrorMessage());
      throw e;
    }
  }

  public static String[] getSplitLocationValue(String location) {
    location = location.substring(5);
    String[] split = location.split("/", 2);
    return split;
  }

  public static String getClientRegion(String fallbackRegion) {
    String region = "";
    try {
      region = new DefaultAwsRegionProviderChain().getRegion();
    } catch (SdkClientException e) {
      log.info("No region found in Default region chain.");
    }
    return StringUtils.isBlank(region) ? fallbackRegion : region;
  }

  public static AmazonS3 createS3Client(CustomerConfigStorageS3Data s3Data)
      throws AmazonS3Exception, PlatformServiceException {
    AmazonS3ClientBuilder s3ClientBuilder = AmazonS3ClientBuilder.standard();
    if (s3Data.isIAMInstanceProfile) {
      // Using credential chaining here.
      // This first looks for K8s service account IAM role,
      // then IAM user,
      // then falls back to the Node/EC2 IAM role.
      try {
        s3ClientBuilder.withCredentials(
            new AWSStaticCredentialsProvider(
                new IAMTemporaryCredentialsProvider().getTemporaryCredentials(s3Data)));
      } catch (Exception e) {
        log.error("Fetching IAM credentials failed: {}", e.getMessage());
        throw new PlatformServiceException(PRECONDITION_FAILED, e.getMessage());
      }
    } else {
      String key = s3Data.awsAccessKeyId;
      String secret = s3Data.awsSecretAccessKey;
      AWSCredentials credentials = new BasicAWSCredentials(key, secret);
      AWSCredentialsProvider creds = new AWSStaticCredentialsProvider(credentials);
      s3ClientBuilder.withCredentials(creds);
    }
    s3ClientBuilder.withForceGlobalBucketAccessEnabled(true);
    String endpoint = s3Data.awsHostBase;
    String region = getClientRegion(s3Data.fallbackRegion);
    if (StringUtils.isNotBlank(endpoint)) {
      // Need to set region because region-chaining may
      // fail if correct environment variables not found.
      s3ClientBuilder.withEndpointConfiguration(new EndpointConfiguration(endpoint, region));
    } else {
      s3ClientBuilder.withRegion(region);
    }
    try {
      return s3ClientBuilder.build();
    } catch (SdkClientException e) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Failed to create S3 client, error: %s", e.getMessage()));
    }
  }

  public String getBucketRegion(String bucketName, CustomerConfigStorageS3Data s3Data)
      throws SdkClientException {
    AmazonS3 client = createS3Client(s3Data);
    return getBucketRegion(bucketName, client);
  }

  // For reusing already created client, as in listBuckets function
  private String getBucketRegion(String bucketName, AmazonS3 s3Client) throws SdkClientException {
    try {
      GetBucketLocationRequest locationRequest = new GetBucketLocationRequest(bucketName);
      String bucketRegion = s3Client.getBucketLocation(locationRequest);
      if (bucketRegion.equals("US")) {
        bucketRegion = AWS_DEFAULT_REGION;
      }
      return bucketRegion;
    } catch (SdkClientException e) {
      log.error(String.format("Fetching bucket region for %s failed", bucketName), e.getMessage());
      throw e;
    }
  }

  public String createBucketRegionSpecificHostBase(String bucketName, String bucketRegion) {
    String regionSpecificHostBase =
        String.format(AWS_REGION_SPECIFIC_HOST_BASE_FORMAT, bucketRegion);
    return regionSpecificHostBase;
  }

  public boolean isHostBaseS3Standard(String hostBase) {
    return standardHostBaseCompiled.matcher(hostBase).matches();
  }

  public String getOrCreateHostBase(
      CustomerConfigStorageS3Data s3Data, String bucketName, String bucketRegion) {
    String hostBase = s3Data.awsHostBase;
    if (StringUtils.isEmpty(hostBase) || hostBase.equals(AWS_DEFAULT_ENDPOINT)) {
      hostBase = createBucketRegionSpecificHostBase(bucketName, bucketRegion);
    }
    return hostBase;
  }

  @Override
  public void deleteStorage(CustomerConfigData configData, List<String> backupLocations)
      throws Exception {
    for (String backupLocation : backupLocations) {
      try {
        AmazonS3 s3Client = createS3Client((CustomerConfigStorageS3Data) configData);
        String[] splitLocation = getSplitLocationValue(backupLocation);
        String bucketName = splitLocation[0];
        String objectPrefix = splitLocation[1];
        String nextContinuationToken = null;
        do {
          ListObjectsV2Result listObjectsResult = s3Client.listObjectsV2(bucketName, objectPrefix);
          if (listObjectsResult.getKeyCount() == 0) {
            break;
          }
          nextContinuationToken = null;
          if (listObjectsResult.isTruncated()) {
            nextContinuationToken = listObjectsResult.getContinuationToken();
          }
          log.debug(
              "Retrieved blobs info for bucket " + bucketName + " with prefix " + objectPrefix);
          retrieveAndDeleteObjects(listObjectsResult, bucketName, s3Client);
        } while (nextContinuationToken != null);
      } catch (AmazonS3Exception e) {
        log.error(" Error in deleting objects at location " + backupLocation, e.getErrorMessage());
        throw e;
      }
    }
  }

  @Override
  public InputStream getCloudFileInputStream(CustomerConfigData configData, String cloudPath)
      throws Exception {
    AmazonS3 s3Client = createS3Client((CustomerConfigStorageS3Data) configData);
    String[] splitLocation = getSplitLocationValue(cloudPath);
    String bucketName = splitLocation[0];
    String objectPrefix = splitLocation[1];
    S3Object object = s3Client.getObject(bucketName, objectPrefix);
    if (object == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "No object was found at the specified location: " + cloudPath);
    }
    return object.getObjectContent();
  }

  public void retrieveAndDeleteObjects(
      ListObjectsV2Result listObjectsResult, String bucketName, AmazonS3 s3Client)
      throws AmazonS3Exception {
    List<S3ObjectSummary> objectSummary = listObjectsResult.getObjectSummaries();
    List<DeleteObjectsRequest.KeyVersion> objectKeys =
        objectSummary
            .parallelStream()
            .map(o -> new KeyVersion(o.getKey()))
            .collect(Collectors.toList());
    DeleteObjectsRequest deleteRequest =
        new DeleteObjectsRequest(bucketName).withKeys(objectKeys).withQuiet(false);
    s3Client.deleteObjects(deleteRequest);
  }

  @Override
  public CloudStoreSpec createCloudStoreSpec(
      String storageLocation,
      String commonDir,
      String previousBackupLocation,
      CustomerConfigData configData) {
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    String[] splitValues = getSplitLocationValue(storageLocation);
    String bucket = splitValues[0];
    String cloudDir =
        splitValues.length > 1
            ? BackupUtil.getCloudpathWithConfigSuffix(splitValues[1], commonDir)
            : commonDir;
    cloudDir = BackupUtil.appendSlash(cloudDir);
    String previousCloudDir = "";
    if (StringUtils.isNotBlank(previousBackupLocation)) {
      splitValues = getSplitLocationValue(previousBackupLocation);
      previousCloudDir =
          splitValues.length > 1 ? BackupUtil.appendSlash(splitValues[1]) : previousCloudDir;
    }
    Map<String, String> s3CredsMap = createCredsMapYbc(s3Data, bucket);
    return YbcBackupUtil.buildCloudStoreSpec(
        bucket, cloudDir, previousCloudDir, s3CredsMap, Util.S3);
  }

  @Override
  public CloudStoreSpec createRestoreCloudStoreSpec(
      String storageLocation, String cloudDir, CustomerConfigData configData, boolean isDsm) {
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    String[] splitValues = getSplitLocationValue(storageLocation);
    String bucket = splitValues[0];
    Map<String, String> s3CredsMap = createCredsMapYbc(s3Data, bucket);
    if (isDsm) {
      String location = BackupUtil.appendSlash(splitValues[1]);
      return YbcBackupUtil.buildCloudStoreSpec(bucket, location, "", s3CredsMap, Util.S3);
    }
    return YbcBackupUtil.buildCloudStoreSpec(bucket, cloudDir, "", s3CredsMap, Util.S3);
  }

  private Map<String, String> createCredsMapYbc(CustomerConfigData configData, String bucket) {
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    Map<String, String> s3CredsMap = new HashMap<>();
    if (s3Data.isIAMInstanceProfile) {
      fillMapWithIAMCreds(s3CredsMap, s3Data);
    } else {
      s3CredsMap.put(YBC_AWS_ACCESS_KEY_ID_FIELDNAME, s3Data.awsAccessKeyId);
      s3CredsMap.put(YBC_AWS_SECRET_ACCESS_KEY_FIELDNAME, s3Data.awsSecretAccessKey);
    }
    String bucketRegion = null;
    try {
      bucketRegion = getBucketRegion(bucket, s3Data);
    } catch (SdkClientException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format(
              "Failed to retrieve region of Bucket %s, error: %s", bucket, e.getMessage()));
    }
    String hostBase = getOrCreateHostBase(s3Data, bucket, bucketRegion);
    s3CredsMap.put(YBC_AWS_ENDPOINT_FIELDNAME, hostBase);
    s3CredsMap.put(YBC_AWS_DEFAULT_REGION_FIELDNAME, bucketRegion);
    return s3CredsMap;
  }

  private void fillMapWithIAMCreds(
      Map<String, String> s3CredsMap, CustomerConfigStorageS3Data s3Data) {
    try {
      AWSCredentials creds = iamCredsProvider.getTemporaryCredentials(s3Data);
      s3CredsMap.put(YBC_AWS_ACCESS_KEY_ID_FIELDNAME, creds.getAWSAccessKeyId());
      s3CredsMap.put(YBC_AWS_SECRET_ACCESS_KEY_FIELDNAME, creds.getAWSSecretKey());
      if (creds instanceof AWSSessionCredentials) {
        s3CredsMap.put(
            YBC_AWS_ACCESS_TOKEN_FIELDNAME, ((AWSSessionCredentials) creds).getSessionToken());
      }
    } catch (Exception e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format("Failed to retrieve IAM credentials, error: %s", e.getMessage()));
    }
  }

  @Override
  public Map<String, String> listBuckets(CustomerConfigData configData) {
    Map<String, String> bucketHostBaseMap = new HashMap<>();
    try {
      CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
      if ((StringUtils.isBlank(s3Data.awsAccessKeyId)
              || StringUtils.isBlank(s3Data.awsSecretAccessKey))
          && s3Data.isIAMInstanceProfile == false) {
        return bucketHostBaseMap;
      }
      final boolean useOriginalHostBase =
          StringUtils.isNotBlank(s3Data.awsHostBase) && !isHostBaseS3Standard(s3Data.awsHostBase);
      // don't use standard host base with regions, conflicting regions will lead to authorization
      // errors.
      if (!useOriginalHostBase) {
        s3Data.awsHostBase = null;
      }
      AmazonS3 client = createS3Client(s3Data);
      List<Bucket> buckets = client.listBuckets();
      buckets
          .parallelStream()
          .forEach(
              b ->
                  bucketHostBaseMap.put(
                      b.getName(),
                      useOriginalHostBase
                          ? s3Data.awsHostBase
                          : createBucketRegionSpecificHostBase(
                              b.getName(), getBucketRegion(b.getName(), client))));
    } catch (SdkClientException e) {
      log.error("Error while listing S3 buckets {}", e.getMessage());
    }
    return bucketHostBaseMap;
  }

  public Map<String, String> getRegionLocationsMap(CustomerConfigData configData) {
    Map<String, String> regionLocationsMap = new HashMap<>();
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    if (CollectionUtils.isNotEmpty(s3Data.regionLocations)) {
      s3Data.regionLocations.stream().forEach(rL -> regionLocationsMap.put(rL.region, rL.location));
    }
    return regionLocationsMap;
  }
}
