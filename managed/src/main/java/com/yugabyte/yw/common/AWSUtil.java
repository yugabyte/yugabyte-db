// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.apache.commons.lang3.exception.ExceptionUtils.getStackTrace;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.EXPECTATION_FAILED;
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
import com.amazonaws.services.cloudtrail.AWSCloudTrail;
import com.amazonaws.services.cloudtrail.model.Event;
import com.amazonaws.services.cloudtrail.model.LookupAttribute;
import com.amazonaws.services.cloudtrail.model.LookupAttributeKey;
import com.amazonaws.services.cloudtrail.model.LookupEventsRequest;
import com.amazonaws.services.cloudtrail.model.LookupEventsResult;
import com.amazonaws.services.ec2.AmazonEC2;
import com.amazonaws.services.ec2.AmazonEC2ClientBuilder;
import com.amazonaws.services.ec2.model.DescribeInstancesRequest;
import com.amazonaws.services.ec2.model.DescribeInstancesResult;
import com.amazonaws.services.ec2.model.DescribeSpotPriceHistoryRequest;
import com.amazonaws.services.ec2.model.DescribeSpotPriceHistoryResult;
import com.amazonaws.services.ec2.model.Filter;
import com.amazonaws.services.ec2.model.Instance;
import com.amazonaws.services.ec2.model.SpotPrice;
import com.amazonaws.services.s3.AmazonS3;
import com.amazonaws.services.s3.AmazonS3ClientBuilder;
import com.amazonaws.services.s3.model.AmazonS3Exception;
import com.amazonaws.services.s3.model.Bucket;
import com.amazonaws.services.s3.model.DeleteObjectsRequest;
import com.amazonaws.services.s3.model.DeleteObjectsRequest.KeyVersion;
import com.amazonaws.services.s3.model.GetBucketLocationRequest;
import com.amazonaws.services.s3.model.GetObjectRequest;
import com.amazonaws.services.s3.model.ListObjectsV2Request;
import com.amazonaws.services.s3.model.ListObjectsV2Result;
import com.amazonaws.services.s3.model.PutObjectRequest;
import com.amazonaws.services.s3.model.S3Object;
import com.amazonaws.services.s3.model.S3ObjectSummary;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.cloud.aws.AWSCloudImpl;
import com.yugabyte.yw.common.UniverseInterruptionResult.InterruptionStatus;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse.ResponseCloudStoreSpec;
import com.yugabyte.yw.common.certmgmt.castore.CustomCAStoreManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data.RegionLocations;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.KeyStore;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.annotation.Nullable;
import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.http.conn.ssl.NoopHostnameVerifier;
import org.apache.http.conn.ssl.SSLConnectionSocketFactory;
import org.yb.ybc.CloudStoreSpec;
import org.yb.ybc.CloudType;
import org.yb.ybc.ProxySpec;

@Singleton
@Slf4j
public class AWSUtil implements CloudUtil {
  @Inject CustomCAStoreManager customCAStoreManager;
  @Inject RuntimeConfGetter runtimeConfGetter;
  @Inject AWSCloudImpl awsCloudImpl;

  public static final String AWS_S3_LOCATION_PREFIX = "s3://";
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
  public static final String YBC_USE_AWS_IAM_FIELDNAME = "USE_AWS_IAM";
  private static final Pattern standardHostBaseCompiled =
      Pattern.compile(AWS_STANDARD_HOST_BASE_PATTERN);

  private void tryListObjects(AmazonS3 s3Client, String bucketName, String prefix)
      throws SdkClientException {
    Boolean doesBucketExist = s3Client.doesBucketExistV2(bucketName);
    if (!doesBucketExist) {
      throw new RuntimeException(String.format("No S3 bucket found with name %s", bucketName));
    }
    ListObjectsV2Result result;
    if (StringUtils.isBlank(prefix)) {
      result = s3Client.listObjectsV2(bucketName);
    } else {
      result = s3Client.listObjectsV2(bucketName, prefix);
    }
    if (result.getKeyCount() == 0) {
      log.debug("No objects exists within bucket {}", bucketName);
    }
  }

  // This method is a way to check if given S3 config can extract objects.
  @Override
  public boolean canCredentialListObjects(
      CustomerConfigData configData, Map<String, String> regionLocationsMap) {
    if (MapUtils.isEmpty(regionLocationsMap)) {
      return true;
    }
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    try {
      maybeDisableCertVerification();
      for (Map.Entry<String, String> entry : regionLocationsMap.entrySet()) {
        String region = entry.getKey();
        String backupLocation = entry.getValue();
        AmazonS3 s3Client = createS3Client(s3Data, region);
        try {
          CloudLocationInfo cLInfo = getCloudLocationInfo(region, configData, backupLocation);
          String bucketName = cLInfo.bucket;
          String prefix = cLInfo.cloudPath;
          tryListObjects(s3Client, bucketName, prefix);
        } catch (SdkClientException e) {
          String msg = String.format("Cannot list objects in backup location %s", backupLocation);
          log.error(msg, e);
          return false;
        }
      }
      return true;
    } catch (SdkClientException e) {
      log.error("Failed to create S3 client", e.getMessage());
      return false;
    } finally {
      maybeEnableCertVerification();
    }
  }

