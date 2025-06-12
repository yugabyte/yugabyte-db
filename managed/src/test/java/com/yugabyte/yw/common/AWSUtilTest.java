// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
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

import com.amazonaws.services.s3.AmazonS3;
import com.amazonaws.services.s3.model.AmazonS3Exception;
import com.amazonaws.services.s3.model.DeleteObjectsRequest.KeyVersion;
import com.amazonaws.services.s3.model.GetObjectRequest;
import com.amazonaws.services.s3.model.ListObjectsV2Request;
import com.amazonaws.services.s3.model.ListObjectsV2Result;
import com.amazonaws.services.s3.model.PutObjectRequest;
import com.amazonaws.services.s3.model.PutObjectResult;
import com.amazonaws.services.s3.model.S3Object;
import com.amazonaws.services.s3.model.S3ObjectInputStream;
import com.amazonaws.services.s3.model.S3ObjectSummary;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
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

@RunWith(JUnitParamsRunner.class)
@Slf4j
public class AWSUtilTest extends FakeDBApplication {

  AmazonS3 mockS3Client = mock(AmazonS3.class);
  @Mock RuntimeConfGetter mockConfGetter;
  @Spy @InjectMocks AWSUtil mockAWSUtil;

  @Before
  public void setup() {
    MockitoAnnotations.openMocks(this);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.numCloudYbaBackupsRetention)))
        .thenReturn(2);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.enforceCertVerificationBackupRestore)))
        .thenReturn(true);
    doReturn(mockS3Client).when(mockAWSUtil).createS3Client(any());
    PutObjectResult mockResult = new PutObjectResult();
    when(mockS3Client.putObject(any(PutObjectRequest.class))).thenReturn(mockResult);
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
                request ->
                    request.getBucketName().equals("test-bucket")
                        && request.getKey().equals("backup/test-backup-dir/" + backupFile.getName())
                        && request.getFile().equals(backupFile)));

    // Verify marker file was uploaded with exact arguments
    verify(mockS3Client)
        .putObject(
            argThat(
                request ->
                    request.getBucketName().equals("test-bucket")
                        && request.getKey().equals("backup/test-backup-dir/.yba_backup_marker")
                        && request.getFile() != null));
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
                request ->
                    request.getBucketName().equals("test-bucket")
                        && request.getKey().equals("test-backup-dir/" + backupFile.getName())
                        && request.getFile().equals(backupFile)));

    // Verify marker file was uploaded without backup dir in path
    verify(mockS3Client)
        .putObject(
            argThat(
                request ->
                    request.getBucketName().equals("test-bucket")
                        && request.getKey().equals("test-backup-dir/.yba_backup_marker")
                        && request.getFile() != null));
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
    doThrow(new AmazonS3Exception("Upload failed"))
        .when(mockS3Client)
        .putObject(any(PutObjectRequest.class));

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

    // Create mock ListObjectsV2Result with various scenarios
    ListObjectsV2Result mockResult = mock(ListObjectsV2Result.class);
    List<S3ObjectSummary> objectSummaries = new ArrayList<>();

    // Add objects with backup markers at different depths
    S3ObjectSummary marker1 = mock(S3ObjectSummary.class);
    when(marker1.getKey()).thenReturn("backup/dir1/.yba_backup_marker");
    objectSummaries.add(marker1);

    S3ObjectSummary marker2 = mock(S3ObjectSummary.class);
    when(marker2.getKey()).thenReturn("backup/dir2/.yba_backup_marker");
    objectSummaries.add(marker2);

    // Add objects without backup markers
    S3ObjectSummary noMarker1 = mock(S3ObjectSummary.class);
    when(noMarker1.getKey()).thenReturn("backup/dir3/file.txt");
    objectSummaries.add(noMarker1);

    S3ObjectSummary noMarker2 = mock(S3ObjectSummary.class);
    when(noMarker2.getKey()).thenReturn("backup/dir4/subdir/file.txt");
    objectSummaries.add(noMarker2);

    S3ObjectSummary noMarker3 = mock(S3ObjectSummary.class);
    when(noMarker3.getKey()).thenReturn("backup/dir5/subdir/.yba_backup_marker");
    objectSummaries.add(noMarker3);

    when(mockResult.getObjectSummaries()).thenReturn(objectSummaries);
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

    // Mock S3 client to throw AmazonS3Exception
    doThrow(new AmazonS3Exception("S3 error"))
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
                request ->
                    request.getBucketName().equals("test-bucket")
                        && request
                            .getKey()
                            .equals(
                                "backup/cloudPath/test-backup-dir/ybdb_releases/1.0.0/"
                                    + releaseFile.getName())
                        && request.getFile().equals(releaseFile)));
  }

  @Test
  public void testGetRemoteReleaseVersions() throws Exception {
    // Setup test data
    CustomerConfigStorageS3Data s3Data = new CustomerConfigStorageS3Data();
    s3Data.awsAccessKeyId = "test-key";
    s3Data.awsSecretAccessKey = "test-secret";
    s3Data.backupLocation = "s3://test-bucket/backup/cloudPath";
    s3Data.awsHostBase = "s3.amazonaws.com";

    // Create mock ListObjectsV2Result with various scenarios
    ListObjectsV2Result mockResult = mock(ListObjectsV2Result.class);
    List<S3ObjectSummary> objectSummaries = new ArrayList<>();

    // Add objects with release versions
    S3ObjectSummary version1 = mock(S3ObjectSummary.class);
    when(version1.getKey())
        .thenReturn("backup/cloudPath/test-backup-dir/ybdb_releases/1.0.0.0-b1/release.tar.gz");
    objectSummaries.add(version1);

    S3ObjectSummary version2 = mock(S3ObjectSummary.class);
    when(version2.getKey())
        .thenReturn("backup/cloudPath/test-backup-dir/ybdb_releases/2.0.0.0/release.tar.gz");
    objectSummaries.add(version2);

    S3ObjectSummary version3 = mock(S3ObjectSummary.class);
    when(version3.getKey())
        .thenReturn("backup/test-backup-dir/ybdb_releases/3.0.0.0-b2/release.tar.gz");
    objectSummaries.add(version3);

    S3ObjectSummary version4 = mock(S3ObjectSummary.class);
    when(version4.getKey()).thenReturn("backup/test-backup-dir/4.0.0.0-b2/release.tar.gz");
    objectSummaries.add(version4);

    // Add objects without release versions
    S3ObjectSummary noVersion1 = mock(S3ObjectSummary.class);
    when(noVersion1.getKey()).thenReturn("backup/cloudPath/test-backup-dir/file.txt");
    objectSummaries.add(noVersion1);

    when(mockResult.getObjectSummaries()).thenReturn(objectSummaries);
    when(mockResult.isTruncated()).thenReturn(false);
    when(mockResult.getKeyCount()).thenReturn(objectSummaries.size());
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
    ListObjectsV2Result mockListResult = mock(ListObjectsV2Result.class);
    List<S3ObjectSummary> mockSummaries = new ArrayList<>();

    // Create two mock S3ObjectSummary objects for different architectures
    S3ObjectSummary x86Summary = mock(S3ObjectSummary.class);
    when(x86Summary.getKey())
        .thenReturn(
            "backup/cloudPath/test-backup-dir/ybdb_releases/2.20.0.0/yugabyte-2.20.0.0-b1-centos-x86_64.tar.gz");

    S3ObjectSummary aarch64Summary = mock(S3ObjectSummary.class);
    when(aarch64Summary.getKey())
        .thenReturn(
            "backup/cloudPath/test-backup-dir/ybdb_releases/2.20.0.0/yugabyte-2.20.0.0-b1-centos-aarch64.tar.gz");

    mockSummaries.add(x86Summary);
    mockSummaries.add(aarch64Summary);

    when(mockListResult.getObjectSummaries()).thenReturn(mockSummaries);
    when(mockListResult.getKeyCount()).thenReturn(mockSummaries.size());
    when(mockListResult.isTruncated()).thenReturn(false);
    when(mockListResult.getNextContinuationToken()).thenReturn(null);

    when(mockS3Client.listObjectsV2(any(ListObjectsV2Request.class))).thenReturn(mockListResult);

    // Mock S3Object and its content
    S3Object mockS3Object = mock(S3Object.class);
    S3ObjectInputStream mockInputStream = mock(S3ObjectInputStream.class);
    when(mockS3Object.getObjectContent()).thenReturn(mockInputStream);
    when(mockS3Client.getObject(any(GetObjectRequest.class))).thenReturn(mockS3Object);

    // Mock the input stream to simulate file download
    AtomicInteger readCount = new AtomicInteger(0);
    doAnswer(
            invocation -> {
              byte[] buffer = invocation.getArgument(0);
              if (readCount.get() >= mockSummaries.size()) { // After two reads, return EOF
                return -1;
              }
              readCount.incrementAndGet();
              Arrays.fill(buffer, (byte) 0);
              return 1024; // Return full buffer size
            })
        .when(mockInputStream)
        .read(any(byte[].class));

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
                    request.getBucketName().equals("test-bucket")
                        && request
                            .getPrefix()
                            .equals("backup/cloudPath/test-backup-dir/ybdb_releases/2.20.0.0")));
    verify(mockS3Client, times(mockSummaries.size())).getObject(any(GetObjectRequest.class));

    // Clean up
    mockInputStream.close();
    mockS3Object.close();
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
    ListObjectsV2Result mockListResult = mock(ListObjectsV2Result.class);
    List<S3ObjectSummary> mockSummaries = new ArrayList<>();

    // Create a mock S3ObjectSummary for the backup file
    S3ObjectSummary backupSummary = mock(S3ObjectSummary.class);
    when(backupSummary.getKey())
        .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-26_12-00-00.tgz");
    when(backupSummary.getLastModified()).thenReturn(new Date(2025, 3, 26, 12, 0, 0));
    mockSummaries.add(backupSummary);

    S3ObjectSummary backupSummary2 = mock(S3ObjectSummary.class);
    when(backupSummary2.getKey())
        .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-27_12-00-00.tgz");
    when(backupSummary2.getLastModified()).thenReturn(new Date(2025, 3, 27, 12, 0, 0));
    mockSummaries.add(backupSummary2);

    S3ObjectSummary backupSummary3 = mock(S3ObjectSummary.class);
    when(backupSummary3.getKey())
        .thenReturn(
            "backup/cloudPath/test-backup-dir/ybdb_releases/2.20.0.0/yugabyte-2.20.0.0-b1-centos-x86_64.tar.gz");
    when(backupSummary3.getLastModified()).thenReturn(new Date(2025, 3, 28, 12, 0, 0));
    mockSummaries.add(backupSummary3);

    when(mockListResult.getObjectSummaries()).thenReturn(mockSummaries);
    when(mockListResult.getKeyCount()).thenReturn(mockSummaries.size());
    when(mockListResult.isTruncated()).thenReturn(false);
    when(mockListResult.getNextContinuationToken()).thenReturn(null);

    when(mockS3Client.listObjectsV2(any(ListObjectsV2Request.class))).thenReturn(mockListResult);

    // Mock the input stream to simulate file download
    S3Object mockS3Object = mock(S3Object.class);
    S3ObjectInputStream mockInputStream = mock(S3ObjectInputStream.class);
    when(mockS3Object.getObjectContent()).thenReturn(mockInputStream);
    when(mockS3Client.getObject(any(GetObjectRequest.class))).thenReturn(mockS3Object);
    AtomicInteger readCount = new AtomicInteger(0);
    doAnswer(
            invocation -> {
              byte[] buffer = invocation.getArgument(0);
              if (readCount.get() >= 1) { // After read, return EOF
                return -1;
              }
              readCount.incrementAndGet();
              Arrays.fill(buffer, (byte) 0);
              return 1024; // Return full buffer size
            })
        .when(mockInputStream)
        .read(any(byte[].class));

    // Test successful download of backup
    File downloadedBackup =
        mockAWSUtil.downloadYbaBackup(s3Data, "test-backup-dir", tempBackupPath);
    assertTrue(downloadedBackup.exists());
    assertEquals("backup_2025-03-27_12-00-00.tgz", downloadedBackup.getName());

    // Verify S3 client interactions
    verify(mockS3Client).listObjectsV2(any(ListObjectsV2Request.class));

    // Clean up
    mockInputStream.close();
    mockS3Object.close();
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
    ListObjectsV2Result mockListResult = mock(ListObjectsV2Result.class);
    List<S3ObjectSummary> mockSummaries = new ArrayList<>();

    // Create a mock S3ObjectSummary for the backup file
    S3ObjectSummary backupSummary = mock(S3ObjectSummary.class);
    when(backupSummary.getKey())
        .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-26_12-00-00.tgz");
    when(backupSummary.getLastModified()).thenReturn(new Date(2025, 3, 26, 12, 0, 0));
    mockSummaries.add(backupSummary);

    S3ObjectSummary backupSummary2 = mock(S3ObjectSummary.class);
    when(backupSummary2.getKey())
        .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-27_12-00-00.tgz");
    when(backupSummary2.getLastModified()).thenReturn(new Date(2025, 3, 27, 12, 0, 0));
    mockSummaries.add(backupSummary2);

    S3ObjectSummary backupSummary3 = mock(S3ObjectSummary.class);
    when(backupSummary3.getKey())
        .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-28_12-00-00.tgz");
    when(backupSummary3.getLastModified()).thenReturn(new Date(2025, 3, 28, 12, 0, 0));
    mockSummaries.add(backupSummary3);

    when(mockListResult.getObjectSummaries()).thenReturn(mockSummaries);
    when(mockListResult.getKeyCount()).thenReturn(mockSummaries.size());
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
                request -> {
                  List<KeyVersion> keys = request.getKeys();
                  assertEquals(1, keys.size()); // Should only delete one backup
                  return keys.get(0)
                      .getKey()
                      .equals("backup/cloudPath/test-backup-dir/backup_2025-03-26_12-00-00.tgz");
                }));
  }
}
