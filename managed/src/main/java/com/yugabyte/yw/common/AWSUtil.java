// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.PRECONDITION_FAILED;

import com.amazonaws.ClientConfiguration;
import com.amazonaws.SDKGlobalConfiguration;
import com.amazonaws.SdkClientException;
import com.amazonaws.auth.AWSCredentials;
import com.amazonaws.auth.AWSCredentialsProvider;
import com.amazonaws.auth.AWSSessionCredentials;
import com.amazonaws.auth.AWSStaticCredentialsProvider;
import com.amazonaws.auth.BasicAWSCredentials;
import com.amazonaws.client.builder.AwsClientBuilder.EndpointConfiguration;
import com.amazonaws.regions.DefaultAwsRegionProviderChain;
import com.amazonaws.services.s3.AmazonS3;
import com.amazonaws.services.s3.AmazonS3ClientBuilder;
import com.amazonaws.services.s3.model.AmazonS3Exception;
import com.amazonaws.services.s3.model.Bucket;
import com.amazonaws.services.s3.model.DeleteObjectsRequest;
import com.amazonaws.services.s3.model.DeleteObjectsRequest.KeyVersion;
import com.amazonaws.services.s3.model.GetBucketLocationRequest;
import com.amazonaws.services.s3.model.ListObjectsV2Request;
import com.amazonaws.services.s3.model.ListObjectsV2Result;
import com.amazonaws.services.s3.model.S3Object;
import com.amazonaws.services.s3.model.S3ObjectSummary;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.certmgmt.castore.CustomCAStoreManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data.ProxySetting;
import java.io.InputStream;
import java.security.KeyStore;
import java.security.SecureRandom;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.http.conn.ssl.NoopHostnameVerifier;
import org.apache.http.conn.ssl.SSLConnectionSocketFactory;
import org.yb.ybc.CloudStoreSpec;
import org.yb.ybc.CloudType;
import org.yb.ybc.S3ProxySetting;

@Singleton
@Slf4j
public class AWSUtil implements CloudUtil {
  @Inject IAMTemporaryCredentialsProvider iamCredsProvider;
  @Inject CustomCAStoreManager customCAStoreManager;
  @Inject RuntimeConfGetter runtimeConfGetter;

  public static final String AWS_ACCESS_KEY_ID_FIELDNAME = "AWS_ACCESS_KEY_ID";
  public static final String AWS_SECRET_ACCESS_KEY_FIELDNAME = "AWS_SECRET_ACCESS_KEY";
  private static final String AWS_REGION_SPECIFIC_HOST_BASE_FORMAT = "s3.%s.amazonaws.com";
  private static final String AWS_STANDARD_HOST_BASE_PATTERN = "s3([.](.+)|)[.]amazonaws[.]com";
  public static final String AWS_DEFAULT_REGION = "us-east-1";
  public static final String AWS_DEFAULT_ENDPOINT = "s3.amazonaws.com";

  public static final String YBC_AWS_ACCESS_KEY_ID_FIELDNAME = "AWS_ACCESS_KEY_ID";
  public static final String YBC_AWS_SECRET_ACCESS_KEY_FIELDNAME = "AWS_SECRET_ACCESS_KEY";
  public static final String YBC_AWS_ENDPOINT_FIELDNAME = "AWS_ENDPOINT";
  public static final String YBC_AWS_DEFAULT_REGION_FIELDNAME = "AWS_DEFAULT_REGION";
  public static final String YBC_AWS_ACCESS_TOKEN_FIELDNAME = "AWS_ACCESS_TOKEN";
  private static final Pattern standardHostBaseCompiled =
      Pattern.compile(AWS_STANDARD_HOST_BASE_PATTERN);

