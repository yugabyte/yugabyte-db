// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.mockito.Spy;
import software.amazon.awssdk.core.ResponseInputStream;
import software.amazon.awssdk.core.sync.RequestBody;
import software.amazon.awssdk.services.s3.S3Client;
import software.amazon.awssdk.services.s3.model.DeleteObjectsRequest;
import software.amazon.awssdk.services.s3.model.GetObjectRequest;
import software.amazon.awssdk.services.s3.model.GetObjectResponse;
import software.amazon.awssdk.services.s3.model.ListObjectsV2Request;
import software.amazon.awssdk.services.s3.model.ListObjectsV2Response;
import software.amazon.awssdk.services.s3.model.ObjectIdentifier;
import software.amazon.awssdk.services.s3.model.PutObjectRequest;
import software.amazon.awssdk.services.s3.model.S3Exception;
import software.amazon.awssdk.services.s3.model.S3Object;

@RunWith(JUnitParamsRunner.class)
@Slf4j
public class AWSUtilTest extends FakeDBApplication {

  S3Client mockS3Client = mock(S3Client.class);
  @Mock RuntimeConfGetter mockConfGetter;
  @Spy @InjectMocks AWSUtil mockAWSUtil;

  @Before
  public void setup() {
    MockitoAnnotations.openMocks(this);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.numCloudYbaBackupsRetention)))
        .thenReturn(2);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.enforceCertVerificationBackupRestore)))
        .thenReturn(true);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.allowYbaRestoreWithOldBackup)))
        .thenReturn(false);
    doReturn(mockS3Client).when(mockAWSUtil).createS3Client(any());
  }

  @Test
  @Parameters({
    // Standard URLs
    "s3.ap-south-1.amazonaws.com, true",
    "s3.amazonaws.com, true",
    "s3-us-east-1.amazonaws.com, true",
    "s3.us-east-1.amazonaws.com, true",
    "s3-control.me-central-1.amazonaws.com, true",
    "s3.dualstack.mx-central-1.amazonaws.com, true",
    "s3-website-us-west-2.amazonaws.com, true",
    // Standard should always end with '.amazonaws.com'
    "s3-amazonaws.com, false",
    // Any random URL - rejected
    "rmn.kiba.local, false",
    // Custom private host bases (standards will start with 's3.')
    "bucket.vpce-abcxyz-lsuyzz63.s3.ap-south-1.vpce.amazonaws.com, false",
    "bucket.vpce-a1.s3.ap-south-1.amazonaws.com, false",
    "bucket.vpce.amazonaws.com, false"
  })
  public void testS3HostBase(String hostBase, boolean expectedResult) {
    boolean actualResult = mockAWSUtil.isHostBaseS3Standard(hostBase);
    assertEquals(actualResult, expectedResult);
  }

  @Test
  public void testUploadYbaBackupWithCloudPath() throws Exception {
    // Setup test data
    CustomerConfigStorageS3Data s3Data = new CustomerConfigStorageS3Data();
    s3Data.awsAccessKeyId = "test-key";
    s3Data.awsSecretAccessKey = "test-secret";
    // Cloud path is backup
    s3Data.backupLocation = "s3://test-bucket/backup";
    s3Data.awsHostBase = "s3.amazonaws.com";

    // Create a temporary test file
    File backupFile = File.createTempFile("test-backup", ".tgz");
    backupFile.deleteOnExit();

    // Test successful upload
    boolean result = mockAWSUtil.uploadYbaBackup(s3Data, backupFile, "test-backup-dir");
    assertTrue(result);

    // Verify backup file was uploaded with exact arguments
    verify(mockS3Client)
        .putObject(
            argThat(
                (PutObjectRequest request) ->
                    request.bucket().equals("test-bucket")
                        && request.key().equals("backup/test-backup-dir/" + backupFile.getName())
                        && request.contentType().equals("application/octet-stream")),
            any(RequestBody.class));

    // Verify marker file was uploaded with exact arguments
    verify(mockS3Client)
        .putObject(
            argThat(
                (PutObjectRequest request) ->
                    request.bucket().equals("test-bucket")
                        && request.key().equals("backup/test-backup-dir/.yba_backup_marker")),
            any(RequestBody.class));
  }

  @Test
  public void testUploadYbaBackupWithEmptyCloudPath() throws Exception {
    // Setup test data
    CustomerConfigStorageS3Data s3Data = new CustomerConfigStorageS3Data();
    s3Data.awsAccessKeyId = "test-key";
    s3Data.awsSecretAccessKey = "test-secret";
    s3Data.backupLocation = "s3://test-bucket";
    s3Data.awsHostBase = "s3.amazonaws.com";

    // Create a temporary test file
    File backupFile = File.createTempFile("test-backup", ".tgz");
    backupFile.deleteOnExit();

    // Test upload with empty backup dir
    boolean result = mockAWSUtil.uploadYbaBackup(s3Data, backupFile, "test-backup-dir");
    assertTrue(result);

    // Verify backup file was uploaded without backup dir in path
    verify(mockS3Client)
        .putObject(
            argThat(
                (PutObjectRequest request) ->
                    request.bucket().equals("test-bucket")
                        && request.key().equals("test-backup-dir/" + backupFile.getName())
                        && request.contentType().equals("application/octet-stream")),
            any(RequestBody.class));

    // Verify marker file was uploaded without backup dir in path
    verify(mockS3Client)
        .putObject(
            argThat(
                (PutObjectRequest request) ->
                    request.bucket().equals("test-bucket")
                        && request.key().equals("test-backup-dir/.yba_backup_marker")),
            any(RequestBody.class));
  }

  @Test
  public void testUploadYbaBackupFailure() throws Exception {
    // Setup test data
    CustomerConfigStorageS3Data s3Data = new CustomerConfigStorageS3Data();
    s3Data.awsAccessKeyId = "test-key";
    s3Data.awsSecretAccessKey = "test-secret";
    s3Data.backupLocation = "s3://test-bucket/backup";
    s3Data.awsHostBase = "s3.amazonaws.com";

    // Create a temporary test file
    File backupFile = File.createTempFile("test-backup", ".tgz");
    backupFile.deleteOnExit();

    // Mock S3 client to throw exception
    doThrow(S3Exception.builder().message("Upload failed").build())
        .when(mockS3Client)
        .putObject(any(PutObjectRequest.class), any(RequestBody.class));

    // Test upload failure
    boolean result = mockAWSUtil.uploadYbaBackup(s3Data, backupFile, "test-backup-dir");
    assertFalse(result);
  }

  @Test
  public void testGetYbaBackupDirs() throws Exception {
    // Setup test data
    CustomerConfigStorageS3Data s3Data = new CustomerConfigStorageS3Data();
    s3Data.awsAccessKeyId = "test-key";
    s3Data.awsSecretAccessKey = "test-secret";
    s3Data.backupLocation = "s3://test-bucket/backup";
    s3Data.awsHostBase = "s3.amazonaws.com";

    // Create mock ListObjectsV2Response with various scenarios
    ListObjectsV2Response mockResult = mock(ListObjectsV2Response.class);
    List<S3Object> objects = new ArrayList<>();

    // Add objects with backup markers at different depths
    S3Object marker1 = mock(S3Object.class);
    when(marker1.key()).thenReturn("backup/dir1/.yba_backup_marker");
    objects.add(marker1);

    S3Object marker2 = mock(S3Object.class);
    when(marker2.key()).thenReturn("backup/dir2/.yba_backup_marker");
    objects.add(marker2);

    // Add objects without backup markers
    S3Object noMarker1 = mock(S3Object.class);
    when(noMarker1.key()).thenReturn("backup/dir3/file.txt");
    objects.add(noMarker1);

    S3Object noMarker2 = mock(S3Object.class);
    when(noMarker2.key()).thenReturn("backup/dir4/subdir/file.txt");
    objects.add(noMarker2);

    S3Object noMarker3 = mock(S3Object.class);
    when(noMarker3.key()).thenReturn("backup/dir5/subdir/.yba_backup_marker");
    objects.add(noMarker3);

    when(mockResult.contents()).thenReturn(objects);
    when(mockResult.isTruncated()).thenReturn(false);
    when(mockS3Client.listObjectsV2(any(ListObjectsV2Request.class))).thenReturn(mockResult);

    // Test successful retrieval of backup directories
    List<String> backupDirs = mockAWSUtil.getYbaBackupDirs(s3Data);

    // Verify we got the correct backup directories
    assertEquals(2, backupDirs.size());
    assertTrue(backupDirs.contains("dir1"));
    assertTrue(backupDirs.contains("dir2"));
  }

  @Test
  public void testGetYbaBackupDirsFailure() throws Exception {
    // Setup test data
    CustomerConfigStorageS3Data s3Data = new CustomerConfigStorageS3Data();
    s3Data.awsAccessKeyId = "test-key";
    s3Data.awsSecretAccessKey = "test-secret";
    s3Data.backupLocation = "s3://test-bucket/backup";
    s3Data.awsHostBase = "s3.amazonaws.com";

    // Mock S3 client to throw S3Exception
    doThrow(S3Exception.builder().message("S3 error").build())
        .when(mockS3Client)
        .listObjectsV2(any(ListObjectsV2Request.class));

    // Test failure scenario
    List<String> backupDirs = mockAWSUtil.getYbaBackupDirs(s3Data);

    // Verify we get an empty list on failure
    assertTrue(backupDirs.isEmpty());
  }

  @Test
  public void testUploadYBDBRelease() throws Exception {
    // Setup test data
    CustomerConfigStorageS3Data s3Data = new CustomerConfigStorageS3Data();
    s3Data.awsAccessKeyId = "test-key";
    s3Data.awsSecretAccessKey = "test-secret";
    s3Data.backupLocation = "s3://test-bucket/backup/cloudPath";
    s3Data.awsHostBase = "s3.amazonaws.com";

    // Create a temporary test file
    File releaseFile = File.createTempFile("test-release", ".tgz");
    releaseFile.deleteOnExit();

    // Test successful upload
    boolean result = mockAWSUtil.uploadYBDBRelease(s3Data, releaseFile, "test-backup-dir", "1.0.0");
    assertTrue(result);

    // Verify backup file was uploaded with exact arguments
    verify(mockS3Client)
        .putObject(
            argThat(
                (PutObjectRequest request) ->
                    request.bucket().equals("test-bucket")
                        && request
                            .key()
                            .equals(
                                "backup/cloudPath/test-backup-dir/ybdb_releases/1.0.0/"
                                    + releaseFile.getName())
                        && request.contentType().equals("application/octet-stream")),
            any(RequestBody.class));
  }

  @Test
  public void testGetRemoteReleaseVersions() throws Exception {
    // Setup test data
    CustomerConfigStorageS3Data s3Data = new CustomerConfigStorageS3Data();
    s3Data.awsAccessKeyId = "test-key";
    s3Data.awsSecretAccessKey = "test-secret";
    s3Data.backupLocation = "s3://test-bucket/backup/cloudPath";
    s3Data.awsHostBase = "s3.amazonaws.com";

    // Create mock ListObjectsV2Response with various scenarios
    ListObjectsV2Response mockResult = mock(ListObjectsV2Response.class);
    List<S3Object> objects = new ArrayList<>();

    // Add objects with release versions
    S3Object version1 = mock(S3Object.class);
    when(version1.key())
        .thenReturn("backup/cloudPath/test-backup-dir/ybdb_releases/1.0.0.0-b1/release.tar.gz");
    objects.add(version1);

    S3Object version2 = mock(S3Object.class);
    when(version2.key())
        .thenReturn("backup/cloudPath/test-backup-dir/ybdb_releases/2.0.0.0/release.tar.gz");
    objects.add(version2);

    S3Object version3 = mock(S3Object.class);
    when(version3.key())
        .thenReturn("backup/test-backup-dir/ybdb_releases/3.0.0.0-b2/release.tar.gz");
    objects.add(version3);

    S3Object version4 = mock(S3Object.class);
    when(version4.key()).thenReturn("backup/test-backup-dir/4.0.0.0-b2/release.tar.gz");
    objects.add(version4);

    // Add objects without release versions
    S3Object noVersion1 = mock(S3Object.class);
    when(noVersion1.key()).thenReturn("backup/cloudPath/test-backup-dir/file.txt");
    objects.add(noVersion1);

    when(mockResult.contents()).thenReturn(objects);
    when(mockResult.isTruncated()).thenReturn(false);
    when(mockResult.keyCount()).thenReturn(objects.size());
    when(mockS3Client.listObjectsV2(any(ListObjectsV2Request.class))).thenReturn(mockResult);

    // Test successful retrieval of release versions
    Set<String> releaseVersions = mockAWSUtil.getRemoteReleaseVersions(s3Data, "test-backup-dir");

    // Verify we got the correct release versions
    assertEquals(2, releaseVersions.size());
    assertTrue(releaseVersions.contains("1.0.0.0-b1"));
    assertTrue(releaseVersions.contains("2.0.0.0"));
  }

  @Test
  public void testDownloadRemoteReleases() throws Exception {
    CustomerConfigStorageS3Data s3Data = new CustomerConfigStorageS3Data();
    s3Data.awsAccessKeyId = "test-key";
    s3Data.awsSecretAccessKey = "test-secret";
    s3Data.backupLocation = "s3://test-bucket/backup/cloudPath";
    s3Data.awsHostBase = "s3.amazonaws.com";

    // Create a temporary directory for releases
    Path tempReleasesPath = Files.createTempDirectory("test-releases");
    tempReleasesPath.toFile().deleteOnExit();

    // Set up test data
    Set<String> releaseVersions = Collections.singleton("2.20.0.0");
    String backupDir = "test-backup-dir";

    // Mock S3 client behavior
    ListObjectsV2Response mockListResult = mock(ListObjectsV2Response.class);
    List<S3Object> mockObjects = new ArrayList<>();

    // Create two mock S3Object objects for different architectures
    S3Object x86Object = mock(S3Object.class);
    when(x86Object.key())
        .thenReturn(
            "backup/cloudPath/test-backup-dir/ybdb_releases/2.20.0.0/yugabyte-2.20.0.0-b1-centos-x86_64.tar.gz");

    S3Object aarch64Object = mock(S3Object.class);
    when(aarch64Object.key())
        .thenReturn(
            "backup/cloudPath/test-backup-dir/ybdb_releases/2.20.0.0/yugabyte-2.20.0.0-b1-centos-aarch64.tar.gz");

    mockObjects.add(x86Object);
    mockObjects.add(aarch64Object);

    when(mockListResult.contents()).thenReturn(mockObjects);
    when(mockListResult.keyCount()).thenReturn(mockObjects.size());
    when(mockListResult.isTruncated()).thenReturn(false);
    when(mockListResult.nextContinuationToken()).thenReturn(null);

    when(mockS3Client.listObjectsV2(any(ListObjectsV2Request.class))).thenReturn(mockListResult);

    // Mock S3 GetObject input stream
    ResponseInputStream<GetObjectResponse> mockInputStream = mock(ResponseInputStream.class);

    // Mock the input stream to simulate file download
    AtomicInteger readCount = new AtomicInteger(0);
    doAnswer(
            invocation -> {
              byte[] buffer = invocation.getArgument(0);
              if (readCount.get() >= mockObjects.size()) { // After two reads, return EOF
                return -1;
              }
              readCount.incrementAndGet();
              Arrays.fill(buffer, (byte) 0);
              return 1024; // Return full buffer size
            })
        .when(mockInputStream)
        .read(any(byte[].class));

    // Mock S3Object and its content
    when(mockS3Client.getObject(any(GetObjectRequest.class))).thenReturn(mockInputStream);

    boolean result =
        mockAWSUtil.downloadRemoteReleases(
            s3Data, releaseVersions, tempReleasesPath.toString(), backupDir);
    assertTrue(result);

    // Verify that both files were downloaded
    Path versionPath = tempReleasesPath.resolve("2.20.0.0");
    assertTrue(Files.exists(versionPath.resolve("yugabyte-2.20.0.0-b1-centos-x86_64.tar.gz")));
    assertTrue(Files.exists(versionPath.resolve("yugabyte-2.20.0.0-b1-centos-aarch64.tar.gz")));

    // Verify S3 client interactions
    verify(mockS3Client)
        .listObjectsV2(
            argThat(
                (ListObjectsV2Request request) ->
                    request.bucket().equals("test-bucket")
                        && request
                            .prefix()
                            .equals("backup/cloudPath/test-backup-dir/ybdb_releases/2.20.0.0")));
    verify(mockS3Client, times(mockObjects.size())).getObject(any(GetObjectRequest.class));

    // Cleanup
    mockInputStream.close();
  }

  @Test
  public void testDownloadYbaBackupOldFailure() throws Exception {
    // Setup test data
    CustomerConfigStorageS3Data s3Data = new CustomerConfigStorageS3Data();
    s3Data.awsAccessKeyId = "test-key";
    s3Data.awsSecretAccessKey = "test-secret";
    s3Data.backupLocation = "s3://test-bucket/backup/cloudPath";
    s3Data.awsHostBase = "s3.amazonaws.com";

    // Create a temporary directory for the backup
    Path tempBackupPath = Files.createTempDirectory("test-backup");
    tempBackupPath.toFile().deleteOnExit();

    // Mock S3 client behavior
    ListObjectsV2Response mockListResult = mock(ListObjectsV2Response.class);
    List<S3Object> mockObjects = new ArrayList<>();

    // Create a mock S3Object for the backup file
    S3Object backupObject = mock(S3Object.class);
    when(backupObject.key())
        .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-26_12-00-00.tgz");
    when(backupObject.lastModified()).thenReturn(Instant.parse("2025-03-26T12:00:00Z"));
    mockObjects.add(backupObject);

    when(mockListResult.contents()).thenReturn(mockObjects);
    when(mockListResult.keyCount()).thenReturn(mockObjects.size());
    when(mockListResult.isTruncated()).thenReturn(false);
    when(mockListResult.nextContinuationToken()).thenReturn(null);

    when(mockS3Client.listObjectsV2(any(ListObjectsV2Request.class))).thenReturn(mockListResult);

    // Test backup download fails because the backup is more than 1 day old
    Exception exception =
        assertThrows(
            "YB Anywhere restore is not allowed when backup file is more than 1 day old, enable"
                + " runtime flag yb.yba_backup.allow_restore_with_old_backup to continue",
            PlatformServiceException.class,
            () -> {
              mockAWSUtil.downloadYbaBackup(s3Data, "test-backup-dir", tempBackupPath);
            });
  }

  @Test
  public void testDownloadYbaBackupNoBackupsFound() throws Exception {
    CustomerConfigStorageS3Data s3Data = new CustomerConfigStorageS3Data();
    s3Data.awsAccessKeyId = "test-key";
    s3Data.awsSecretAccessKey = "test-secret";
    s3Data.backupLocation = "s3://test-bucket/backup/cloudPath";
    s3Data.awsHostBase = "s3.amazonaws.com";

    Path tempBackupPath = Files.createTempDirectory("test-backup");
    tempBackupPath.toFile().deleteOnExit();

    ListObjectsV2Response mockListResult = mock(ListObjectsV2Response.class);
    when(mockListResult.contents()).thenReturn(Collections.emptyList());
    when(mockListResult.keyCount()).thenReturn(0);
    when(mockListResult.isTruncated()).thenReturn(false);
    when(mockListResult.nextContinuationToken()).thenReturn(null);

    when(mockS3Client.listObjectsV2(any(ListObjectsV2Request.class))).thenReturn(mockListResult);

    PlatformServiceException exception =
        assertThrows(
            "Could not find YB Anywhere backup in S3 bucket",
            PlatformServiceException.class,
            () -> mockAWSUtil.downloadYbaBackup(s3Data, "test-backup-dir", tempBackupPath));
  }

  @Test
  public void testDownloadYbaBackupNoMatchingBackup() throws Exception {
    CustomerConfigStorageS3Data s3Data = new CustomerConfigStorageS3Data();
    s3Data.awsAccessKeyId = "test-key";
    s3Data.awsSecretAccessKey = "test-secret";
    s3Data.backupLocation = "s3://test-bucket/backup/cloudPath";
    s3Data.awsHostBase = "s3.amazonaws.com";

    Path tempBackupPath = Files.createTempDirectory("test-backup");
    tempBackupPath.toFile().deleteOnExit();

    ListObjectsV2Response mockListResult = mock(ListObjectsV2Response.class);
    List<S3Object> mockObjects = new ArrayList<>();

    S3Object nonBackupObject = mock(S3Object.class);
    when(nonBackupObject.key()).thenReturn("backup/cloudPath/test-backup-dir/notes.txt");
    when(nonBackupObject.lastModified()).thenReturn(Instant.now());
    mockObjects.add(nonBackupObject);

    when(mockListResult.contents()).thenReturn(mockObjects);
    when(mockListResult.keyCount()).thenReturn(mockObjects.size());
    when(mockListResult.isTruncated()).thenReturn(false);
    when(mockListResult.nextContinuationToken()).thenReturn(null);

    when(mockS3Client.listObjectsV2(any(ListObjectsV2Request.class))).thenReturn(mockListResult);

    PlatformServiceException exception =
        assertThrows(
            "Could not find matching YB Anywhere backup in S3 bucket",
            PlatformServiceException.class,
            () -> mockAWSUtil.downloadYbaBackup(s3Data, "test-backup-dir", tempBackupPath));
  }

  @Test
  public void testDownloadYbaBackup() throws Exception {
    // Setup test data
    CustomerConfigStorageS3Data s3Data = new CustomerConfigStorageS3Data();
    s3Data.awsAccessKeyId = "test-key";
    s3Data.awsSecretAccessKey = "test-secret";
    s3Data.backupLocation = "s3://test-bucket/backup/cloudPath";
    s3Data.awsHostBase = "s3.amazonaws.com";

    // Create a temporary directory for the backup
    Path tempBackupPath = Files.createTempDirectory("test-backup");
    tempBackupPath.toFile().deleteOnExit();

    // Mock S3 client behavior
    ListObjectsV2Response mockListResult = mock(ListObjectsV2Response.class);
    List<S3Object> mockObjects = new ArrayList<>();

    // Create a mock S3Object for the backup file
    S3Object backupObject = mock(S3Object.class);
    when(backupObject.key())
        .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-27_11-00-00.tgz");
    when(backupObject.lastModified()).thenReturn(Instant.now().minus(2, ChronoUnit.HOURS));
    mockObjects.add(backupObject);

    S3Object backupObject2 = mock(S3Object.class);
    when(backupObject2.key())
        .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-27_12-00-00.tgz");
    // Make the backup file less than 1 day old
    when(backupObject2.lastModified()).thenReturn(Instant.now().minus(1, ChronoUnit.HOURS));
    mockObjects.add(backupObject2);

    S3Object backupObject3 = mock(S3Object.class);
    when(backupObject3.key())
        .thenReturn(
            "backup/cloudPath/test-backup-dir/ybdb_releases/2.20.0.0/yugabyte-2.20.0.0-b1-centos-x86_64.tar.gz");
    when(backupObject3.lastModified()).thenReturn(Instant.parse("2025-03-28T12:00:00Z"));
    mockObjects.add(backupObject3);

    when(mockListResult.contents()).thenReturn(mockObjects);
    when(mockListResult.keyCount()).thenReturn(mockObjects.size());
    when(mockListResult.isTruncated()).thenReturn(false);
    when(mockListResult.nextContinuationToken()).thenReturn(null);

    when(mockS3Client.listObjectsV2(any(ListObjectsV2Request.class))).thenReturn(mockListResult);

    // Mock S3 GetObject input stream
    ResponseInputStream<GetObjectResponse> mockInputStream = mock(ResponseInputStream.class);

    // Mock the input stream to simulate file download
    AtomicInteger readCount = new AtomicInteger(0);
    doAnswer(
            invocation -> {
              byte[] buffer = invocation.getArgument(0);
              if (readCount.get() >= 1) { // After two reads, return EOF
                return -1;
              }
              readCount.incrementAndGet();
              Arrays.fill(buffer, (byte) 0);
              return 1024; // Return full buffer size
            })
        .when(mockInputStream)
        .read(any(byte[].class));

    when(mockS3Client.getObject(any(GetObjectRequest.class))).thenReturn(mockInputStream);

    // Test successful download of backup
    File downloadedBackup =
        mockAWSUtil.downloadYbaBackup(s3Data, "test-backup-dir", tempBackupPath);
    assertTrue(downloadedBackup.exists());
    assertEquals("backup_2025-03-27_12-00-00.tgz", downloadedBackup.getName());

    // Verify S3 client interactions
    verify(mockS3Client).listObjectsV2(any(ListObjectsV2Request.class));

    // Clean up
    mockInputStream.close();
  }

  @Test
  public void testCleanupUploadedBackups() throws Exception {
    // Setup test data
    CustomerConfigStorageS3Data s3Data = new CustomerConfigStorageS3Data();
    s3Data.awsAccessKeyId = "test-key";
    s3Data.awsSecretAccessKey = "test-secret";
    s3Data.backupLocation = "s3://test-bucket/backup/cloudPath";
    s3Data.awsHostBase = "s3.amazonaws.com";

    // Create a temporary directory for the backup
    Path tempBackupPath = Files.createTempDirectory("test-backup");
    tempBackupPath.toFile().deleteOnExit();

    // Mock S3 client behavior
    ListObjectsV2Response mockListResult = mock(ListObjectsV2Response.class);
    List<S3Object> mockObjects = new ArrayList<>();

    // Create a mock S3Object for the backup file
    S3Object backupObject = mock(S3Object.class);
    when(backupObject.key())
        .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-26_12-00-00.tgz");
    when(backupObject.lastModified()).thenReturn(Instant.parse("2025-03-26T12:00:00Z"));
    mockObjects.add(backupObject);

    S3Object backupObject2 = mock(S3Object.class);
    when(backupObject2.key())
        .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-27_12-00-00.tgz");
    when(backupObject2.lastModified()).thenReturn(Instant.parse("2025-03-27T12:00:00Z"));
    mockObjects.add(backupObject2);

    S3Object backupObject3 = mock(S3Object.class);
    when(backupObject3.key())
        .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-28_12-00-00.tgz");
    when(backupObject3.lastModified()).thenReturn(Instant.parse("2025-03-28T12:00:00Z"));
    mockObjects.add(backupObject3);

    when(mockListResult.contents()).thenReturn(mockObjects);
    when(mockListResult.keyCount()).thenReturn(mockObjects.size());
    when(mockListResult.isTruncated()).thenReturn(false);
    when(mockS3Client.listObjectsV2(any(ListObjectsV2Request.class))).thenReturn(mockListResult);

    // Test successful cleanup of uploaded backups
    boolean result = mockAWSUtil.cleanupUploadedBackups(s3Data, "test-backup-dir");
    assertTrue(result);

    // Verify S3 client interactions
    verify(mockS3Client).listObjectsV2(any(ListObjectsV2Request.class));
    verify(mockS3Client)
        .deleteObjects(
            argThat(
                (DeleteObjectsRequest request) -> {
                  List<ObjectIdentifier> keys = request.delete().objects();
                  assertEquals(1, keys.size()); // Should only delete one backup
                  return keys.get(0)
                      .key()
                      .equals("backup/cloudPath/test-backup-dir/backup_2025-03-26_12-00-00.tgz");
                }));
  }
}