  @Override
  public void checkListObjectsWithYbcSuccessMarkerCloudStore(
      CustomerConfigData configData, YbcBackupResponse.ResponseCloudStoreSpec csSpec) {
    Map<String, ResponseCloudStoreSpec.BucketLocation> regionPrefixesMap =
        csSpec.getBucketLocationsMap();
    Map<String, String> configRegions = getRegionLocationsMap(configData);
    try {
      maybeDisableCertVerification();
      for (Map.Entry<String, ResponseCloudStoreSpec.BucketLocation> regionPrefix :
          regionPrefixesMap.entrySet()) {
        String region = regionPrefix.getKey();
        if (configRegions.containsKey(region)) {
          AmazonS3 s3Client = createS3Client((CustomerConfigStorageS3Data) configData, region);
          // Use "cloudDir" of success marker as object prefix
          String prefix = regionPrefix.getValue().cloudDir;
          // Use config's bucket for bucket name
          String bucketName = getCloudLocationInfo(regionPrefix.getKey(), configData, null).bucket;
          log.debug("Trying object listing with S3 bucket {} and prefix {}", bucketName, prefix);
          try {
            tryListObjects(s3Client, bucketName, prefix);
          } catch (SdkClientException e) {
            String msg =
                String.format(
                    "Cannot list objects in cloud location with bucket %s and cloud directory %s",
                    bucketName, prefix);
            log.error(msg, e);
            throw new PlatformServiceException(
                PRECONDITION_FAILED, msg + ": " + e.getLocalizedMessage());
          }
        }
      }
    } catch (SdkClientException e) {
      throw new PlatformServiceException(
          PRECONDITION_FAILED, "Failed to create S3 client: " + e.getLocalizedMessage());
    } finally {
      maybeEnableCertVerification();
    }
  }

  @Override
  public boolean deleteKeyIfExists(CustomerConfigData configData, String defaultBackupLocation) {
    CloudLocationInfo cLInfo =
        getCloudLocationInfo(
            YbcBackupUtil.DEFAULT_REGION_STRING, configData, defaultBackupLocation);
    String bucketName = cLInfo.bucket;
    String objectPrefix = cLInfo.cloudPath;
    String keyLocation =
        objectPrefix.substring(0, objectPrefix.lastIndexOf('/')) + KEY_LOCATION_SUFFIX;
    try {
      maybeDisableCertVerification();
      AmazonS3 s3Client = createS3Client((CustomerConfigStorageS3Data) configData);
      ListObjectsV2Result listObjectsResult = s3Client.listObjectsV2(bucketName, keyLocation);
      if (listObjectsResult.getKeyCount() == 0) {
        log.info("Specified Location " + keyLocation + " does not contain objects");
      } else {
        log.debug("Retrieved blobs info for bucket " + bucketName + " with prefix " + keyLocation);
        retrieveAndDeleteObjects(listObjectsResult, bucketName, s3Client);
      }
    } catch (Exception e) {
      log.error("Error while deleting key object at location: {}", keyLocation, e);
      return false;
    } finally {
      maybeEnableCertVerification();
    }
    return true;
  }

  // For S3 location: s3://bucket/suffix
  // splitLocation[0] gives the bucket
  // splitLocation[1] gives the suffix string
  public static String[] getSplitLocationValue(String location) {
    if (StringUtils.isBlank(location)) {
      return new String[] {""};
    }
    location = location.substring(AWS_S3_LOCATION_PREFIX.length());
    String[] split = location.split("/", 2);
    return split;
  }

  @Override
  public void checkConfigTypeAndBackupLocationSame(String backupLocation) {
    if (!backupLocation.startsWith(AWS_S3_LOCATION_PREFIX)) {
      throw new PlatformServiceException(PRECONDITION_FAILED, "Not an S3 location");
    }
  }

  @Override
  public boolean uploadYbaBackup(CustomerConfigData configData, File backup, String backupDir) {
    try {
      maybeDisableCertVerification();
      CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
      AmazonS3 client = createS3Client(s3Data);
      CloudLocationInfo cLInfo =
          getCloudLocationInfo(YbcBackupUtil.DEFAULT_REGION_STRING, s3Data, "");
      // Construct full path to upload like (s3://bucket/)cloudPath/backupDir/backupName.tar.gz
      String keyName =
          Stream.of(cLInfo.cloudPath, backupDir, backup.getName())
              .filter(s -> !s.isEmpty())
              .collect(Collectors.joining("/"));

      PutObjectRequest request = new PutObjectRequest(cLInfo.bucket, keyName, backup);
      client.putObject(request);
    } catch (Exception e) {
      log.error("Error uploading backup: {}", e);
      return false;
    } finally {
      maybeEnableCertVerification();
    }
    return true;
  }

  @Override
  public boolean uploadYBDBRelease(
      CustomerConfigData configData, File release, String backupDir, String version) {
    try {
      maybeDisableCertVerification();
      CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
      AmazonS3 client = createS3Client(s3Data);
      CloudLocationInfo cLInfo =
          getCloudLocationInfo(YbcBackupUtil.DEFAULT_REGION_STRING, s3Data, "");
      String keyName =
          Stream.of(cLInfo.cloudPath, backupDir, YBDB_RELEASES, version, release.getName())
              .filter(s -> !s.isEmpty())
              .collect(Collectors.joining("/"));
      long startTime = System.nanoTime();
      PutObjectRequest request = new PutObjectRequest(cLInfo.bucket, keyName, release);
      client.putObject(request);
      long endTime = System.nanoTime();
      // Calculate duration in seconds
      double durationInSeconds = (endTime - startTime) / 1_000_000_000.0;
      log.info(
          "Upload of {} to S3 path {} completed in {} seconds",
          release.getName(),
          keyName,
          durationInSeconds);
    } catch (Exception e) {
      log.error("Error uploading YBDB release {}: {}", release.getName(), e);
      return false;
    } finally {
      maybeEnableCertVerification();
    }
    return true;
  }

