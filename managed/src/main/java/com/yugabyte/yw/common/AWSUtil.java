// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.amazonaws.SdkClientException;

import com.amazonaws.auth.AWSCredentials;
import com.amazonaws.auth.AWSCredentialsProvider;
import com.amazonaws.auth.AWSStaticCredentialsProvider;
import com.amazonaws.auth.BasicAWSCredentials;
import com.amazonaws.auth.BasicSessionCredentials;
import com.amazonaws.auth.InstanceProfileCredentialsProvider;
import com.amazonaws.client.builder.AwsClientBuilder.EndpointConfiguration;
import com.amazonaws.services.ec2.model.AmazonEC2Exception;
import com.amazonaws.services.identitymanagement.AmazonIdentityManagement;
import com.amazonaws.services.identitymanagement.AmazonIdentityManagementClient;
import com.amazonaws.services.identitymanagement.model.AmazonIdentityManagementException;
import com.amazonaws.services.identitymanagement.model.GetRoleRequest;
import com.amazonaws.services.identitymanagement.model.Role;
import com.amazonaws.services.s3.AmazonS3;
import com.amazonaws.services.s3.AmazonS3Client;
import com.amazonaws.services.s3.AmazonS3ClientBuilder;
import com.amazonaws.services.s3.model.AmazonS3Exception;
import com.amazonaws.services.s3.model.Bucket;
import com.amazonaws.services.s3.model.DeleteObjectsRequest;
import com.amazonaws.services.s3.model.GetBucketLocationRequest;
import com.amazonaws.services.s3.model.DeleteObjectsRequest.KeyVersion;
import com.amazonaws.services.securitytoken.AWSSecurityTokenService;
import com.amazonaws.services.securitytoken.AWSSecurityTokenServiceClient;
import com.amazonaws.services.securitytoken.AWSSecurityTokenServiceClientBuilder;
import com.amazonaws.services.securitytoken.model.AssumeRoleRequest;
import com.amazonaws.services.securitytoken.model.AssumeRoleResult;
import com.amazonaws.services.securitytoken.model.Credentials;
import com.amazonaws.util.EC2MetadataUtils;
import com.amazonaws.util.EC2MetadataUtils.IAMSecurityCredential;
import com.amazonaws.services.s3.model.ListObjectsV2Result;
import com.amazonaws.services.s3.model.S3ObjectSummary;
import com.google.inject.Singleton;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import java.sql.Timestamp;
import java.text.SimpleDateFormat;
import java.time.ZoneId;
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
import org.apache.commons.lang3.StringUtils;
import org.yb.ybc.CloudStoreSpec;