  // This method is a way to check if given S3 config can extract objects.
  @Override
  public boolean canCredentialListObjects(
      CustomerConfigData configData, Collection<String> locations) {
    if (CollectionUtils.isEmpty(locations)) {
      return true;
    }
    for (String location : locations) {
      try {
        maybeDisableCertVerification();
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
        String msg = String.format("Cannot list objects in backup location %s", location);
        log.error(msg, e);
        throw new PlatformServiceException(
            PRECONDITION_FAILED, msg + ": " + e.getLocalizedMessage());
      } finally {
        maybeEnableCertVerification();
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
      maybeDisableCertVerification();
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
    } finally {
      maybeEnableCertVerification();
    }
  }

  // For S3 location: s3://bucket/suffix
  // splitLocation[0] gives the bucket
  // splitLocation[1] gives the suffix string
  public static String[] getSplitLocationValue(String location) {
    location = location.substring(5);
    String[] split = location.split("/", 2);
    return split;
  }

  @Override
  public ConfigLocationInfo getConfigLocationInfo(String location) {
    String[] splitLocations = getSplitLocationValue(location);
    String bucket = splitLocations.length > 0 ? splitLocations[0] : "";
    String cloudPath = splitLocations.length > 1 ? splitLocations[1] : "";
    return new ConfigLocationInfo(bucket, cloudPath);
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

  public AmazonS3 createS3Client(CustomerConfigStorageS3Data s3Data)
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
    if (s3Data.isPathStyleAccess) {
      s3ClientBuilder.withPathStyleAccessEnabled(true);
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
    ClientConfiguration clientConfig = null;
    if (s3Data.proxySetting != null) {
      clientConfig = getClientConfiguration(s3Data.proxySetting);
    }
    try {
      Boolean caStoreEnabled = customCAStoreManager.isEnabled();
      Boolean caCertUploaded = customCAStoreManager.areCustomCAsPresent();
      // Adding enforce certification check as well so customers don't fail scheduled backups
      // immediately after upgrading to releases having CA store enabled. This gives time to the
      // customer to add CA certificates and make their configuration more secure.
      Boolean certVerificationEnforced =
          runtimeConfGetter.getGlobalConf(GlobalConfKeys.enforceCertVerificationBackupRestore);
      if (caStoreEnabled && caCertUploaded && certVerificationEnforced) {
        KeyStore ybaAndJavaKeyStore = customCAStoreManager.getYbaAndJavaKeyStore();
        try {
          TrustManagerFactory trustFactory =
              TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
          trustFactory.init(ybaAndJavaKeyStore);
          TrustManager[] ybaJavaTrustManagers = trustFactory.getTrustManagers();
          SecureRandom secureRandom = new SecureRandom();
          SSLContext sslContext = SSLContext.getInstance("TLS");
          sslContext.init(null, ybaJavaTrustManagers, secureRandom);
          SSLConnectionSocketFactory sslSocketFactory;
          if (StringUtils.isBlank(endpoint) || isHostBaseS3Standard(s3Data.awsHostBase)) {
            sslSocketFactory =
                new SSLConnectionSocketFactory(sslContext, new NoopHostnameVerifier());
          } else {
            sslSocketFactory = new SSLConnectionSocketFactory(sslContext);
          }
          if (clientConfig == null) {
            clientConfig = new ClientConfiguration();
          }
          clientConfig.getApacheHttpClientConfig().setSslSocketFactory(sslSocketFactory);
          return s3ClientBuilder.withClientConfiguration(clientConfig).build();
        } catch (Exception e) {
          log.error("Could not create S3 client", e);
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR,
              String.format("Could not create S3 client", e.getLocalizedMessage()));
        }
      } else {
        if (caStoreEnabled && !certVerificationEnforced) {
          log.warn("CA store feature is enabled but certificate verification is not enforced.");
        }
        return s3ClientBuilder.build();
      }
    } catch (SdkClientException e) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Failed to create S3 client, error: %s", e.getMessage()));
    }
  }

  private static ClientConfiguration getClientConfiguration(ProxySetting proxySetting) {
    ClientConfiguration cc = new ClientConfiguration();
    cc.withProxyHost(proxySetting.proxy);
    if (proxySetting.port > 0) {
      cc.withProxyPort(proxySetting.port);
    }
    if (StringUtils.isNotBlank(proxySetting.username)
        && StringUtils.isNotBlank(proxySetting.password)) {
      cc.withProxyUsername(proxySetting.username);
      cc.withProxyPassword(proxySetting.password);
    }
    return cc;
  }

  public String getBucketRegion(String bucketName, CustomerConfigStorageS3Data s3Data)
      throws SdkClientException {
    try {
      maybeDisableCertVerification();
      AmazonS3 client = createS3Client(s3Data);
      return getBucketRegion(bucketName, client);
    } finally {
      maybeEnableCertVerification();
    }
  }