  @Override
  public Set<String> getRemoteReleaseVersions(CustomerConfigData configData, String backupDir) {
    try {
      maybeDisableCertVerification();
      CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
      AmazonS3 client = createS3Client(s3Data);
      CloudLocationInfo cLInfo =
          getCloudLocationInfo(YbcBackupUtil.DEFAULT_REGION_STRING, s3Data, "");

      // Get all the backups in the specified bucket/directory
      Set<String> releaseVersions = new HashSet<>();
      String nextContinuationToken = null;
      do {
        ListObjectsV2Result listObjectsResult;
        ListObjectsV2Request request =
            new ListObjectsV2Request()
                .withBucketName(cLInfo.bucket)
                .withPrefix(backupDir + "/" + YBDB_RELEASES);
        if (StringUtils.isNotBlank(nextContinuationToken)) {
          request.withContinuationToken(nextContinuationToken);
        }
        listObjectsResult = client.listObjectsV2(request);

        if (listObjectsResult.getKeyCount() == 0) {
          break;
        }
        nextContinuationToken = null;
        if (listObjectsResult.isTruncated()) {
          nextContinuationToken = listObjectsResult.getNextContinuationToken();
        }
        // Parse out release version from result
        List<S3ObjectSummary> releases = listObjectsResult.getObjectSummaries();
        for (S3ObjectSummary release : releases) {
          String version = extractReleaseVersion(release.getKey(), backupDir);
          if (version != null) {
            log.info("Found version {} in S3 bucket", version);
            releaseVersions.add(version);
          }
        }
      } while (nextContinuationToken != null);

      return releaseVersions;
    } catch (AmazonS3Exception e) {
      log.error("AWS Error occurred while listing releases in S3: {}", e.getErrorMessage());
    } catch (Exception e) {
      log.error("Unexpected exception while listing releases in S3: {}", e.getMessage());
    } finally {
      maybeEnableCertVerification();
    }
    return new HashSet<>();
  }

  // Best effort to download YBDB releases matching releaseVersions specified. Downloads both x86
  // and aarch64 releases. false only if Exception, true even if release version not found
  @Override
  public boolean downloadRemoteReleases(
      CustomerConfigData configData,
      Set<String> releaseVersions,
      String releasesPath,
      String backupDir) {

    for (String version : releaseVersions) {
      // Create the local directory if necessary inside of yb.storage.path/releases
      Path versionPath;
      try {
        versionPath = Files.createDirectories(Path.of(releasesPath, version));
      } catch (Exception e) {
        log.error(
            "Error creating local releases directory for version {}: {}", version, e.getMessage());
        return false;
      }

      // Find the exact S3 file paths (x86 and aarch64) matching the version
      try {
        maybeDisableCertVerification();
        CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
        AmazonS3 client = createS3Client(s3Data);
        CloudLocationInfo cLInfo =
            getCloudLocationInfo(YbcBackupUtil.DEFAULT_REGION_STRING, s3Data, "");

        // Get all the backups in the specified bucket/directory
        String nextContinuationToken = null;

        do {
          ListObjectsV2Result listObjectsResult;
          // List objects with prefix matching version number
          ListObjectsV2Request request =
              new ListObjectsV2Request()
                  .withBucketName(cLInfo.bucket)
                  .withPrefix(backupDir + "/" + YBDB_RELEASES + "/" + version);

          if (StringUtils.isNotBlank(nextContinuationToken)) {
            request.withContinuationToken(nextContinuationToken);
          }

          listObjectsResult = client.listObjectsV2(request);

          if (listObjectsResult.getKeyCount() == 0) {
            // No releases found for a specified version (best effort, might work later)
            break;
          }

          nextContinuationToken =
              listObjectsResult.isTruncated() ? listObjectsResult.getNextContinuationToken() : null;

          List<S3ObjectSummary> releases = listObjectsResult.getObjectSummaries();
          // Download found releases individually (same version, different arch)
          for (S3ObjectSummary release : releases) {

            // Name the local file same as S3 key basename (yugabyte-version-arch.tar.gz)
            File localRelease =
                versionPath
                    .resolve(release.getKey().substring(release.getKey().lastIndexOf('/') + 1))
                    .toFile();

            log.info("Attempting to download from S3 {} to {}", release.getKey(), localRelease);

            GetObjectRequest getRequest = new GetObjectRequest(cLInfo.bucket, release.getKey());
            try (S3Object releaseObject = client.getObject(getRequest);
                InputStream inputStream = releaseObject.getObjectContent();
                FileOutputStream outputStream = new FileOutputStream(localRelease)) {
              byte[] buffer = new byte[1024];
              int bytesRead;
              while ((bytesRead = inputStream.read(buffer)) != -1) {
                outputStream.write(buffer, 0, bytesRead);
              }

            } catch (Exception e) {
              log.error(
                  "Error downloading {} to {}: {}", release.getKey(), localRelease, e.getMessage());
              return false;
            }
          }
        } while (nextContinuationToken != null);

      } catch (AmazonS3Exception e) {
        log.error("AWS Error occurred while downloading releases from S3: {}", e.getErrorMessage());
        return false;
      } catch (Exception e) {
        log.error("Unexpected exception while downloading releases from S3: {}", e.getMessage());
        return false;
      } finally {
        maybeEnableCertVerification();
      }
    }
    return true;
  }