@Singleton
@Slf4j
public class AWSUtil implements CloudUtil {

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
            log.error("No bucket exists with name {}", bucketName);
          }
        } else {
          ListObjectsV2Result result = s3Client.listObjectsV2(bucketName, prefix);
          if (result.getKeyCount() == 0) {
            log.error("No objects exists within bucket {}", bucketName);
          }
        }
      } catch (AmazonS3Exception e) {
        log.error(
            String.format(
                "Credential cannot list objects in the specified backup location %s", location),
            e.getErrorMessage());
        return false;
      }
    }
    return true;
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

  // Fetch temporary credentials from EC2 metadata.
  private Credentials getTemporaryCredentialsInstanceProfile() throws Exception {
    Map<String, IAMSecurityCredential> instanceProfileCredentials =
        EC2MetadataUtils.getIAMSecurityCredentials();
    Credentials credentials = null;
    if (MapUtils.isNotEmpty(instanceProfileCredentials)
        && instanceProfileCredentials.values().iterator().hasNext()) {
      IAMSecurityCredential credential = instanceProfileCredentials.values().iterator().next();
      SimpleDateFormat formatter = new SimpleDateFormat(ZULU_TIME_FORMAT);
      formatter.setTimeZone(TimeZone.getTimeZone("UTC"));
      Date date = formatter.parse(credential.expiration);
      credentials =
          new Credentials(
              credential.accessKeyId, credential.secretAccessKey, credential.token, date);
    }
    return credentials;
  }

  // Fetch temporary credentials using Assume role via STS client.
  private synchronized Credentials getTemporaryCredentialsAssumeRole() throws Exception {
    // Create STS client to make subsequent calls.
    EndpointConfiguration ec =
        new EndpointConfiguration(AWS_DEFAULT_REGIONAL_STS_ENDPOINT, AWS_DEFAULT_REGION);
    AWSSecurityTokenServiceClientBuilder stsClientBuilder = AWSSecurityTokenServiceClient.builder();
    stsClientBuilder.withEndpointConfiguration(ec);
    AWSCredentialsProvider creds = new InstanceProfileCredentialsProvider(false);
    AWSSecurityTokenService stsService = stsClientBuilder.withCredentials(creds).build();

    // Fetch role name for the IAM instance, should be same as instance-profile name.
    EC2MetadataUtils.IAMInfo iamInfo = EC2MetadataUtils.getIAMInstanceProfileInfo();
    String instanceProfileArn = iamInfo.instanceProfileArn;
    String[] arnSplit = instanceProfileArn.split("/", 0);
    String role = arnSplit[arnSplit.length - 1];

    // Fetch max session limit for the role.
    AmazonIdentityManagement iamClient =
        AmazonIdentityManagementClient.builder().withCredentials(creds).build();
    Role iamRole = iamClient.getRole(new GetRoleRequest().withRoleName(role)).getRole();
    int maxDuration = iamRole.getMaxSessionDuration();

    // Generate temporary credentials valid until the max session duration
    // required because we are sending creds to nodes, no mechanism to fetch creds via instance
    // there.
    Timestamp timestamp = new Timestamp(System.currentTimeMillis());
    AssumeRoleRequest roleRequest =
        new AssumeRoleRequest()
            .withDurationSeconds(maxDuration)
            .withRoleArn(iamRole.getArn())
            .withRoleSessionName(Long.toString(timestamp.toInstant().toEpochMilli()));
    AssumeRoleResult roleResult = stsService.assumeRole(roleRequest);
    Credentials temporaryCredentials = roleResult.getCredentials();
    return temporaryCredentials;
  }

  private Credentials getTemporaryCredentials() {
    Credentials instanceCredentials = null;
    Credentials assumeRoleCredentials = null;
    try {
      assumeRoleCredentials = getTemporaryCredentialsAssumeRole();
    } catch (Exception e) {
      log.error("Fetching temporary credentials from STS client failed: {}", e.getMessage());
    }
    try {
      instanceCredentials = getTemporaryCredentialsInstanceProfile();
    } catch (Exception e) {
      log.error("Fetching instance credentials failed: {}", e.getMessage());
    }
    if (assumeRoleCredentials != null) {
      if (assumeRoleCredentials.getExpiration().compareTo(instanceCredentials.getExpiration())
          >= 0) {
        log.info(
            "Using assume role credentials with expiry: {}",
            assumeRoleCredentials.getExpiration().toString());
        return assumeRoleCredentials;
      }
      log.info(
          "Assume role expiry: {} is less than instance profile credentials expiry: {},"
              + "using instance profile credentials",
          assumeRoleCredentials.getExpiration().toString(),
          instanceCredentials.getExpiration().toString());
      return instanceCredentials;
    }
    log.info(
        "Unable to assume Role, defaulting to intance profile credentials with expiry: {}",
        instanceCredentials.getExpiration().toString());
    return instanceCredentials;
  }

  public static AmazonS3 createS3Client(CustomerConfigStorageS3Data s3Data)
      throws AmazonS3Exception {
    AmazonS3ClientBuilder s3ClientBuilder = AmazonS3Client.builder();
    AWSCredentialsProvider creds = null;
    if (s3Data.isIAMInstanceProfile) {
      // Using instance creds from ec2.services.com here
      // since the client is used on Platform itself unlike backups.
      creds = new InstanceProfileCredentialsProvider(false);
    } else {
      String key = s3Data.awsAccessKeyId;
      String secret = s3Data.awsSecretAccessKey;
      AWSCredentials credentials = new BasicAWSCredentials(key, secret);
      creds = new AWSStaticCredentialsProvider(credentials);
    }
    s3ClientBuilder.withCredentials(creds).withForceGlobalBucketAccessEnabled(true);
    EndpointConfiguration endpointConfiguration = null;
    String endpoint = s3Data.awsHostBase;
    if (StringUtils.isNotBlank(endpoint)) {
      // Need to set default region because region-chaining may
      // fail if correct environment variables not found.
      endpointConfiguration = new EndpointConfiguration(endpoint, AWS_DEFAULT_REGION);
    } else {
      endpointConfiguration = new EndpointConfiguration(AWS_DEFAULT_ENDPOINT, AWS_DEFAULT_REGION);
    }
    s3ClientBuilder.withEndpointConfiguration(endpointConfiguration);
    return s3ClientBuilder.build();
  }

  public String getBucketRegion(String bucketName, CustomerConfigStorageS3Data s3Data)
      throws AmazonS3Exception {
    try {
      AmazonS3 client = createS3Client(s3Data);
      return getBucketRegion(bucketName, client);
    } catch (AmazonS3Exception e) {
      throw e;
    }
  }

  // For reusing already created client, as in listBuckets function
  private String getBucketRegion(String bucketName, AmazonS3 s3Client) {
    try {
      GetBucketLocationRequest locationRequest = new GetBucketLocationRequest(bucketName);
      String bucketRegion = s3Client.getBucketLocation(locationRequest);
      if (bucketRegion.equals("US")) {
        bucketRegion = AWS_DEFAULT_REGION;
      }
      return bucketRegion;
    } catch (AmazonS3Exception e) {
      log.error(
          String.format("Fetching bucket region for %s failed", bucketName), e.getErrorMessage());
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
      String backupLocation, String commonDir, CustomerConfigData configData) {
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    String[] splitValues = getSplitLocationValue(backupLocation);
    String bucket = splitValues[0];
    String cloudDir = splitValues.length > 1 ? splitValues[1] : "";
    if (StringUtils.isNotBlank(cloudDir)) {
      cloudDir = String.format("%s/%s/", splitValues[1], commonDir);
    } else {
      cloudDir = commonDir.concat("/");
    }
    Map<String, String> s3CredsMap = createCredsMapYbc(s3Data, bucket);
    return YbcBackupUtil.buildCloudStoreSpec(bucket, cloudDir, s3CredsMap, Util.S3);
  }

  private Map<String, String> createCredsMapYbc(CustomerConfigData configData, String bucket) {
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    Map<String, String> s3CredsMap = new HashMap<>();
    if (s3Data.isIAMInstanceProfile) {
      Credentials temporaryCredentials = getTemporaryCredentials();
      s3CredsMap.put(YBC_AWS_ACCESS_TOKEN_FIELDNAME, temporaryCredentials.getSessionToken());
      s3CredsMap.put(YBC_AWS_ACCESS_KEY_ID_FIELDNAME, temporaryCredentials.getAccessKeyId());
      s3CredsMap.put(
          YBC_AWS_SECRET_ACCESS_KEY_FIELDNAME, temporaryCredentials.getSecretAccessKey());
    } else {
      s3CredsMap.put(YBC_AWS_ACCESS_KEY_ID_FIELDNAME, s3Data.awsAccessKeyId);
      s3CredsMap.put(YBC_AWS_SECRET_ACCESS_KEY_FIELDNAME, s3Data.awsSecretAccessKey);
    }
    String bucketRegion = getBucketRegion(bucket, s3Data);
    String hostBase = getOrCreateHostBase(s3Data, bucket, bucketRegion);
    s3CredsMap.put(YBC_AWS_ENDPOINT_FIELDNAME, hostBase);
    s3CredsMap.put(YBC_AWS_DEFAULT_REGION_FIELDNAME, bucketRegion);
    return s3CredsMap;
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