  // For reusing already created client, as in listBuckets function
  // Cert-check is not disabled here, since the scope above this, where the S3 client is created,
  // should disable it.
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
    return standardHostBaseCompiled.matcher(hostBase).find();
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
        maybeDisableCertVerification();
        AmazonS3 s3Client = createS3Client((CustomerConfigStorageS3Data) configData);
        String[] splitLocation = getSplitLocationValue(backupLocation);
        String bucketName = splitLocation[0];
        String objectPrefix = splitLocation[1];
        String nextContinuationToken = null;
        do {
          ListObjectsV2Result listObjectsResult;
          ListObjectsV2Request request =
              new ListObjectsV2Request().withBucketName(bucketName).withPrefix(objectPrefix);
          if (StringUtils.isNotBlank(nextContinuationToken)) {
            request.withContinuationToken(nextContinuationToken);
          }
          listObjectsResult = s3Client.listObjectsV2(request);

          if (listObjectsResult.getKeyCount() == 0) {
            break;
          }
          nextContinuationToken = null;
          if (listObjectsResult.isTruncated()) {
            nextContinuationToken = listObjectsResult.getNextContinuationToken();
          }
          log.debug(
              "Retrieved blobs info for bucket " + bucketName + " with prefix " + objectPrefix);
          retrieveAndDeleteObjects(listObjectsResult, bucketName, s3Client);
        } while (nextContinuationToken != null);
      } catch (AmazonS3Exception e) {
        log.error(" Error in deleting objects at location " + backupLocation, e.getErrorMessage());
        throw e;
      } finally {
        maybeEnableCertVerification();
      }
    }
  }

  @Override
  public InputStream getCloudFileInputStream(CustomerConfigData configData, String cloudPath) {
    try {
      maybeDisableCertVerification();
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
    } finally {
      maybeEnableCertVerification();
    }
  }

  @Override
  public boolean checkFileExists(
      CustomerConfigData configData,
      Set<String> locations,
      String fileName,
      boolean checkExistsOnAll) {
    try {
      System.setProperty(SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "true");
      AmazonS3 s3Client = createS3Client((CustomerConfigStorageS3Data) configData);
      AtomicInteger count = new AtomicInteger(0);
      return locations.stream()
          .map(
              l -> {
                // For S3 location s3://bucket/suffix
                // The splitLocation[0] gets bucket
                // The splitLocation[1] gets any suffix string attached to it
                // We append the suffix with the file name to get exact path fo file
                String[] splitLocation = getSplitLocationValue(l);
                String bucketName = splitLocation[0];
                String objectSuffix =
                    splitLocation.length > 1
                        ? BackupUtil.getPathWithPrefixSuffixJoin(splitLocation[1], fileName)
                        : fileName;
                ListObjectsV2Result listResult = s3Client.listObjectsV2(bucketName, objectSuffix);
                if (listResult.getKeyCount() > 0) {
                  count.incrementAndGet();
                }
                return count;
              })
          .anyMatch(i -> checkExistsOnAll ? (i.get() == locations.size()) : (i.get() == 1));
    } catch (SdkClientException e) {
      throw new RuntimeException("Error checking files on locations", e);
    } finally {
      System.setProperty(SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "false");
    }
  }

  public void retrieveAndDeleteObjects(
      ListObjectsV2Result listObjectsResult, String bucketName, AmazonS3 s3Client)
      throws AmazonS3Exception {
    List<S3ObjectSummary> objectSummary = listObjectsResult.getObjectSummaries();
    List<DeleteObjectsRequest.KeyVersion> objectKeys =
        objectSummary.parallelStream()
            .map(o -> new KeyVersion(o.getKey()))
            .collect(Collectors.toList());
    DeleteObjectsRequest deleteRequest =
        new DeleteObjectsRequest(bucketName).withKeys(objectKeys).withQuiet(false);
    s3Client.deleteObjects(deleteRequest);
  }

  @Override
  public CloudStoreSpec createCloudStoreSpec(
      String region,
      String commonDir,
      String previousBackupLocation,
      CustomerConfigData configData) {
    CloudStoreSpec.Builder cloudStoreSpecBuilder =
        CloudStoreSpec.newBuilder().setType(CloudType.S3);
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    String storageLocation = getRegionLocationsMap(configData).get(region);
    String[] splitValues = getSplitLocationValue(storageLocation);
    String bucket = splitValues[0];
    String cloudDir =
        splitValues.length > 1
            ? BackupUtil.getPathWithPrefixSuffixJoin(splitValues[1], commonDir)
            : commonDir;
    cloudDir = BackupUtil.appendSlash(cloudDir);
    String previousCloudDir = "";
    if (StringUtils.isNotBlank(previousBackupLocation)) {
      splitValues = getSplitLocationValue(previousBackupLocation);
      previousCloudDir =
          splitValues.length > 1 ? BackupUtil.appendSlash(splitValues[1]) : previousCloudDir;
    }
    Map<String, String> s3CredsMap = createCredsMapYbc(s3Data, bucket);
    cloudStoreSpecBuilder
        .setBucket(bucket)
        .setPrevCloudDir(previousCloudDir)
        .setCloudDir(cloudDir)
        .putAllCreds(s3CredsMap);
    if (s3Data.proxySetting != null) {
      cloudStoreSpecBuilder.setProxySetting(addYbcProxySettings(s3Data.proxySetting));
    }
    return cloudStoreSpecBuilder.build();
  }

  @Override
  public CloudStoreSpec createRestoreCloudStoreSpec(
      String region, String cloudDir, CustomerConfigData configData, boolean isDsm) {
    CloudStoreSpec.Builder cloudStoreSpecBuilder =
        CloudStoreSpec.newBuilder().setType(CloudType.S3);
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    String storageLocation = getRegionLocationsMap(configData).get(region);
    String[] splitValues = getSplitLocationValue(storageLocation);
    String bucket = splitValues[0];
    Map<String, String> s3CredsMap = createCredsMapYbc(s3Data, bucket);
    cloudStoreSpecBuilder.setBucket(bucket).setPrevCloudDir("").putAllCreds(s3CredsMap);
    if (isDsm) {
      String location = BackupUtil.appendSlash(getSplitLocationValue(cloudDir)[1]);
      cloudStoreSpecBuilder.setCloudDir(location);
    } else {
      cloudStoreSpecBuilder.setCloudDir(cloudDir);
    }
    if (s3Data.proxySetting != null) {
      cloudStoreSpecBuilder.setProxySetting(addYbcProxySettings(s3Data.proxySetting));
    }
    return cloudStoreSpecBuilder.build();
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

  private S3ProxySetting addYbcProxySettings(
      CustomerConfigStorageS3Data.ProxySetting proxySettings) {
    S3ProxySetting.Builder proxyBuilder = S3ProxySetting.newBuilder();
    proxyBuilder.setProxyHost(proxySettings.proxy);
    if (proxySettings.port > 0) {
      proxyBuilder.setProxyPort(proxySettings.port);
    }
    if (StringUtils.isNotBlank(proxySettings.username)
        && StringUtils.isNotBlank(proxySettings.password)) {
      proxyBuilder.setProxyPassword(proxySettings.password);
      proxyBuilder.setProxyUsername(proxySettings.username);
    }
    return proxyBuilder.build();
  }

  @Override
  public Map<String, String> listBuckets(CustomerConfigData configData) {
    Map<String, String> bucketHostBaseMap = new HashMap<>();
    try {
      maybeDisableCertVerification();
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
      buckets.parallelStream()
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
    } finally {
      maybeEnableCertVerification();
    }
    return bucketHostBaseMap;
  }

  public Map<String, String> getRegionLocationsMap(CustomerConfigData configData) {
    Map<String, String> regionLocationsMap = new HashMap<>();
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    if (CollectionUtils.isNotEmpty(s3Data.regionLocations)) {
      s3Data.regionLocations.stream().forEach(rL -> regionLocationsMap.put(rL.region, rL.location));
    }
    regionLocationsMap.put(YbcBackupUtil.DEFAULT_REGION_STRING, s3Data.backupLocation);
    return regionLocationsMap;
  }

  public void maybeDisableCertVerification() {
    // Disable cert checking while connecting with s3
    // Enabling it can potentially fail when s3 compatible storages like
    // Dell ECS are provided and custom certs are needed to connect
    // Reference: https://yugabyte.atlassian.net/browse/PLAT-2497
    if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.enforceCertVerificationBackupRestore)) {
      System.setProperty(SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "true");
    }
  }

  public void maybeEnableCertVerification() {
    if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.enforceCertVerificationBackupRestore)) {
      System.setProperty(SDKGlobalConfiguration.DISABLE_CERT_CHECKING_SYSTEM_PROPERTY, "false");
    }
  }
}