  @Override
  public File downloadYbaBackup(CustomerConfigData configData, String backupDir, Path localDir) {
    try {
      maybeDisableCertVerification();
      CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
      AmazonS3 client = createS3Client(s3Data);
      CloudLocationInfo cLInfo =
          getCloudLocationInfo(YbcBackupUtil.DEFAULT_REGION_STRING, s3Data, "");
      log.info("Downloading most recent backup in s3://{}/{}", cLInfo.bucket, backupDir);

      // Get all the backups in the specified bucket/directory
      List<S3ObjectSummary> allBackups = new ArrayList<>();
      String nextContinuationToken = null;
      do {
        ListObjectsV2Result listObjectsResult;
        ListObjectsV2Request request =
            new ListObjectsV2Request().withBucketName(cLInfo.bucket).withPrefix(backupDir);
        if (StringUtils.isNotBlank(nextContinuationToken)) {
          request.withContinuationToken(nextContinuationToken);
        }
        listObjectsResult = client.listObjectsV2(request);

        if (listObjectsResult.getKeyCount() == 0) {
          break;
        }
        nextContinuationToken = null;
        if (listObjectsResult.isTruncated()) {
          nextContinuationToken = listObjectsResult.getNextContinuationToken();
        }
        allBackups.addAll(listObjectsResult.getObjectSummaries());
      } while (nextContinuationToken != null);

      // Sort backups by last modified date (most recent first)
      List<S3ObjectSummary> sortedBackups =
          allBackups.stream()
              .sorted((o1, o2) -> o2.getLastModified().compareTo(o1.getLastModified()))
              .collect(Collectors.toList());
      if (sortedBackups.isEmpty()) {
        log.error("Could not find any backups to restore");
        return null;
      }
      // Iterate through until we find a backup
      S3ObjectSummary backup = null;
      Matcher match = null;
      Pattern backupPattern = Pattern.compile("backup_.*\\.tgz");
      for (S3ObjectSummary bkp : sortedBackups) {
        match = backupPattern.matcher(bkp.getKey());
        if (match.find()) {
          log.info("Downloading backup s3:{}/{}", bkp.getBucketName(), bkp.getKey());
          backup = bkp;
          break;
        }
      }

      if (backup == null) {
        log.error("Could not find matching backup, aborting restore.");
        return null;
      }

      // Construct full local filepath with same name as remote backup
      File localFile = localDir.resolve(match.group()).toFile();
      // Create directory at platformReplication/<storageConfigUUID> if necessary
      Files.createDirectories(localFile.getParentFile().toPath());
      GetObjectRequest getRequest = new GetObjectRequest(cLInfo.bucket, backup.getKey());
      client.getObject(getRequest);
      try (S3Object s3Object = client.getObject(getRequest);
          InputStream inputStream = s3Object.getObjectContent();
          FileOutputStream outputStream = new FileOutputStream(localFile)) {

        // Write the object content to the local file
        byte[] buffer = new byte[1024];
        int bytesRead;
        while ((bytesRead = inputStream.read(buffer)) != -1) {
          outputStream.write(buffer, 0, bytesRead);
        }

      } catch (Exception e) {
        log.error("Error writing S3 object to file: {}", e.getMessage());
        return null;
      }
      if (!localFile.exists() || localFile.length() == 0) {
        log.error("Local file does not exist or is empty, aborting restore.");
        return null;
      }
      log.info("Downloaded file from S3 to {}", localFile.getCanonicalPath());
      return localFile;
    } catch (AmazonS3Exception e) {
      log.error("Error occurred while getting object in S3: {}", e.getErrorMessage());
    } catch (Exception e) {
      log.error("Unexpected exception while getting object in S3: {}", e.getMessage());
    } finally {
      maybeEnableCertVerification();
    }
    return null;
  }

  @Override
  public boolean cleanupUploadedBackups(CustomerConfigData configData, String backupDir) {
    try {
      maybeDisableCertVerification();
      CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
      AmazonS3 client = createS3Client(s3Data);
      CloudLocationInfo cLInfo =
          getCloudLocationInfo(YbcBackupUtil.DEFAULT_REGION_STRING, s3Data, "");
      log.info("Cleaning up uploaded backups in S3 location s3://{}/{}", cLInfo.bucket, backupDir);

      // Get all the backups in the specified bucket/directory
      List<S3ObjectSummary> allBackups = new ArrayList<>();
      String nextContinuationToken = null;
      do {
        ListObjectsV2Result listObjectsResult;
        ListObjectsV2Request request =
            new ListObjectsV2Request().withBucketName(cLInfo.bucket).withPrefix(backupDir);
        if (StringUtils.isNotBlank(nextContinuationToken)) {
          request.withContinuationToken(nextContinuationToken);
        }
        listObjectsResult = client.listObjectsV2(request);

        if (listObjectsResult.getKeyCount() == 0) {
          break;
        }
        nextContinuationToken = null;
        if (listObjectsResult.isTruncated()) {
          nextContinuationToken = listObjectsResult.getNextContinuationToken();
        }
        allBackups.addAll(listObjectsResult.getObjectSummaries());
      } while (nextContinuationToken != null);

      Pattern backupPattern = Pattern.compile("backup_.*\\.tgz");
      // Sort backups by last modified date (most recent first)
      List<S3ObjectSummary> sortedBackups =
          allBackups.stream()
              .filter(
                  o ->
                      !o.getKey().contains(YBDB_RELEASES)
                          && backupPattern.matcher(o.getKey()).find())
              .sorted((o1, o2) -> o2.getLastModified().compareTo(o1.getLastModified()))
              .collect(Collectors.toList());

      // Only keep the n most recent backups
      int numKeepBackups =
          runtimeConfGetter.getGlobalConf(GlobalConfKeys.numCloudYbaBackupsRetention);
      if (sortedBackups.size() <= numKeepBackups) {
        log.info(
            "No backups to delete, only {} backups in s3://{}/{} less than limit {}",
            sortedBackups.size(),
            cLInfo.bucket,
            backupDir,
            numKeepBackups);
        return true;
      }
      List<S3ObjectSummary> backupsToDelete =
          sortedBackups.subList(numKeepBackups, sortedBackups.size());
      // Prepare the delete request
      DeleteObjectsRequest deleteRequest =
          new DeleteObjectsRequest(cLInfo.bucket)
              .withKeys(
                  backupsToDelete.stream()
                      .map(o -> new KeyVersion(o.getKey()))
                      .collect(Collectors.toList()));

      // Delete the old backups
      client.deleteObjects(deleteRequest);
      log.info(
          "Deleted {} old backups from s3://{}/{}",
          backupsToDelete.size(),
          cLInfo.bucket,
          backupDir);
    } catch (AmazonS3Exception e) {
      log.warn("Error occured while deleted objects in S3: {}", e.getErrorMessage());
      return false;
    } catch (Exception e) {
      log.warn(
          "Unexpected exception while attempting to cleanup S3 YBA backup: {}", e.getMessage());
      return false;
    } finally {
      maybeEnableCertVerification();
    }
    return true;
  }

