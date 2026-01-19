// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.Util.HTTPS_SCHEME;
import static org.apache.commons.lang3.exception.ExceptionUtils.getStackTrace;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.EXPECTATION_FAILED;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.PRECONDITION_FAILED;

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
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data.RegionLocations;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URI;
import java.net.URISyntaxException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.KeyStore;
import java.security.SecureRandom;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Collections;
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
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.http.conn.ssl.NoopHostnameVerifier;
import org.apache.http.conn.ssl.SSLConnectionSocketFactory;
import org.yb.ybc.CloudStoreSpec;
import org.yb.ybc.CloudType;
import org.yb.ybc.ProxySpec;
import software.amazon.awssdk.auth.credentials.AwsBasicCredentials;
import software.amazon.awssdk.auth.credentials.AwsCredentials;
import software.amazon.awssdk.auth.credentials.AwsSessionCredentials;
import software.amazon.awssdk.auth.credentials.StaticCredentialsProvider;
import software.amazon.awssdk.core.ResponseBytes;
import software.amazon.awssdk.core.ResponseInputStream;
import software.amazon.awssdk.core.exception.SdkClientException;
import software.amazon.awssdk.core.sync.RequestBody;
import software.amazon.awssdk.http.apache.ApacheHttpClient;
import software.amazon.awssdk.regions.Region;
import software.amazon.awssdk.regions.providers.DefaultAwsRegionProviderChain;
import software.amazon.awssdk.services.cloudtrail.CloudTrailClient;
import software.amazon.awssdk.services.cloudtrail.model.Event;
import software.amazon.awssdk.services.cloudtrail.model.LookupAttribute;
import software.amazon.awssdk.services.cloudtrail.model.LookupAttributeKey;
import software.amazon.awssdk.services.cloudtrail.model.LookupEventsRequest;
import software.amazon.awssdk.services.cloudtrail.model.LookupEventsResponse;
import software.amazon.awssdk.services.ec2.Ec2Client;
import software.amazon.awssdk.services.ec2.model.DescribeInstancesRequest;
import software.amazon.awssdk.services.ec2.model.DescribeInstancesResponse;
import software.amazon.awssdk.services.ec2.model.DescribeSpotPriceHistoryRequest;
import software.amazon.awssdk.services.ec2.model.DescribeSpotPriceHistoryResponse;
import software.amazon.awssdk.services.ec2.model.Filter;
import software.amazon.awssdk.services.ec2.model.Instance;
import software.amazon.awssdk.services.ec2.model.InstanceType;
import software.amazon.awssdk.services.ec2.model.SpotPrice;
import software.amazon.awssdk.services.s3.S3Client;
import software.amazon.awssdk.services.s3.model.Bucket;
import software.amazon.awssdk.services.s3.model.Delete;
import software.amazon.awssdk.services.s3.model.DeleteObjectRequest;
import software.amazon.awssdk.services.s3.model.DeleteObjectsRequest;
import software.amazon.awssdk.services.s3.model.GetBucketLocationRequest;
import software.amazon.awssdk.services.s3.model.GetObjectRequest;
import software.amazon.awssdk.services.s3.model.GetObjectResponse;
import software.amazon.awssdk.services.s3.model.ListObjectsV2Request;
import software.amazon.awssdk.services.s3.model.ListObjectsV2Response;
import software.amazon.awssdk.services.s3.model.NoSuchKeyException;
import software.amazon.awssdk.services.s3.model.ObjectIdentifier;
import software.amazon.awssdk.services.s3.model.PutObjectRequest;
import software.amazon.awssdk.services.s3.model.S3Exception;
import software.amazon.awssdk.services.s3.model.S3Object;

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
  private static final String AWS_STANDARD_HOST_BASE_PATTERN =
      // Allows 's3', followed by any number of alphanumerics and dots and hyphens (or nothing),
      // followed by '.amazonaws.com'
      "^s3(?:[a-zA-Z0-9.-]+)?[.]amazonaws[.]com$";
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

  @AllArgsConstructor
  @Data
  public static class AWSHostBase {
    public String awsHostBase;
    public boolean globalBucketAccess;
    public String signingRegion;
  }

  private void tryListObjects(S3Client s3Client, String bucketName, String prefix)
      throws SdkClientException {
    // HeadBucket to check if bucket exists (doesBucketExistV2 is not available in SDK v2)
    try {
      s3Client.headBucket(builder -> builder.bucket(bucketName));
    } catch (S3Exception e) {
      if (e.statusCode() == play.mvc.Http.Status.NOT_FOUND) {
        // Bucket does not exist
        throw new RuntimeException(String.format("No S3 bucket found with name %s", bucketName));
      } else {
        throw e; // rethrow other errors
      }
    }
    ListObjectsV2Request.Builder requestBuilder = ListObjectsV2Request.builder().bucket(bucketName);
    if (StringUtils.isNotBlank(prefix)) {
      requestBuilder.prefix(prefix);
    }
    ListObjectsV2Response result = s3Client.listObjectsV2(requestBuilder.build());
    if (result.keyCount() == 0) {
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
        try (S3Client s3Client = createS3Client(s3Data, region)) {
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
          // Use "cloudDir" of success marker as object prefix
          String prefix = regionPrefix.getValue().cloudDir;
          // Use config's bucket for bucket name
          String bucketName = getCloudLocationInfo(regionPrefix.getKey(), configData, null).bucket;
          log.debug("Trying object listing with S3 bucket {} and prefix {}", bucketName, prefix);
          CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
          try (S3Client s3Client = createS3Client(s3Data, region)) {
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
  public boolean isIamEnabled(CustomerConfig config) {
    return ((CustomerConfigStorageS3Data) config.getDataObject()).isIAMInstanceProfile;
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

    S3Client s3Client = null;
    try {
      maybeDisableCertVerification();
      s3Client = createS3Client((CustomerConfigStorageS3Data) configData);

      ListObjectsV2Request listReq =
          ListObjectsV2Request.builder().bucket(bucketName).prefix(keyLocation).build();

      ListObjectsV2Response listResp = s3Client.listObjectsV2(listReq);

      if (listResp.keyCount() == 0) {
        log.info("Specified Location " + keyLocation + " does not contain objects");
      } else {
        log.debug("Retrieved blobs info for bucket " + bucketName + " with prefix " + keyLocation);
        retrieveAndDeleteObjects(listResp, bucketName, s3Client);
      }

    } catch (Exception e) {
      log.error("Error while deleting key object at location: {}", keyLocation, e);
      return false;
    } finally {
      maybeEnableCertVerification();
      if (s3Client != null) {
        s3Client.close(); // Clean up resources
      }
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
    S3Client s3Client = null;
    try {
      maybeDisableCertVerification();
      CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
      s3Client = createS3Client(s3Data);
      CloudLocationInfo cLInfo =
          getCloudLocationInfo(YbcBackupUtil.DEFAULT_REGION_STRING, s3Data, "");
      // Construct full path to upload like (s3://bucket/)cloudPath/backupDir/backupName.tar.gz
      String keyName =
          Stream.of(stripSlash(cLInfo.cloudPath), backupDir, backup.getName())
              .filter(s -> !s.isEmpty())
              .collect(Collectors.joining("/"));

      PutObjectRequest request =
          PutObjectRequest.builder()
              .bucket(cLInfo.bucket)
              .key(keyName)
              .contentType("application/octet-stream")
              .build();
      s3Client.putObject(request, RequestBody.fromFile(backup));

      // Construct full path to marker file s3://bucket/cloudPath/backupDir/.yba_backup_marker
      File markerFile = File.createTempFile("backup_marker", ".txt");
      markerFile.deleteOnExit();
      String markerKey =
          Stream.of(stripSlash(cLInfo.cloudPath), backupDir, YBA_BACKUP_MARKER)
              .filter(s -> !s.isEmpty())
              .collect(Collectors.joining("/"));

      // Upload the file to S3
      PutObjectRequest markerRequest =
          PutObjectRequest.builder().bucket(cLInfo.bucket).key(markerKey).build();
      s3Client.putObject(markerRequest, RequestBody.fromFile(markerFile));
      return true;
    } catch (Exception e) {
      log.error("Error uploading backup: {}", e);
    } finally {
      maybeEnableCertVerification();
      if (s3Client != null) {
        s3Client.close();
      }
    }
    return false;
  }

  @Override
  public String getYbaBackupStorageLocation(CustomerConfigData configData, String backupDir) {
    try {
      maybeDisableCertVerification();
      CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
      CloudLocationInfo cLInfo =
          getCloudLocationInfo(YbcBackupUtil.DEFAULT_REGION_STRING, s3Data, "");
      String location =
          Stream.of(stripSlash(cLInfo.bucket), stripSlash(cLInfo.cloudPath), stripSlash(backupDir))
              .filter(s -> !s.isEmpty())
              .collect(Collectors.joining("/"));
      return String.format("s3://%s", location);
    } catch (Exception e) {
      log.error("Error determining storage location: {}", e);
    } finally {
      maybeEnableCertVerification();
    }
    return null;
  }

  @Override
  public List<String> getYbaBackupDirs(CustomerConfigData configData) {
    S3Client s3Client = null;
    try {
      maybeDisableCertVerification();
      CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
      s3Client = createS3Client(s3Data);
      CloudLocationInfo cLInfo =
          getCloudLocationInfo(YbcBackupUtil.DEFAULT_REGION_STRING, s3Data, "");

      String prefix =
          Stream.of(stripSlash(cLInfo.cloudPath))
              .filter(s -> !s.isEmpty())
              .collect(Collectors.joining("/"));
      ListObjectsV2Request request =
          ListObjectsV2Request.builder().bucket(cLInfo.bucket).prefix(prefix).build();

      Set<String> backupDirs = new HashSet<>();

      String nextContinuationToken = null;
      do {
        ListObjectsV2Request req = request;
        if (StringUtils.isNotBlank(nextContinuationToken)) {
          req = request.toBuilder().continuationToken(nextContinuationToken).build();
        }
        ListObjectsV2Response result = s3Client.listObjectsV2(req);
        nextContinuationToken = result.isTruncated() ? result.nextContinuationToken() : null;
        for (S3Object object : result.contents()) {
          String key = object.key();
          String backupDir = extractBackupDirFromKey(key, stripSlash(cLInfo.cloudPath));
          if (StringUtils.isNotBlank(backupDir)) {
            backupDirs.add(backupDir);
          }
        }
      } while (nextContinuationToken != null);

      return new ArrayList<>(backupDirs);
    } catch (Exception e) {
      log.error("Error retrieving backup directories: {}", e);
    } finally {
      maybeEnableCertVerification();
      if (s3Client != null) {
        s3Client.close();
      }
    }
    return Collections.emptyList();
  }

  @Override
  public boolean uploadYBDBRelease(
      CustomerConfigData configData, File release, String backupDir, String version) {
    S3Client s3Client = null;
    try {
      maybeDisableCertVerification();
      CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
      s3Client = createS3Client(s3Data);
      CloudLocationInfo cLInfo =
          getCloudLocationInfo(YbcBackupUtil.DEFAULT_REGION_STRING, s3Data, "");
      String keyName =
          Stream.of(
                  stripSlash(cLInfo.cloudPath),
                  backupDir,
                  YBDB_RELEASES,
                  version,
                  release.getName())
              .filter(s -> !s.isEmpty())
              .collect(Collectors.joining("/"));
      long startTime = System.nanoTime();

      PutObjectRequest request =
          PutObjectRequest.builder()
              .bucket(cLInfo.bucket)
              .key(keyName)
              .contentType("application/octet-stream")
              .build();
      s3Client.putObject(request, RequestBody.fromFile(release));
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
      if (s3Client != null) {
        s3Client.close();
      }
    }
    return true;
  }

  @Override
  public Set<String> getRemoteReleaseVersions(CustomerConfigData configData, String backupDir) {
    S3Client s3Client = null;
    try {
      maybeDisableCertVerification();
      CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
      s3Client = createS3Client(s3Data);
      CloudLocationInfo cLInfo =
          getCloudLocationInfo(YbcBackupUtil.DEFAULT_REGION_STRING, s3Data, "");

      // Get all the backups in the specified bucket/directory
      Set<String> releaseVersions = new HashSet<>();
      String nextContinuationToken = null;
      do {
        String prefix =
            Stream.of(stripSlash(cLInfo.cloudPath), backupDir, YBDB_RELEASES)
                .filter(s -> !s.isEmpty())
                .collect(Collectors.joining("/"));
        ListObjectsV2Request.Builder reqBuilder =
            ListObjectsV2Request.builder().bucket(cLInfo.bucket).prefix(prefix);
        if (StringUtils.isNotBlank(nextContinuationToken)) {
          reqBuilder.continuationToken(nextContinuationToken);
        }
        ListObjectsV2Response listObjectsResult;
        listObjectsResult = s3Client.listObjectsV2(reqBuilder.build());

        if (listObjectsResult.keyCount() == 0) {
          break;
        }
        nextContinuationToken = null;
        if (listObjectsResult.isTruncated()) {
          nextContinuationToken = listObjectsResult.nextContinuationToken();
        }
        // Parse out release version from result
        List<S3Object> releases = listObjectsResult.contents();
        for (S3Object release : releases) {
          String version = extractReleaseVersion(release.key(), backupDir, cLInfo.cloudPath);
          if (version != null) {
            releaseVersions.add(version);
          }
        }
      } while (nextContinuationToken != null);

      return releaseVersions;
    } catch (S3Exception e) {
      log.error("AWS Error occurred while listing releases in S3: {}", e.getMessage());
    } catch (Exception e) {
      log.error("Unexpected exception while listing releases in S3: {}", e.getMessage());
    } finally {
      maybeEnableCertVerification();
      if (s3Client != null) {
        s3Client.close();
      }
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
      S3Client s3Client = null;
      try {
        maybeDisableCertVerification();
        CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
        s3Client = createS3Client(s3Data);
        CloudLocationInfo cLInfo =
            getCloudLocationInfo(YbcBackupUtil.DEFAULT_REGION_STRING, s3Data, "");

        // Get all the releases in the specified bucket/directory
        String nextContinuationToken = null;

        do {
          String prefix =
              Stream.of(stripSlash(cLInfo.cloudPath), backupDir, YBDB_RELEASES, version)
                  .filter(s -> !s.isEmpty())
                  .collect(Collectors.joining("/"));
          // List objects with prefix matching version number
          ListObjectsV2Request.Builder requestBuilder =
              ListObjectsV2Request.builder().bucket(cLInfo.bucket).prefix(prefix);

          if (StringUtils.isNotBlank(nextContinuationToken)) {
            requestBuilder.continuationToken(nextContinuationToken);
          }

          ListObjectsV2Response listObjectsResult;
          listObjectsResult = s3Client.listObjectsV2(requestBuilder.build());

          if (listObjectsResult.keyCount() == 0) {
            // No releases found for a specified version (best effort, might work later)
            continue;
          }

          nextContinuationToken =
              listObjectsResult.isTruncated() ? listObjectsResult.nextContinuationToken() : null;

          List<S3Object> releases = listObjectsResult.contents();
          // Download found releases individually (same version, different arch)
          for (S3Object release : releases) {

            // Name the local file same as S3 key basename (yugabyte-version-arch.tar.gz)
            File localRelease =
                versionPath
                    .resolve(release.key().substring(release.key().lastIndexOf('/') + 1))
                    .toFile();

            log.info("Attempting to download from S3 {} to {}", release.key(), localRelease);

            GetObjectRequest getRequest =
                GetObjectRequest.builder().bucket(cLInfo.bucket).key(release.key()).build();
            try (ResponseInputStream<GetObjectResponse> inputStream =
                    s3Client.getObject(getRequest);
                FileOutputStream outputStream = new FileOutputStream(localRelease)) {
              byte[] buffer = new byte[1024];
              int bytesRead;
              while ((bytesRead = inputStream.read(buffer)) != -1) {
                outputStream.write(buffer, 0, bytesRead);
              }

            } catch (Exception e) {
              log.error(
                  "Error downloading {} to {}: {}", release.key(), localRelease, e.getMessage());
              return false;
            }
          }
        } while (nextContinuationToken != null);

      } catch (S3Exception e) {
        log.error("AWS Error occurred while downloading releases from S3: {}", e.getMessage());
        return false;
      } catch (Exception e) {
        log.error("Unexpected exception while downloading releases from S3: {}", e.getMessage());
        return false;
      } finally {
        maybeEnableCertVerification();
        if (s3Client != null) {
          s3Client.close();
        }
      }
    }
    return true;
  }

  @Override
  public File downloadYbaBackup(CustomerConfigData configData, String backupDir, Path localDir) {
    S3Client client = null;
    try {
      maybeDisableCertVerification();
      CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
      client = createS3Client(s3Data);
      CloudLocationInfo cLInfo =
          getCloudLocationInfo(YbcBackupUtil.DEFAULT_REGION_STRING, s3Data, "");

      log.info(
          "Downloading most recent backup in s3://{}/{}{}",
          cLInfo.bucket,
          StringUtils.isBlank(cLInfo.cloudPath) ? "" : stripSlash(cLInfo.cloudPath) + "/",
          backupDir);

      List<S3Object> allBackups = new ArrayList<>();
      String nextContinuationToken = null;

      do {
        String prefix =
            Stream.of(stripSlash(cLInfo.cloudPath), backupDir)
                .filter(s -> !s.isEmpty())
                .collect(Collectors.joining("/"));

        ListObjectsV2Request.Builder listBuilder =
            ListObjectsV2Request.builder().bucket(cLInfo.bucket).prefix(prefix);

        if (StringUtils.isNotBlank(nextContinuationToken)) {
          listBuilder.continuationToken(nextContinuationToken);
        }

        ListObjectsV2Response listResp = client.listObjectsV2(listBuilder.build());

        if (listResp.keyCount() == 0) {
          break;
        }
        nextContinuationToken = null;
        if (listResp.isTruncated()) {
          nextContinuationToken = listResp.nextContinuationToken();
        }
        allBackups.addAll(listResp.contents());
      } while (nextContinuationToken != null);

      // Sort backups by last modified date (most recent first)
      List<S3Object> sortedBackups =
          allBackups.stream()
              .sorted((o1, o2) -> o2.lastModified().compareTo(o1.lastModified()))
              .collect(Collectors.toList());
      if (sortedBackups.isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Could not find YB Anywhere backup in S3 bucket");
      }
      // Iterate through until we find a backup
      S3Object backup = null;
      Matcher match = null;
      Pattern backupPattern = Pattern.compile("backup_.*\\.tgz");
      for (S3Object bkp : sortedBackups) {
        match = backupPattern.matcher(bkp.key());
        if (match.find()) {
          log.info("Downloading backup s3://{}/{}", cLInfo.bucket, bkp.key());
          backup = bkp;
          break;
        }
      }

      if (backup == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Could not find matching YB Anywhere backup in S3 bucket");
      }

      // Validate backup file is less than 1 day old
      if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.allowYbaRestoreWithOldBackup)) {
        if (backup.lastModified().isBefore(Instant.now().minus(1, ChronoUnit.DAYS))) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "YB Anywhere restore is not allowed when backup file is more than 1 day old, enable"
                  + " runtime flag yb.yba_backup.allow_restore_with_old_backup to continue");
        }
      }

      // Construct full local filepath with same name as remote backup
      File localFile = localDir.resolve(match.group()).toFile();

      // Create directory at platformReplication/<storageConfigUUID> if necessary
      Files.createDirectories(localFile.getParentFile().toPath());
      GetObjectRequest getRequest =
          GetObjectRequest.builder().bucket(cLInfo.bucket).key(backup.key()).build();

      try (ResponseInputStream<GetObjectResponse> inputStream = client.getObject(getRequest);
          FileOutputStream outputStream = new FileOutputStream(localFile)) {

        // Write the object content to the local file
        byte[] buffer = new byte[1024];
        int bytesRead;
        while ((bytesRead = inputStream.read(buffer)) != -1) {
          outputStream.write(buffer, 0, bytesRead);
        }

      } catch (Exception e) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Error writing S3 object to file: " + e.getMessage());
      }

      if (!localFile.exists() || localFile.length() == 0) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Local file does not exist or is empty, aborting restore.");
      }

      log.info("Downloaded file from S3 to {}", localFile.getCanonicalPath());
      return localFile;
    } catch (S3Exception e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "AWS error downloading YB Anywhere backup: " + e.awsErrorDetails().errorMessage());
    } catch (IOException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "IO error occurred while downloading object from S3: " + e.getMessage());
    } finally {
      maybeEnableCertVerification();
      if (client != null) {
        client.close();
      }
    }
  }

  @Override
  public boolean cleanupUploadedBackups(CustomerConfigData configData, String backupDir) {
    S3Client s3Client = null;
    try {
      maybeDisableCertVerification();
      CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
      s3Client = createS3Client(s3Data);
      CloudLocationInfo cLInfo =
          getCloudLocationInfo(YbcBackupUtil.DEFAULT_REGION_STRING, s3Data, "");
      log.info(
          "Cleaning up uploaded backups in S3 location s3://{}/{}{}",
          cLInfo.bucket,
          StringUtils.isBlank(cLInfo.cloudPath) ? "" : stripSlash(cLInfo.cloudPath) + "/",
          backupDir);

      // Get all the backups in the specified bucket/directory
      List<S3Object> allBackups = new ArrayList<>();
      String nextContinuationToken = null;

      do {
        String prefix =
            Stream.of(stripSlash(cLInfo.cloudPath), backupDir)
                .filter(s -> !s.isEmpty())
                .collect(Collectors.joining("/"));
        ListObjectsV2Request.Builder requestBuilder =
            ListObjectsV2Request.builder().bucket(cLInfo.bucket).prefix(prefix);
        if (StringUtils.isNotBlank(nextContinuationToken)) {
          requestBuilder.continuationToken(nextContinuationToken);
        }
        ListObjectsV2Response listObjectsResp;
        listObjectsResp = s3Client.listObjectsV2(requestBuilder.build());

        if (listObjectsResp.keyCount() == 0) {
          break;
        }
        nextContinuationToken = null;
        if (listObjectsResp.isTruncated()) {
          nextContinuationToken = listObjectsResp.nextContinuationToken();
        }
        allBackups.addAll(listObjectsResp.contents());
      } while (nextContinuationToken != null);

      Pattern backupPattern = Pattern.compile("backup_.*\\.tgz");
      // Sort backups by last modified date (most recent first)
      List<S3Object> sortedBackups =
          allBackups.stream()
              .filter(
                  o -> !o.key().contains(YBDB_RELEASES) && backupPattern.matcher(o.key()).find())
              .sorted((o1, o2) -> o2.lastModified().compareTo(o1.lastModified()))
              .collect(Collectors.toList());

      // Only keep the n most recent backups
      int numKeepBackups =
          runtimeConfGetter.getGlobalConf(GlobalConfKeys.numCloudYbaBackupsRetention);
      if (sortedBackups.size() <= numKeepBackups) {
        log.info(
            "No backups to delete, only {} backups in s3://{}/{}{} less than limit {}",
            sortedBackups.size(),
            cLInfo.bucket,
            StringUtils.isBlank(cLInfo.cloudPath) ? "" : stripSlash(cLInfo.cloudPath) + "/",
            backupDir,
            numKeepBackups);
        return true;
      }

      // Prepare list of ObjectIdentifiers from sortedBackups
      List<ObjectIdentifier> backupsToDelete =
          sortedBackups.subList(numKeepBackups, sortedBackups.size()).stream()
              .map(o -> ObjectIdentifier.builder().key(o.key()).build())
              .collect(Collectors.toList());

      // Prepare delete request
      DeleteObjectsRequest deleteRequest =
          DeleteObjectsRequest.builder()
              .bucket(cLInfo.bucket)
              .delete(Delete.builder().objects(backupsToDelete).build())
              .build();

      // Delete the old backups
      s3Client.deleteObjects(deleteRequest);
      log.info(
          "Deleted {} old backup(s) from s3://{}/{}{}",
          backupsToDelete.size(),
          cLInfo.bucket,
          StringUtils.isBlank(cLInfo.cloudPath) ? "" : stripSlash(cLInfo.cloudPath) + "/",
          backupDir);
    } catch (S3Exception e) {
      log.warn("Error occured while deleted objects in S3: {}", e.getMessage());
      return false;
    } catch (Exception e) {
      log.warn(
          "Unexpected exception while attempting to cleanup S3 YBA backup: {}", e.getMessage());
      return false;
    } finally {
      maybeEnableCertVerification();
      if (s3Client != null) {
        s3Client.close();
      }
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
        region = new DefaultAwsRegionProviderChain().getRegion().id();
      } catch (SdkClientException e) {
        log.trace("No region found in Default region chain.");
      }
    }
    return StringUtils.isBlank(region) ? "us-east-1" : region;
  }

  public S3Client createS3Client(CustomerConfigStorageS3Data s3Data)
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
  public S3Client createS3Client(CustomerConfigStorageS3Data s3Data, String region)
      throws SdkClientException, PlatformServiceException {
    S3Client client = null;
    var builder = S3Client.builder();

    if (s3Data.isIAMInstanceProfile) {
      // Using credential chaining here.
      // This first looks for K8s service account IAM role,
      // then IAM user,
      // then falls back to the Node/EC2 IAM role.
      try {
        builder.credentialsProvider(
            StaticCredentialsProvider.create(
                (new IAMTemporaryCredentialsProvider(runtimeConfGetter))
                    .getTemporaryCredentials(s3Data)));
      } catch (Exception e) {
        log.error("Fetching IAM credentials failed: {}", e.getMessage());
        throw new PlatformServiceException(PRECONDITION_FAILED, e.getMessage());
      }
    } else {
      String key = s3Data.awsAccessKeyId;
      String secret = s3Data.awsSecretAccessKey;
      AwsBasicCredentials credentials = AwsBasicCredentials.create(key, secret);
      builder.credentialsProvider(StaticCredentialsProvider.create(credentials));
    }
    if (s3Data.isPathStyleAccess) {
      builder.forcePathStyle(true);
    }
    try {
      //  Use region specific hostbase
      AWSHostBase hostBase = getRegionHostBaseMap(s3Data).get(region);
      String endpoint = hostBase.awsHostBase;
      boolean globalFlag = hostBase.globalBucketAccess;
      String clientRegion = getClientRegion(hostBase.signingRegion);
      if (globalFlag && (StringUtils.isBlank(endpoint) || isHostBaseS3Standard(endpoint))) {
        // Use global bucket access only for standard S3.
        builder.crossRegionAccessEnabled(true);
      } else if (StringUtils.isNotBlank(endpoint)) {
        URI uri = new URI(endpoint);
        if (uri.getScheme() == null) {
          uri = new URI(HTTPS_SCHEME + endpoint);
        }
        builder.endpointOverride(uri);
      }
      // Need to set region because region-chaining may
      // fail if correct environment variables not found.
      builder.region(Region.of(clientRegion));

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

          ApacheHttpClient.Builder httpClientBuilder =
              ApacheHttpClient.builder().socketFactory(sslSocketFactory);

          client = builder.httpClientBuilder(httpClientBuilder).build();
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
        client = builder.build();
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
    } catch (SdkClientException | URISyntaxException e) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Failed to create S3 client, error: %s", e.getMessage()));
    }
  }

  public static Map<String, AWSHostBase> getRegionHostBaseMap(CustomerConfigData configData) {
    CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
    Map<String, AWSHostBase> regionHostBaseMap = new HashMap<>();
    AWSHostBase hostBase =
        new AWSHostBase(s3Data.awsHostBase, s3Data.globalBucketAccess, s3Data.fallbackRegion);
    regionHostBaseMap.put(YbcBackupUtil.DEFAULT_REGION_STRING, hostBase);
    if (CollectionUtils.isNotEmpty(s3Data.regionLocations)) {
      s3Data.regionLocations.stream()
          // Populate default region's host base if empty.
          .forEach(
              rL ->
                  regionHostBaseMap.put(
                      rL.region,
                      new AWSHostBase(rL.awsHostBase, rL.globalBucketAccess, rL.fallbackRegion)));
    }
    return regionHostBaseMap;
  }

  public String getBucketRegion(
      String bucketName, CustomerConfigStorageS3Data s3Data, String region)
      throws SdkClientException {
    S3Client s3Client = null;
    try {
      maybeDisableCertVerification();
      s3Client = createS3Client(s3Data, region);
      return getBucketRegion(bucketName, s3Client);
    } finally {
      maybeEnableCertVerification();
      if (s3Client != null) {
        s3Client.close();
      }
    }
  }

  // For reusing already created client, as in listBuckets function
  // Cert-check is not disabled here, since the scope above this, where the S3 client is created,
  // should disable it.
  private String getBucketRegion(String bucketName, S3Client s3Client) throws SdkClientException {
    try {
      GetBucketLocationRequest request =
          GetBucketLocationRequest.builder().bucket(bucketName).build();
      String region = s3Client.getBucketLocation(request).locationConstraintAsString();
      // AWS SDK v2 returns null or "null" (or "us-east-1") for legacy "US"
      // Buckets in Region us-east-1 have a LocationConstraint of null.
      // Buckets with a LocationConstraint of EU reside in eu-west-1.
      if (StringUtils.isBlank(region) || region.equalsIgnoreCase("null") || region.equals("US")) {
        region = AWS_DEFAULT_REGION;
      } else if (region.equals("EU")) {
        region = "eu-west-1";
      }
      return region;
    } catch (SdkClientException e) {
      log.error(String.format("Fetching bucket region for %s failed", bucketName), e.getMessage());
      throw e;
    }
  }

  public String createBucketRegionSpecificHostBase(String bucketName, String bucketRegion) {
    return String.format(AWS_REGION_SPECIFIC_HOST_BASE_FORMAT, bucketRegion);
  }

  public boolean isHostBaseS3Standard(String hostBase) {
    return standardHostBaseCompiled.matcher(hostBase).matches();
  }

  public String getOrCreateHostBase(
      CustomerConfigStorageS3Data s3Data, String bucketName, String bucketRegion, String region) {
    Map<String, AWSHostBase> hostBaseMap = getRegionHostBaseMap(s3Data);
    AWSHostBase globalHostBase = hostBaseMap.get(region);
    String hostBase = globalHostBase.awsHostBase;
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
      S3Client s3Client = null;
      try {
        maybeDisableCertVerification();
        String region = backupRegionLocations.getKey();
        s3Client = createS3Client((CustomerConfigStorageS3Data) configData, region);
        for (String backupLocation : backupRegionLocations.getValue()) {
          CloudLocationInfo cLInfo = getCloudLocationInfo(region, configData, backupLocation);
          String bucketName = cLInfo.bucket;
          String objectPrefix = cLInfo.cloudPath;
          String nextContinuationToken = null;
          try {
            do {
              ListObjectsV2Request.Builder request =
                  ListObjectsV2Request.builder().bucket(bucketName).prefix(objectPrefix);

              if (StringUtils.isNotBlank(nextContinuationToken)) {
                request.continuationToken(nextContinuationToken);
              }
              ListObjectsV2Response listObjectsResult = s3Client.listObjectsV2(request.build());

              if (listObjectsResult.keyCount() == 0) {
                break;
              }
              nextContinuationToken = null;
              if (listObjectsResult.isTruncated()) {
                nextContinuationToken = listObjectsResult.nextContinuationToken();
              }
              log.debug(
                  "Retrieved blobs info for bucket " + bucketName + " with prefix " + objectPrefix);
              retrieveAndDeleteObjects(listObjectsResult, bucketName, s3Client);
            } while (nextContinuationToken != null);
          } catch (S3Exception e) {
            log.error(
                "Error occured while deleting objects at location {}. Error {}",
                backupLocation,
                e.getMessage());
            throw e;
          }
        }
      } catch (S3Exception e) {
        log.error(" Error occured while deleting objects in S3: {}", e.getMessage());
        return false;
      } finally {
        maybeEnableCertVerification();
        if (s3Client != null) {
          s3Client.close();
        }
      }
    }
    return true;
  }

  @Override
  // This method is in use by ReleaseManager code, which does not contain config location in
  // CustomerConfigData object. Such case would not be allowed for UI generated customer config.
  public InputStream getCloudFileInputStream(CustomerConfigData configData, String cloudPath) {
    S3Client s3Client = null;
    try {
      maybeDisableCertVerification();
      CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) configData;
      s3Client = createS3Client(s3Data);
      String[] splitLocation = getSplitLocationValue(cloudPath);
      String bucketName = splitLocation[0];
      String objectPrefix = splitLocation[1];
      GetObjectRequest getObjectRequest =
          GetObjectRequest.builder().bucket(bucketName).key(objectPrefix).build();
      ResponseInputStream<GetObjectResponse> objectStream = s3Client.getObject(getObjectRequest);
      if (objectStream == null) {
        maybeEnableCertVerification();
        s3Client.close();
        s3Client = null; // Set to null to prevent double cleanup in catch block
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "No object was found at the specified location: " + cloudPath);
      }
      // Capture s3Client in a final variable for use in the anonymous class
      final S3Client finalS3Client = s3Client;
      // Clear the reference so we don't close it in the catch block
      s3Client = null;
      // Wrap the stream to ensure S3Client is closed when the stream is closed.
      // The S3Client must remain open while the stream is in use, otherwise the stream
      // becomes unusable since it's tied to the client's HTTP connection.
      return new FilterInputStream(objectStream) {
        private volatile boolean closed = false;

        @Override
        public void close() throws IOException {
          if (closed) {
            return;
          }
          closed = true;
          try {
            // Close the ResponseInputStream first. If the stream has already reached EOF,
            // the underlying HTTP connection may have been auto-closed, which can cause
            // a SocketException. We catch and ignore this since it's harmless.
            try {
              super.close();
            } catch (java.net.SocketException e) {
              // Socket already closed - this is harmless and can happen when the stream
              // reaches EOF before explicit close. Log at trace level for debugging.
              log.trace("Socket already closed when closing ResponseInputStream", e);
            }
          } finally {
            maybeEnableCertVerification();
            try {
              finalS3Client.close();
            } catch (Exception e) {
              // Log but don't propagate - client close failures shouldn't prevent cleanup
              log.warn("Error closing S3Client", e);
            }
          }
        }
      };
    } catch (PlatformServiceException e) {
      // Re-throw PlatformServiceException as-is
      // Clean up if client was created but stream wasn't returned
      if (s3Client != null) {
        maybeEnableCertVerification();
        s3Client.close();
      }
      throw e;
    } catch (Exception e) {
      // Handle any other exceptions and ensure cleanup
      if (s3Client != null) {
        maybeEnableCertVerification();
        s3Client.close();
      } else {
        maybeEnableCertVerification();
      }
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to get cloud file input stream: " + e.getMessage());
    }
  }

  @Override
  public boolean checkFileExists(
      CustomerConfigData configData,
      Set<String> locations,
      String fileName,
      boolean checkExistsOnAll) {

    // TODO: Note other invocations of this function are in the try block
    // s3Client could not be declared outside try block as it is used in the map function
    maybeDisableCertVerification();
    try (S3Client s3Client = createS3Client((CustomerConfigStorageS3Data) configData)) {
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
                ListObjectsV2Request request =
                    ListObjectsV2Request.builder().bucket(bucketName).prefix(objectSuffix).build();
                ListObjectsV2Response listResult = s3Client.listObjectsV2(request);
                if (listResult.keyCount() > 0) {
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
      ListObjectsV2Response listObjectsResult, String bucketName, S3Client s3Client)
      throws SdkClientException {
    List<S3Object> objectSummary = listObjectsResult.contents();
    List<ObjectIdentifier> objectIdentifiers =
        objectSummary.stream()
            .map(o -> ObjectIdentifier.builder().key(o.key()).build())
            .collect(Collectors.toList());
    DeleteObjectsRequest deleteRequest =
        DeleteObjectsRequest.builder()
            .bucket(bucketName)
            .delete(builder -> builder.objects(objectIdentifiers))
            .build();
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
      AwsCredentials creds =
          (new IAMTemporaryCredentialsProvider(runtimeConfGetter)).getTemporaryCredentials(s3Data);
      s3CredsMap.put(YBC_AWS_ACCESS_KEY_ID_FIELDNAME, creds.accessKeyId());
      s3CredsMap.put(YBC_AWS_SECRET_ACCESS_KEY_FIELDNAME, creds.secretAccessKey());
      if (creds instanceof AwsSessionCredentials) {
        s3CredsMap.put(
            YBC_AWS_ACCESS_TOKEN_FIELDNAME, ((AwsSessionCredentials) creds).sessionToken());
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
    // TODO: Note - other invocations of this fuction are in try block.
    maybeDisableCertVerification();
    try (S3Client client = createS3Client(s3Data)) {
      List<Bucket> buckets = client.listBuckets().buckets();
      buckets.stream()
          .forEach(
              b ->
                  bucketHostBaseMap.put(
                      b.name(),
                      useOriginalHostBase
                          ? s3Data.awsHostBase
                          : createBucketRegionSpecificHostBase(
                              b.name(), getBucketRegion(b.name(), client))));
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
      AwsBasicCredentials credentials =
          AwsBasicCredentials.create(
              p.getDetails().getCloudInfo().getAws().awsAccessKeyID,
              p.getDetails().getCloudInfo().getAws().awsAccessKeySecret);
      Ec2Client ec2Client =
          Ec2Client.builder()
              .region(Region.of(region))
              .credentialsProvider(StaticCredentialsProvider.create(credentials))
              .build();
      DescribeSpotPriceHistoryRequest request =
          DescribeSpotPriceHistoryRequest.builder()
              .availabilityZone(zone)
              .instanceTypes(InstanceType.fromValue(instanceType))
              .productDescriptions("Linux/UNIX")
              .startTime(Instant.now())
              .endTime(Instant.now())
              .build();
      DescribeSpotPriceHistoryResponse result = ec2Client.describeSpotPriceHistory(request);
      List<SpotPrice> prices = result.spotPriceHistory();
      Double spotPrice = Double.parseDouble(prices.get(0).spotPrice());
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
      System.setProperty("com.amazonaws.sdk.disableCertChecking", "true");
    }
  }

  public void maybeEnableCertVerification() {
    if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.enforceCertVerificationBackupRestore)) {
      System.setProperty("com.amazonaws.sdk.disableCertChecking", "false");
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

    S3Client s3Client = null;
    String exceptionMsg = null;
    try {
      s3Client = createS3Client(s3data);
    } catch (SdkClientException s3Exception) {
      exceptionMsg = s3Exception.getMessage();
      throw new RuntimeException(exceptionMsg);
    } finally {
      if (s3Client != null) {
        s3Client.close();
      }
    }

    validateOnLocation(s3Client, YbcBackupUtil.DEFAULT_REGION_STRING, configData, permissions);

    if (s3data.regionLocations != null) {
      for (RegionLocations location : s3data.regionLocations) {
        if (StringUtils.isEmpty(location.region)) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Region of RegionLocation: " + location.location + " is empty.");
        }
        s3Client = null;
        try {
          s3Client = createS3Client(s3data, location.region);
          validateOnLocation(s3Client, location.region, configData, permissions);
        } catch (SdkClientException e) {
          exceptionMsg = e.getMessage();
          throw new RuntimeException(exceptionMsg);
        } finally {
          if (s3Client != null) {
            s3Client.close();
          }
        }
      }
    }
  }

  /** Validates S3 configuration on a specific location */
  private void validateOnLocation(
      S3Client client,
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
      S3Client client,
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

  private void createObject(S3Client client, String bucketName, String content, String fileName) {
    PutObjectRequest request =
        PutObjectRequest.builder()
            .bucket(bucketName)
            .key(fileName)
            .contentType("text/plain")
            .build();
    client.putObject(request, RequestBody.fromString(content));
  }

  private void validateReadObject(
      S3Client client, String bucketName, String objectName, String content) {
    String readString = readObject(client, bucketName, objectName, content.getBytes().length);
    if (!content.equals(readString)) {
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
      S3Client client, String bucketName, String objectName, int bytesToRead) {
    try {
      GetObjectRequest request =
          GetObjectRequest.builder().bucket(bucketName).key(objectName).build();
      ResponseBytes<GetObjectResponse> responseBytes = client.getObjectAsBytes(request);
      return responseBytes.asUtf8String();
    } catch (NoSuchKeyException e) {
      throw new PlatformServiceException(
          EXPECTATION_FAILED,
          "Created object " + objectName + " was not found in bucket " + bucketName);
    } catch (S3Exception e) {
      throw new PlatformServiceException(
          EXPECTATION_FAILED,
          "Error reading test object " + objectName + ", exception occurred: " + getStackTrace(e));
    }
  }

  private void validateListObjects(S3Client client, String bucketName, String objectName) {
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

  private boolean listContainsObject(S3Client client, String bucketName, String objectName) {
    ListObjectsV2Request request =
        ListObjectsV2Request.builder().bucket(bucketName).prefix(objectName).build();
    ListObjectsV2Response response = client.listObjectsV2(request);
    return response.contents().stream().anyMatch(object -> object.key().equals(objectName));
  }

  private void validateDeleteObject(S3Client client, String bucketName, String objectName) {
    DeleteObjectRequest request =
        DeleteObjectRequest.builder().bucket(bucketName).key(objectName).build();
    client.deleteObject(request);
    if (listContainsObject(client, bucketName, objectName)) {
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
      CloudTrailClient client = awsCloudImpl.getCloudTrailClient(provider, nodeDetails.getRegion());
      LookupEventsRequest request =
          LookupEventsRequest.builder()
              .lookupAttributes(
                  LookupAttribute.builder()
                      .attributeKey(LookupAttributeKey.RESOURCE_NAME)
                      .attributeValue(instanceID)
                      .build())
              .build();
      LookupEventsResponse response;
      do {
        response = client.lookupEvents(request);
        for (Event event : response.events()) {
          if (event.eventName().equalsIgnoreCase("StopInstances")
              && event.username().equalsIgnoreCase("InstanceTermination")) {
            return true;
          }
        }
        request = request.toBuilder().nextToken(response.nextToken()).build();
      } while (response.nextToken() != null);
    } catch (Exception e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format("Fetch interruptions status for AWS failed with %s", e.getMessage()));
    }
    return false;
  }

  private String getInstanceIDFromName(NodeDetails nodeDetails, Provider provider, String region) {
    try {
      Ec2Client ec2Client = awsCloudImpl.getEC2Client(provider, region);
      DescribeInstancesRequest request =
          DescribeInstancesRequest.builder()
              .filters(Filter.builder().name("tag:Name").values(nodeDetails.getNodeName()).build())
              .build();
      DescribeInstancesResponse response = ec2Client.describeInstances(request);
      if (response.reservations().isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format("No AWS instance found with name : %s", nodeDetails.getNodeName()));
      }
      List<Instance> instances = response.reservations().get(0).instances();
      String result = "";
      // Choose instance with same IP
      for (Instance instance : instances) {
        if (instance.privateIpAddress().equals(nodeDetails.cloudInfo.private_ip)) {
          result = instance.instanceId();
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