  @Override
  public CloudLocationInfo getCloudLocationInfo(
      String region, CustomerConfigData configData, @Nullable String backupLocation) {
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    Map<String, String> configRegionLocationsMap = getRegionLocationsMap(configData);
    String configLocation = configRegionLocationsMap.getOrDefault(region, s3Data.backupLocation);
    String[] backupSplitLocations =
        getSplitLocationValue(
            StringUtils.isBlank(backupLocation) ? configLocation : backupLocation);
    String[] configSplitLocations = getSplitLocationValue(configLocation);
    String bucket = configSplitLocations.length > 0 ? configSplitLocations[0] : "";
    String cloudPath = backupSplitLocations.length > 1 ? backupSplitLocations[1] : "";
    return new CloudLocationInfo(bucket, cloudPath);
  }

  public static String getClientRegion(String fallbackRegion) {
    String region = fallbackRegion;
    if (StringUtils.isBlank(region)) {
      try {
        region = new DefaultAwsRegionProviderChain().getRegion();
      } catch (SdkClientException e) {
        log.trace("No region found in Default region chain.");
      }
    }
    return StringUtils.isBlank(region) ? "us-east-1" : region;
  }

  public AmazonS3 createS3Client(CustomerConfigStorageS3Data s3Data)
      throws SdkClientException, PlatformServiceException {
    return createS3Client(s3Data, YbcBackupUtil.DEFAULT_REGION_STRING);
  }

  /**
   * Create S3 client
   *
   * @param s3Data Customer config data
   * @param region Config region to use
   * @return The S3 client
   * @throws SdkClientException
   * @throws PlatformServiceException
   */
  public AmazonS3 createS3Client(CustomerConfigStorageS3Data s3Data, String region)
      throws SdkClientException, PlatformServiceException {
    AmazonS3 client = null;
    AmazonS3ClientBuilder s3ClientBuilder = AmazonS3ClientBuilder.standard();
    if (s3Data.isIAMInstanceProfile) {
      // Using credential chaining here.
      // This first looks for K8s service account IAM role,
      // then IAM user,
      // then falls back to the Node/EC2 IAM role.
      try {
        s3ClientBuilder.withCredentials(
            new AWSStaticCredentialsProvider(
                (new IAMTemporaryCredentialsProvider(runtimeConfGetter))
                    .getTemporaryCredentials(s3Data)));
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
    //  Use region specific hostbase
    String endpoint = getRegionHostBaseMap(s3Data).get(region);
    String clientRegion = getClientRegion(s3Data.fallbackRegion);
    if (StringUtils.isBlank(endpoint) || isHostBaseS3Standard(endpoint)) {
      // Use global bucket access only for standard S3.
      s3ClientBuilder.withForceGlobalBucketAccessEnabled(true);
    }
    if (StringUtils.isNotBlank(endpoint)) {
      // Need to set region because region-chaining may
      // fail if correct environment variables not found.
      s3ClientBuilder.withEndpointConfiguration(new EndpointConfiguration(endpoint, clientRegion));
    } else {
      s3ClientBuilder.withRegion(clientRegion);
    }
    ClientConfiguration clientConfig = null;
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
          client = s3ClientBuilder.withClientConfiguration(clientConfig).build();
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
        client = s3ClientBuilder.build();
      }
      // First bucket region fetch may fail, so call and skip.
      try {
        if (client != null) {
          String bucket = getCloudLocationInfo(region, s3Data, null /* backupLocation */).bucket;
          if (StringUtils.isNotBlank(bucket)) {
            getBucketRegion(bucket, client);
          }
        } else {
          throw new RuntimeException("S3 client is null. This is unexpected!");
        }
      } catch (SdkClientException e) {
        log.trace("Skipping first bucket region fetch error: {}", e.getMessage());
      }
      return client;
    } catch (SdkClientException e) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Failed to create S3 client, error: %s", e.getMessage()));
    }
  }

  public static Map<String, String> getRegionHostBaseMap(CustomerConfigData configData) {
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    Map<String, String> regionHostBaseMap = new HashMap<>();
    regionHostBaseMap.put(YbcBackupUtil.DEFAULT_REGION_STRING, s3Data.awsHostBase);
    if (CollectionUtils.isNotEmpty(s3Data.regionLocations)) {
      s3Data.regionLocations.stream()
          // Populate default region's host base if empty.
          .forEach(
              rL ->
                  regionHostBaseMap.put(
                      rL.region,
                      StringUtils.isBlank(rL.awsHostBase) ? s3Data.awsHostBase : rL.awsHostBase));
    }
    return regionHostBaseMap;
  }

  public String getBucketRegion(
      String bucketName, CustomerConfigStorageS3Data s3Data, String region)
      throws SdkClientException {
    try {
      maybeDisableCertVerification();
      AmazonS3 client = createS3Client(s3Data, region);
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
      CustomerConfigStorageS3Data s3Data, String bucketName, String bucketRegion, String region) {
    Map<String, String> hostBaseMap = getRegionHostBaseMap(s3Data);
    String hostBase = hostBaseMap.get(region);
    if (StringUtils.isEmpty(hostBase) || hostBase.equals(AWS_DEFAULT_ENDPOINT)) {
      hostBase = createBucketRegionSpecificHostBase(bucketName, bucketRegion);
    }
    return hostBase;
  }

  @Override
  public boolean deleteStorage(
      CustomerConfigData configData, Map<String, List<String>> backupRegionLocationsMap) {
    for (Map.Entry<String, List<String>> backupRegionLocations :
        backupRegionLocationsMap.entrySet()) {
      try {
        maybeDisableCertVerification();
        String region = backupRegionLocations.getKey();
        AmazonS3 s3Client = createS3Client((CustomerConfigStorageS3Data) configData, region);
        for (String backupLocation : backupRegionLocations.getValue()) {
          CloudLocationInfo cLInfo = getCloudLocationInfo(region, configData, backupLocation);
          String bucketName = cLInfo.bucket;
          String objectPrefix = cLInfo.cloudPath;
          String nextContinuationToken = null;
          try {
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
            log.error(
                "Error occured while deleting objects at location {}. Error {}",
                backupLocation,
                e.getMessage());
            throw e;
          }
        }
      } catch (AmazonS3Exception e) {
        log.error(" Error occured while deleting objects in S3: {}", e.getErrorMessage());
        return false;
      } finally {
        maybeEnableCertVerification();
      }
    }
    return true;
  }

  @Override
  // This method is in use by ReleaseManager code, which does not contain config location in
  // CustomerConfigData object. Such case would not be allowed for UI generated customer config.
  public InputStream getCloudFileInputStream(CustomerConfigData configData, String cloudPath) {
    try {
      maybeDisableCertVerification();
      CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
      AmazonS3 s3Client = createS3Client(s3Data);
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
      maybeDisableCertVerification();
      AmazonS3 s3Client = createS3Client((CustomerConfigStorageS3Data) configData);
      AtomicInteger count = new AtomicInteger(0);
      return locations.stream()
          .map(
              l -> {
                // For S3 location s3://bucket/suffix
                // The configLocationInfo.bucket gets bucket from Storage config
                // The backupLocationInfo.cloudPath gets any suffix string attached to backup
                // location
                // We append the suffix with the file name to get exact path fo file
                CloudLocationInfo cLInfo =
                    getCloudLocationInfo(YbcBackupUtil.DEFAULT_REGION_STRING, configData, l);
                String bucketName = cLInfo.bucket;
                String objectSuffix =
                    StringUtils.isNotBlank(cLInfo.cloudPath)
                        ? BackupUtil.getPathWithPrefixSuffixJoin(cLInfo.cloudPath, fileName)
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
      maybeEnableCertVerification();
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
      CustomerConfigData configData,
      Universe universe) {
    CloudStoreSpec.Builder cloudStoreSpecBuilder =
        CloudStoreSpec.newBuilder().setType(CloudType.S3);
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    CloudLocationInfo csInfo = getCloudLocationInfo(region, configData, "");
    String bucket = csInfo.bucket;
    String cloudDir =
        StringUtils.isNotBlank(csInfo.cloudPath)
            ? BackupUtil.getPathWithPrefixSuffixJoin(csInfo.cloudPath, commonDir)
            : commonDir;
    cloudDir = StringUtils.isNotBlank(cloudDir) ? BackupUtil.appendSlash(cloudDir) : "";
    String previousCloudDir = "";
    if (StringUtils.isNotBlank(previousBackupLocation)) {
      csInfo = getCloudLocationInfo(region, configData, previousBackupLocation);
      previousCloudDir =
          StringUtils.isNotBlank(csInfo.cloudPath)
              ? BackupUtil.appendSlash(csInfo.cloudPath)
              : previousCloudDir;
    }
    Map<String, String> s3CredsMap = createCredsMapYbc(s3Data, bucket, region, universe);
    cloudStoreSpecBuilder
        .setBucket(bucket)
        .setPrevCloudDir(previousCloudDir)
        .setCloudDir(cloudDir)
        .putAllCreds(s3CredsMap);
    return cloudStoreSpecBuilder.build();
  }

  // In case of Restore - cloudDir is picked from success marker
  // In case of Success marker download - cloud Dir is the location provided by user in API
  @Override
  public CloudStoreSpec createRestoreCloudStoreSpec(
      String region,
      String cloudDir,
      CustomerConfigData configData,
      boolean isDsm,
      Universe universe) {
    CloudStoreSpec.Builder cloudStoreSpecBuilder =
        CloudStoreSpec.newBuilder().setType(CloudType.S3);
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    CloudLocationInfo csInfo = getCloudLocationInfo(region, configData, "");
    String bucket = csInfo.bucket;
    Map<String, String> s3CredsMap = createCredsMapYbc(s3Data, bucket, region, universe);
    cloudStoreSpecBuilder.setBucket(bucket).setPrevCloudDir("").putAllCreds(s3CredsMap);
    if (isDsm) {
      String location = getCloudLocationInfo(region, configData, cloudDir).cloudPath;
      cloudStoreSpecBuilder.setCloudDir(BackupUtil.appendSlash(location));
    } else {
      cloudStoreSpecBuilder.setCloudDir(cloudDir);
    }
    return cloudStoreSpecBuilder.build();
  }

  private Map<String, String> createCredsMapYbc(
      CustomerConfigData configData, String bucket, String region, Universe universe) {
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    Map<String, String> s3CredsMap = new HashMap<>();
    if (s3Data.isIAMInstanceProfile) {
      boolean useDbIAM =
          runtimeConfGetter.getConfForScope(universe, UniverseConfKeys.useDBNodesIAMRoleForBackup);
      if (useDbIAM) {
        s3CredsMap.put(YBC_USE_AWS_IAM_FIELDNAME, "true");
      } else {
        fillMapWithIAMCreds(s3CredsMap, s3Data);
      }
    } else {
      s3CredsMap.put(YBC_AWS_ACCESS_KEY_ID_FIELDNAME, s3Data.awsAccessKeyId);
      s3CredsMap.put(YBC_AWS_SECRET_ACCESS_KEY_FIELDNAME, s3Data.awsSecretAccessKey);
    }
    String bucketRegion = null;
    try {
      bucketRegion = getBucketRegion(bucket, s3Data, region);
    } catch (SdkClientException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format(
              "Failed to retrieve region of Bucket %s, error: %s", bucket, e.getMessage()));
    }
    String hostBase = getOrCreateHostBase(s3Data, bucket, bucketRegion, region);
    s3CredsMap.put(YBC_AWS_ENDPOINT_FIELDNAME, hostBase);
    s3CredsMap.put(YBC_AWS_DEFAULT_REGION_FIELDNAME, bucketRegion);
    return s3CredsMap;
  }

  private void fillMapWithIAMCreds(
      Map<String, String> s3CredsMap, CustomerConfigStorageS3Data s3Data) {
    try {
      AWSCredentials creds =
          (new IAMTemporaryCredentialsProvider(runtimeConfGetter)).getTemporaryCredentials(s3Data);
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
  public ProxySpec getOldProxySpec(CustomerConfigData configData) {
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    if (s3Data.proxySetting != null) {
      ProxySpec.Builder proxyBuilder = ProxySpec.newBuilder();
      proxyBuilder.setHost(s3Data.proxySetting.proxy);
      if (s3Data.proxySetting.port > 0) {
        proxyBuilder.setPort(s3Data.proxySetting.port);
      }
      if (StringUtils.isNotBlank(s3Data.proxySetting.username)
          && StringUtils.isNotBlank(s3Data.proxySetting.password)) {
        proxyBuilder.setPassword(s3Data.proxySetting.password);
        proxyBuilder.setUsername(s3Data.proxySetting.username);
      }
      return proxyBuilder.build();
    }
    return null;
  }

  @Override
  public boolean shouldUseHttpsProxy(CustomerConfigData configData) {
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    return StringUtils.isBlank(s3Data.awsHostBase)
        || isHostBaseS3Standard(s3Data.awsHostBase)
        || s3Data.awsHostBase.startsWith("https://");
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

  public static Double getAwsSpotPrice(
      String zone, String instanceType, String providerUUID, String region) {
    try {
      Provider p = Provider.getOrBadRequest(UUID.fromString(providerUUID));
      AWSCredentials creds =
          new BasicAWSCredentials(
              p.getDetails().getCloudInfo().getAws().awsAccessKeyID,
              p.getDetails().getCloudInfo().getAws().awsAccessKeySecret);
      AmazonEC2 ec2Client =
          AmazonEC2ClientBuilder.standard()
              .withCredentials(new AWSStaticCredentialsProvider(creds))
              .withRegion(region)
              .build();
      DescribeSpotPriceHistoryRequest request =
          new DescribeSpotPriceHistoryRequest()
              .withAvailabilityZone(zone)
              .withInstanceTypes(instanceType)
              .withProductDescriptions("Linux/UNIX")
              .withStartTime(new Date())
              .withEndTime(new Date());

      DescribeSpotPriceHistoryResult result = ec2Client.describeSpotPriceHistory(request);
      List<SpotPrice> prices = result.getSpotPriceHistory();
      Double spotPrice = Double.parseDouble(prices.get(0).getSpotPrice());
      log.info(
          "Current aws spot price for instance type {} in zone {} = {}",
          instanceType,
          zone,
          spotPrice);
      return spotPrice;
    } catch (Exception e) {
      log.warn("Fetch AWS spot price failed with error; {}", e.getMessage());
    }
    return Double.NaN;
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

  /**
   * Validates create permissions on the S3 configuration on default region and other regions, apart
   * read, list or delete permissions if specified.
   */
  @Override
  public void validate(CustomerConfigData configData, List<ExtraPermissionToValidate> permissions)
      throws Exception {
    CustomerConfigStorageS3Data s3data = (CustomerConfigStorageS3Data) configData;
    if (StringUtils.isEmpty(s3data.awsAccessKeyId)
        || StringUtils.isEmpty(s3data.awsSecretAccessKey)) {
      if (!s3data.isIAMInstanceProfile) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Aws credentials are null and IAM profile is not used.");
      }
    }

    AmazonS3 s3Client = null;
    String exceptionMsg = null;
    try {
      s3Client = createS3Client(s3data);
    } catch (AmazonS3Exception s3Exception) {
      exceptionMsg = s3Exception.getErrorMessage();
      throw new RuntimeException(exceptionMsg);
    }

    validateOnLocation(s3Client, YbcBackupUtil.DEFAULT_REGION_STRING, configData, permissions);

    if (s3data.regionLocations != null) {
      for (RegionLocations location : s3data.regionLocations) {
        if (StringUtils.isEmpty(location.region)) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Region of RegionLocation: " + location.location + " is empty.");
        }
        try {
          s3Client = createS3Client(s3data, location.region);
        } catch (SdkClientException e) {
          exceptionMsg = e.getMessage();
          throw new RuntimeException(exceptionMsg);
        }
        validateOnLocation(s3Client, location.region, configData, permissions);
      }
    }
  }

  /** Validates S3 configuration on a specific location */
  private void validateOnLocation(
      AmazonS3 client,
      String region,
      CustomerConfigData configData,
      List<ExtraPermissionToValidate> permissions) {
    CloudLocationInfo cLInfo = getCloudLocationInfo(region, configData, null);
    validateOnBucket(client, cLInfo.bucket, cLInfo.cloudPath, permissions);
  }

  /**
   * Validates create permission on a bucket, apart from read, list or delete permissions if
   * specified.
   */
  public void validateOnBucket(
      AmazonS3 client,
      String bucketName,
      String prefix,
      List<ExtraPermissionToValidate> permissions) {
    Optional<ExtraPermissionToValidate> unsupportedPermission =
        permissions.stream()
            .filter(
                permission ->
                    permission != ExtraPermissionToValidate.READ
                        && permission != ExtraPermissionToValidate.LIST)
            .findAny();

    if (unsupportedPermission.isPresent()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Unsupported permission "
              + unsupportedPermission.get().toString()
              + " validation is not supported!");
    }

    String objectName = getRandomUUID().toString() + ".txt";
    String completeObjectPath = BackupUtil.getPathWithPrefixSuffixJoin(prefix, objectName);

    createObject(client, bucketName, DUMMY_DATA, completeObjectPath);
    log.debug("S3: Test object created");

    if (permissions.contains(ExtraPermissionToValidate.READ)) {
      validateReadObject(client, bucketName, completeObjectPath, DUMMY_DATA);
      log.debug("S3: Test object read");
    }

    if (permissions.contains(ExtraPermissionToValidate.LIST)) {
      validateListObjects(client, bucketName, completeObjectPath);
      log.debug("S3: Test object listed");
    }

    validateDeleteObject(client, bucketName, completeObjectPath);
    log.debug("S3: Test object deleted");
  }

  private void createObject(AmazonS3 client, String bucketName, String content, String fileName) {
    client.putObject(bucketName, fileName, content);
  }

  private void validateReadObject(
      AmazonS3 client, String bucketName, String objectName, String content) {
    String readString = readObject(client, bucketName, objectName, content.getBytes().length);
    if (!readString.equals(content)) {
      throw new PlatformServiceException(
          EXPECTATION_FAILED,
          "Error reading test object "
              + objectName
              + ", expected: \""
              + content
              + "\", got: \""
              + readString
              + "\"");
    }
  }

  private String readObject(
      AmazonS3 client, String bucketName, String objectName, int bytesToRead) {
    S3Object object = client.getObject(bucketName, objectName);
    if (object == null) {
      throw new PlatformServiceException(
          EXPECTATION_FAILED,
          "Created object " + objectName + " was not found in bucket " + bucketName);
    }
    byte[] data = new byte[bytesToRead];
    try (InputStream ois = object.getObjectContent()) {
      ois.read(data);
    } catch (IOException e) {
      throw new PlatformServiceException(
          EXPECTATION_FAILED,
          "Error reading test object " + objectName + ", exception occurred: " + getStackTrace(e));
    }

    return new String(data);
  }

  private void validateListObjects(AmazonS3 client, String bucketName, String objectName) {
    if (!listContainsObject(client, bucketName, objectName)) {
      throw new PlatformServiceException(
          EXPECTATION_FAILED,
          "Test object "
              + objectName
              + " was not found in bucket "
              + bucketName
              + " objects list.");
    }
  }

  private boolean listContainsObject(AmazonS3 client, String bucketName, String objectName) {
    Optional<S3ObjectSummary> objSum;
    ListObjectsV2Result objListing = client.listObjectsV2(bucketName, objectName);
    objSum =
        objListing.getObjectSummaries().parallelStream()
            .filter(oS -> oS.getKey().equals(objectName))
            .findAny();
    return objSum.isPresent();
  }

  private void validateDeleteObject(AmazonS3 client, String bucketName, String objectName) {
    client.deleteObject(bucketName, objectName);
    if (client.doesObjectExist(bucketName, objectName)) {
      throw new PlatformServiceException(
          EXPECTATION_FAILED, "Test object " + objectName + " was found in bucket " + bucketName);
    }
  }

  public UniverseInterruptionResult spotInstanceUniverseStatus(Universe universe) {
    UniverseInterruptionResult result = new UniverseInterruptionResult(universe.getName());
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    Provider primaryClusterProvider =
        Provider.getOrBadRequest(UUID.fromString(userIntent.provider));
    UUID primaryClusterUUID = universe.getUniverseDetails().getPrimaryCluster().uuid;

    // For nodes in primary cluster
    for (final NodeDetails nodeDetails : universe.getNodesInCluster(primaryClusterUUID)) {
      result.addNodeStatus(
          nodeDetails.nodeName,
          isSpotInstanceInterrupted(nodeDetails, primaryClusterProvider)
              ? InterruptionStatus.Interrupted
              : InterruptionStatus.NotInterrupted);
    }
    // For nodes in read replicas
    for (Cluster cluster : universe.getUniverseDetails().getReadOnlyClusters()) {
      Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
      for (final NodeDetails nodeDetails : universe.getNodesInCluster(cluster.uuid)) {
        result.addNodeStatus(
            nodeDetails.nodeName,
            isSpotInstanceInterrupted(nodeDetails, provider)
                ? InterruptionStatus.Interrupted
                : InterruptionStatus.NotInterrupted);
      }
    }
    return result;
  }

  private boolean isSpotInstanceInterrupted(NodeDetails nodeDetails, Provider provider) {
    try {
      String instanceID = getInstanceIDFromName(nodeDetails, provider, nodeDetails.getRegion());
      AWSCloudTrail client = awsCloudImpl.getCloudTrailClient(provider, nodeDetails.getRegion());
      LookupEventsRequest request =
          new LookupEventsRequest()
              .withLookupAttributes(
                  new LookupAttribute()
                      .withAttributeKey(LookupAttributeKey.ResourceName)
                      .withAttributeValue(instanceID));
      LookupEventsResult response;
      do {
        response = client.lookupEvents(request);
        for (Event event : response.getEvents()) {
          if (event.getEventName().equalsIgnoreCase("StopInstances")
              && event.getUsername().equalsIgnoreCase("InstanceTermination")) {
            return true;
          }
        }
        request = request.withNextToken(response.getNextToken());
      } while (response.getNextToken() != null);
    } catch (Exception e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format("Fetch interruptions status for AWS failed with %s", e.getMessage()));
    }
    return false;
  }

  private String getInstanceIDFromName(NodeDetails nodeDetails, Provider provider, String region) {
    try {
      AmazonEC2 ec2Client = awsCloudImpl.getEC2Client(provider, region);
      DescribeInstancesRequest request =
          new DescribeInstancesRequest()
              .withFilters(new Filter().withName("tag:Name").withValues(nodeDetails.getNodeName()));
      DescribeInstancesResult response = ec2Client.describeInstances(request);
      if (response.getReservations().isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format("No AWS instance found with name : ", nodeDetails.getNodeName()));
      }
      List<Instance> instances = response.getReservations().get(0).getInstances();
      String result = "";
      // Choose instance with same IP
      for (Instance instance : instances) {
        if (instance.getPrivateIpAddress().equals(nodeDetails.cloudInfo.private_ip)) {
          result = instance.getInstanceId();
          break;
        }
      }
      if (result.equals("")) {
        throw new PlatformServiceException(
            BAD_REQUEST, "No AWS instance found with IP : " + nodeDetails.cloudInfo.private_ip);
      }
      return result;
    } catch (Exception e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format("Get instance id from name for aws failed with %s", e.getMessage()));
    }
  }
}
