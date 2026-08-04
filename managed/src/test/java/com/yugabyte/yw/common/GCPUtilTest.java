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
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.mockStatic;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.api.gax.paging.Page;
import com.google.cloud.storage.Blob;
import com.google.cloud.storage.BlobId;
import com.google.cloud.storage.BlobInfo;
import com.google.cloud.storage.Storage;
import com.google.cloud.storage.StorageException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import java.io.File;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Set;
import junitparams.JUnitParamsRunner;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.MockedStatic;
import org.mockito.MockitoAnnotations;
import org.mockito.Spy;

@RunWith(JUnitParamsRunner.class)
@Slf4j
public class GCPUtilTest extends FakeDBApplication {

  Storage mockStorageClient = mock(Storage.class);
  @Mock RuntimeConfGetter mockConfGetter;
  @Spy @InjectMocks GCPUtil mockGCPUtil;

  @Before
  public void setup() throws Exception {
    MockitoAnnotations.openMocks(this);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.numCloudYbaBackupsRetention)))
        .thenReturn(2);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.allowYbaRestoreWithOldBackup)))
        .thenReturn(false);
    Blob mockBlob = mock(Blob.class);
    when(mockStorageClient.create(any(BlobInfo.class), any(InputStream.class)))
        .thenReturn(mockBlob);
  }

  @Test
  public void testUploadYbaBackupWithCloudPath() throws Exception {
    try (MockedStatic<GCPUtil> mockedStaticGCPUtil = mockStatic(GCPUtil.class)) {
      // Setup static method calls we want to be real
      mockedStaticGCPUtil
          .when(() -> GCPUtil.getStorageService(any(CustomerConfigStorageGCSData.class)))
          .thenReturn(mockStorageClient);
      mockedStaticGCPUtil.when(() -> GCPUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Setup test data
      CustomerConfigStorageGCSData gcsData = new CustomerConfigStorageGCSData();
      gcsData.gcsCredentialsJson = "{}";
      gcsData.backupLocation = "gs://test-bucket/backup";
      gcsData.useGcpIam = true;

      // Create a temporary test file
      File backupFile = File.createTempFile("test-backup", ".tgz");
      backupFile.deleteOnExit();

      // Test successful upload
      boolean result = mockGCPUtil.uploadYbaBackup(gcsData, backupFile, "test-backup-dir");
      assertTrue(result);

      // Verify backup file was uploaded with exact arguments
      verify(mockStorageClient)
          .create(
              argThat(
                  (BlobInfo blobInfo) ->
                      blobInfo.getBucket().equals("test-bucket")
                          && blobInfo
                              .getName()
                              .equals("backup/test-backup-dir/" + backupFile.getName())),
              any(InputStream.class));

      // Verify marker file was uploaded with exact arguments
      verify(mockStorageClient)
          .create(
              argThat(
                  (BlobInfo blobInfo) ->
                      blobInfo.getBucket().equals("test-bucket")
                          && blobInfo
                              .getName()
                              .equals("backup/test-backup-dir/.yba_backup_marker")),
              any(InputStream.class));
    }
  }

  @Test
  public void testUploadYbaBackupWithEmptyCloudPath() throws Exception {
    try (MockedStatic<GCPUtil> mockedStaticGCPUtil = mockStatic(GCPUtil.class)) {
      mockedStaticGCPUtil
          .when(() -> GCPUtil.getStorageService(any(CustomerConfigStorageGCSData.class)))
          .thenReturn(mockStorageClient);
      mockedStaticGCPUtil.when(() -> GCPUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Setup test data
      CustomerConfigStorageGCSData gcsData = new CustomerConfigStorageGCSData();
      gcsData.gcsCredentialsJson = "{}";
      gcsData.backupLocation = "gs://test-bucket";

      // Create a temporary test file
      File backupFile = File.createTempFile("test-backup", ".tgz");
      backupFile.deleteOnExit();

      // Test upload with empty backup dir
      boolean result = mockGCPUtil.uploadYbaBackup(gcsData, backupFile, "test-backup-dir");
      assertTrue(result);

      // Verify backup file was uploaded without backup dir in path
      verify(mockStorageClient)
          .create(
              argThat(
                  (BlobInfo blobInfo) ->
                      blobInfo.getBucket().equals("test-bucket")
                          && blobInfo.getName().equals("test-backup-dir/" + backupFile.getName())),
              any(InputStream.class));

      // Verify marker file was uploaded without backup dir in path
      verify(mockStorageClient)
          .create(
              argThat(
                  (BlobInfo blobInfo) ->
                      blobInfo.getBucket().equals("test-bucket")
                          && blobInfo.getName().equals("test-backup-dir/.yba_backup_marker")),
              any(InputStream.class));
    }
  }

  @Test
  public void testUploadYbaBackupFailure() throws Exception {
    try (MockedStatic<GCPUtil> mockedStaticGCPUtil = mockStatic(GCPUtil.class)) {
      mockedStaticGCPUtil
          .when(() -> GCPUtil.getStorageService(any(CustomerConfigStorageGCSData.class)))
          .thenReturn(mockStorageClient);
      mockedStaticGCPUtil.when(() -> GCPUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Setup test data
      CustomerConfigStorageGCSData gcsData = new CustomerConfigStorageGCSData();
      gcsData.gcsCredentialsJson = "{}";
      gcsData.backupLocation = "gs://test-bucket/backup";

      // Create a temporary test file
      File backupFile = File.createTempFile("test-backup", ".tgz");
      backupFile.deleteOnExit();

      // Mock GCS client to throw exception
      doThrow(new StorageException(500, "Upload failed"))
          .when(mockStorageClient)
          .create(any(BlobInfo.class), any(InputStream.class));

      // Test upload failure
      boolean result = mockGCPUtil.uploadYbaBackup(gcsData, backupFile, "test-backup-dir");
      assertFalse(result);
    }
  }

  @Test
  public void testGetYbaBackupDirs() throws Exception {
    try (MockedStatic<GCPUtil> mockedStaticGCPUtil = mockStatic(GCPUtil.class)) {
      mockedStaticGCPUtil
          .when(() -> GCPUtil.getStorageService(any(CustomerConfigStorageGCSData.class)))
          .thenReturn(mockStorageClient);
      mockedStaticGCPUtil.when(() -> GCPUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Setup test data
      CustomerConfigStorageGCSData gcsData = new CustomerConfigStorageGCSData();
      gcsData.gcsCredentialsJson = "{}";
      gcsData.backupLocation = "gs://test-bucket/backup";

      // Create mock blobs with various scenarios
      List<Blob> blobs = new ArrayList<>();

      // Add objects with backup markers at different depths
      Blob marker1 = mock(Blob.class);
      when(marker1.getName()).thenReturn("backup/dir1/.yba_backup_marker");
      blobs.add(marker1);

      Blob marker2 = mock(Blob.class);
      when(marker2.getName()).thenReturn("backup/dir2/.yba_backup_marker");
      blobs.add(marker2);

      // Add objects without backup markers
      Blob noMarker1 = mock(Blob.class);
      when(noMarker1.getName()).thenReturn("backup/dir3/file.txt");
      blobs.add(noMarker1);

      Blob noMarker2 = mock(Blob.class);
      when(noMarker2.getName()).thenReturn("backup/dir4/subdir/file.txt");
      blobs.add(noMarker2);

      Blob noMarker3 = mock(Blob.class);
      when(noMarker3.getName()).thenReturn("backup/dir5/subdir/.yba_backup_marker");
      blobs.add(noMarker3);

      // Create a mock Page
      Page<Blob> mockPage = mock(Page.class);
      when(mockPage.iterateAll()).thenReturn(blobs);
      when(mockPage.getValues()).thenReturn(blobs);
      // Optional: if you need pagination
      when(mockPage.hasNextPage()).thenReturn(false);
      when(mockPage.getNextPage()).thenReturn(null);

      // Use the specific list method for blobs
      when(mockStorageClient.list(any(String.class), any(Storage.BlobListOption.class)))
          .thenReturn(mockPage);

      // Test successful retrieval of backup directories
      List<String> backupDirs = mockGCPUtil.getYbaBackupDirs(gcsData);

      // Verify we got the correct backup directories
      assertEquals(2, backupDirs.size());
      assertTrue(backupDirs.contains("dir1"));
      assertTrue(backupDirs.contains("dir2"));
    }
  }

  @Test
  public void testGetYbaBackupDirsFailure() throws Exception {
    try (MockedStatic<GCPUtil> mockedStaticGCPUtil = mockStatic(GCPUtil.class)) {
      mockedStaticGCPUtil
          .when(() -> GCPUtil.getStorageService(any(CustomerConfigStorageGCSData.class)))
          .thenReturn(mockStorageClient);
      mockedStaticGCPUtil.when(() -> GCPUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Setup test data
      CustomerConfigStorageGCSData gcsData = new CustomerConfigStorageGCSData();
      gcsData.gcsCredentialsJson = "{}";
      gcsData.backupLocation = "gs://test-bucket/backup";

      // Mock GCS client to throw StorageException
      doThrow(new StorageException(500, "GCS error")).when(mockStorageClient).list(any());

      // Test failure scenario
      List<String> backupDirs = mockGCPUtil.getYbaBackupDirs(gcsData);

      // Verify we get an empty list on failure
      assertTrue(backupDirs.isEmpty());
    }
  }

  @Test
  public void testUploadYBDBRelease() throws Exception {
    try (MockedStatic<GCPUtil> mockedStaticGCPUtil = mockStatic(GCPUtil.class)) {
      mockedStaticGCPUtil
          .when(() -> GCPUtil.getStorageService(any(CustomerConfigStorageGCSData.class)))
          .thenReturn(mockStorageClient);
      mockedStaticGCPUtil.when(() -> GCPUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Setup test data
      CustomerConfigStorageGCSData gcsData = new CustomerConfigStorageGCSData();
      gcsData.gcsCredentialsJson = "{}";
      gcsData.backupLocation = "gs://test-bucket/backup/cloudPath";

      // Create a temporary test file
      File releaseFile = File.createTempFile("test-release", ".tgz");
      releaseFile.deleteOnExit();

      // Test successful upload
      boolean result =
          mockGCPUtil.uploadYBDBRelease(gcsData, releaseFile, "test-backup-dir", "1.0.0");
      assertTrue(result);

      // Verify backup file was uploaded with exact arguments
      verify(mockStorageClient)
          .create(
              argThat(
                  (BlobInfo blobInfo) ->
                      blobInfo.getBucket().equals("test-bucket")
                          && blobInfo
                              .getName()
                              .equals(
                                  "backup/cloudPath/test-backup-dir/ybdb_releases/1.0.0/"
                                      + releaseFile.getName())),
              any(InputStream.class));
    }
  }

  @Test
  public void testGetRemoteReleaseVersions() throws Exception {
    try (MockedStatic<GCPUtil> mockedStaticGCPUtil = mockStatic(GCPUtil.class)) {
      mockedStaticGCPUtil
          .when(() -> GCPUtil.getStorageService(any(CustomerConfigStorageGCSData.class)))
          .thenReturn(mockStorageClient);
      mockedStaticGCPUtil.when(() -> GCPUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Setup test data
      CustomerConfigStorageGCSData gcsData = new CustomerConfigStorageGCSData();
      gcsData.gcsCredentialsJson = "{}";
      gcsData.backupLocation = "gs://test-bucket/backup/cloudPath";

      // Create mock blobs with various scenarios
      List<Blob> blobs = new ArrayList<>();

      // Add objects with release versions
      Blob version1 = mock(Blob.class);
      when(version1.getName())
          .thenReturn("backup/cloudPath/test-backup-dir/ybdb_releases/1.0.0.0-b1/release.tar.gz");
      blobs.add(version1);

      Blob version2 = mock(Blob.class);
      when(version2.getName())
          .thenReturn("backup/cloudPath/test-backup-dir/ybdb_releases/2.0.0.0/release.tar.gz");
      blobs.add(version2);

      Blob version3 = mock(Blob.class);
      when(version3.getName())
          .thenReturn("backup/test-backup-dir/ybdb_releases/3.0.0.0-b2/release.tar.gz");
      blobs.add(version3);

      Blob version4 = mock(Blob.class);
      when(version4.getName()).thenReturn("backup/test-backup-dir/4.0.0.0-b2/release.tar.gz");
      blobs.add(version4);

      // Add objects without release versions
      Blob noVersion1 = mock(Blob.class);
      when(noVersion1.getName()).thenReturn("backup/cloudPath/test-backup-dir/file.txt");
      blobs.add(noVersion1);

      // Create a mock Page
      Page<Blob> mockPage = mock(Page.class);
      when(mockPage.iterateAll()).thenReturn(blobs);
      when(mockPage.getValues()).thenReturn(blobs);
      when(mockPage.hasNextPage()).thenReturn(false);
      when(mockPage.getNextPage()).thenReturn(null);

      // Use the mock Page in the storage client mock
      when(mockStorageClient.list(any(String.class), any(Storage.BlobListOption.class)))
          .thenReturn(mockPage);

      // Test successful retrieval of release versions
      Set<String> releaseVersions =
          mockGCPUtil.getRemoteReleaseVersions(gcsData, "test-backup-dir");

      // Verify we got the correct release versions
      assertEquals(2, releaseVersions.size());
      assertTrue(releaseVersions.contains("1.0.0.0-b1"));
      assertTrue(releaseVersions.contains("2.0.0.0"));
    }
  }

  @Test
  public void testDownloadRemoteReleases() throws Exception {
    try (MockedStatic<GCPUtil> mockedStaticGCPUtil = mockStatic(GCPUtil.class)) {
      mockedStaticGCPUtil
          .when(() -> GCPUtil.getStorageService(any(CustomerConfigStorageGCSData.class)))
          .thenReturn(mockStorageClient);
      mockedStaticGCPUtil.when(() -> GCPUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Setup test data
      CustomerConfigStorageGCSData gcsData = new CustomerConfigStorageGCSData();
      gcsData.gcsCredentialsJson = "{}";
      gcsData.backupLocation = "gs://test-bucket/backup/cloudPath";

      // Create a temporary directory for releases
      Path tempReleasesPath = Files.createTempDirectory("test-releases");
      tempReleasesPath.toFile().deleteOnExit();

      // Set up test data
      Set<String> releaseVersions = Collections.singleton("2.20.0.0");
      String backupDir = "test-backup-dir";

      // Create mock blobs for different architectures
      List<Blob> blobs = new ArrayList<>();

      Blob x86Blob = mock(Blob.class);
      when(x86Blob.getName())
          .thenReturn(
              "backup/cloudPath/test-backup-dir/ybdb_releases/2.20.0.0/yugabyte-2.20.0.0-b1-centos-x86_64.tar.gz");
      when(x86Blob.getName())
          .thenReturn(
              "backup/cloudPath/test-backup-dir/ybdb_releases/2.20.0.0/yugabyte-2.20.0.0-b1-centos-x86_64.tar.gz");
      doAnswer(
              invocation -> {
                Path path = invocation.getArgument(0);
                Files.createFile(path);
                return null;
              })
          .when(x86Blob)
          .downloadTo(any(Path.class));

      Blob aarch64Blob = mock(Blob.class);
      when(aarch64Blob.getName())
          .thenReturn(
              "backup/cloudPath/test-backup-dir/ybdb_releases/2.20.0.0/yugabyte-2.20.0.0-b1-centos-aarch64.tar.gz");
      doAnswer(
              invocation -> {
                Path path = invocation.getArgument(0);
                Files.createFile(path);
                return null;
              })
          .when(aarch64Blob)
          .downloadTo(any(Path.class));
      blobs.add(x86Blob);
      blobs.add(aarch64Blob);

      // Create a mock Page
      Page<Blob> mockPage = mock(Page.class);
      when(mockPage.iterateAll()).thenReturn(blobs);
      when(mockPage.getValues()).thenReturn(blobs);
      when(mockPage.hasNextPage()).thenReturn(false);
      when(mockPage.getNextPage()).thenReturn(null);

      // Use the mock Page in the storage client mock
      when(mockStorageClient.list(any(String.class), any(Storage.BlobListOption.class)))
          .thenReturn(mockPage);

      boolean result =
          mockGCPUtil.downloadRemoteReleases(
              gcsData, releaseVersions, tempReleasesPath.toString(), backupDir);
      assertTrue(result);

      // Verify that both files were downloaded
      Path versionPath = tempReleasesPath.resolve("2.20.0.0");
      Path x86Path = versionPath.resolve("yugabyte-2.20.0.0-b1-centos-x86_64.tar.gz");
      Path aarch64Path = versionPath.resolve("yugabyte-2.20.0.0-b1-centos-aarch64.tar.gz");
      assertTrue(Files.exists(x86Path));
      assertTrue(Files.exists(aarch64Path));

      // Verify GCS client interactions
      verify(mockStorageClient)
          .list(
              eq("test-bucket"),
              argThat(
                  (Storage.BlobListOption option) ->
                      option.equals(
                          Storage.BlobListOption.prefix(
                              "backup/cloudPath/test-backup-dir/ybdb_releases/2.20.0.0"))));
      verify(x86Blob).downloadTo(x86Path);
      verify(aarch64Blob).downloadTo(aarch64Path);
    }
  }

  @Test
  public void testDownloadYbaBackupOldFailure() throws Exception {
    try (MockedStatic<GCPUtil> mockedStaticGCPUtil = mockStatic(GCPUtil.class)) {
      mockedStaticGCPUtil
          .when(() -> GCPUtil.getStorageService(any(CustomerConfigStorageGCSData.class)))
          .thenReturn(mockStorageClient);
      mockedStaticGCPUtil.when(() -> GCPUtil.getSplitLocationValue(any())).thenCallRealMethod();

      CustomerConfigStorageGCSData gcsData = new CustomerConfigStorageGCSData();
      gcsData.gcsCredentialsJson = "{}";
      gcsData.backupLocation = "gs://test-bucket/backup/cloudPath";

      Path tempBackupPath = Files.createTempDirectory("test-backup");
      tempBackupPath.toFile().deleteOnExit();

      Blob oldBackupBlob = mock(Blob.class);
      when(oldBackupBlob.getName())
          .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-20_12-00-00.tgz");
      when(oldBackupBlob.getUpdateTimeOffsetDateTime())
          .thenReturn(OffsetDateTime.now().minusDays(2));

      Page<Blob> mockPage = mock(Page.class);
      List<Blob> blobs = Collections.singletonList(oldBackupBlob);
      when(mockPage.iterateAll()).thenReturn(blobs);
      when(mockPage.getValues()).thenReturn(blobs);
      when(mockPage.hasNextPage()).thenReturn(false);
      when(mockPage.getNextPage()).thenReturn(null);

      when(mockStorageClient.list(any(String.class), any(Storage.BlobListOption.class)))
          .thenReturn(mockPage);
      when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.allowYbaRestoreWithOldBackup)))
          .thenReturn(false);

      assertThrows(
          "YB Anywhere restore is not allowed when backup file is more than 1 day old, enable"
              + " runtime flag yb.yba_backup.allow_restore_with_old_backup to continue",
          PlatformServiceException.class,
          () -> mockGCPUtil.downloadYbaBackup(gcsData, "test-backup-dir", tempBackupPath));
    }
  }

  @Test
  public void testDownloadYbaBackupNoBackupsFound() throws Exception {
    try (MockedStatic<GCPUtil> mockedStaticGCPUtil = mockStatic(GCPUtil.class)) {
      mockedStaticGCPUtil
          .when(() -> GCPUtil.getStorageService(any(CustomerConfigStorageGCSData.class)))
          .thenReturn(mockStorageClient);
      mockedStaticGCPUtil.when(() -> GCPUtil.getSplitLocationValue(any())).thenCallRealMethod();

      CustomerConfigStorageGCSData gcsData = new CustomerConfigStorageGCSData();
      gcsData.gcsCredentialsJson = "{}";
      gcsData.backupLocation = "gs://test-bucket/backup/cloudPath";

      Path tempBackupPath = Files.createTempDirectory("test-backup");
      tempBackupPath.toFile().deleteOnExit();

      Page<Blob> mockPage = mock(Page.class);
      List<Blob> emptyList = Collections.emptyList();
      when(mockPage.iterateAll()).thenReturn(emptyList);
      when(mockPage.getValues()).thenReturn(emptyList);
      when(mockPage.hasNextPage()).thenReturn(false);
      when(mockPage.getNextPage()).thenReturn(null);

      when(mockStorageClient.list(any(String.class), any(Storage.BlobListOption.class)))
          .thenReturn(mockPage);

      assertThrows(
          "Could not find YB Anywhere backup in GCS bucket",
          PlatformServiceException.class,
          () -> mockGCPUtil.downloadYbaBackup(gcsData, "test-backup-dir", tempBackupPath));
    }
  }

  @Test
  public void testDownloadYbaBackupNoMatchingBackup() throws Exception {
    try (MockedStatic<GCPUtil> mockedStaticGCPUtil = mockStatic(GCPUtil.class)) {
      mockedStaticGCPUtil
          .when(() -> GCPUtil.getStorageService(any(CustomerConfigStorageGCSData.class)))
          .thenReturn(mockStorageClient);
      mockedStaticGCPUtil.when(() -> GCPUtil.getSplitLocationValue(any())).thenCallRealMethod();
    }
    CustomerConfigStorageGCSData gcsData = new CustomerConfigStorageGCSData();
    gcsData.gcsCredentialsJson = "{}";
    gcsData.backupLocation = "gs://test-bucket/backup/cloudPath";

    Path tempBackupPath = Files.createTempDirectory("test-backup");
    tempBackupPath.toFile().deleteOnExit();

    Blob nonBackupBlob = mock(Blob.class);
    when(nonBackupBlob.getName()).thenReturn("backup/cloudPath/test-backup-dir/readme.txt");

    Page<Blob> mockPage = mock(Page.class);
    when(mockPage.iterateAll()).thenReturn(Collections.singletonList(nonBackupBlob));
    when(mockPage.getValues()).thenReturn(Collections.singletonList(nonBackupBlob));
    when(mockPage.hasNextPage()).thenReturn(false);
    when(mockPage.getNextPage()).thenReturn(null);

    when(mockStorageClient.list(any(String.class), any(Storage.BlobListOption.class)))
        .thenReturn(mockPage);

    assertThrows(
        "Could not find matching YB Anywhere backup in GCS bucket",
        PlatformServiceException.class,
        () -> mockGCPUtil.downloadYbaBackup(gcsData, "test-backup-dir", tempBackupPath));
  }

  @Test
  public void testDownloadYbaBackup() throws Exception {
    try (MockedStatic<GCPUtil> mockedStaticGCPUtil = mockStatic(GCPUtil.class)) {
      mockedStaticGCPUtil
          .when(() -> GCPUtil.getStorageService(any(CustomerConfigStorageGCSData.class)))
          .thenReturn(mockStorageClient);
      mockedStaticGCPUtil.when(() -> GCPUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Setup test data
      CustomerConfigStorageGCSData gcsData = new CustomerConfigStorageGCSData();
      gcsData.gcsCredentialsJson = "{}";
      gcsData.backupLocation = "gs://test-bucket/backup/cloudPath";

      // Create a temporary directory for the backup
      Path tempBackupPath = Files.createTempDirectory("test-backup");
      tempBackupPath.toFile().deleteOnExit();

      // Create mock blobs for backup files
      List<Blob> blobs = new ArrayList<>();

      Blob backupBlob1 = mock(Blob.class);
      when(backupBlob1.getName())
          .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-27_11-00-00.tgz");
      when(backupBlob1.getUpdateTimeOffsetDateTime())
          .thenReturn(OffsetDateTime.now().minus(2, ChronoUnit.HOURS));
      doAnswer(
              invocation -> {
                Path path = invocation.getArgument(0);
                Files.createFile(path);
                return null;
              })
          .when(backupBlob1)
          .downloadTo(any(Path.class));
      blobs.add(backupBlob1);

      Blob backupBlob2 = mock(Blob.class);
      when(backupBlob2.getName())
          .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-27_12-00-00.tgz");
      when(backupBlob2.getUpdateTimeOffsetDateTime())
          .thenReturn(OffsetDateTime.now().minus(1, ChronoUnit.HOURS));
      doAnswer(
              invocation -> {
                Path path = invocation.getArgument(0);
                Files.createFile(path);
                return null;
              })
          .when(backupBlob2)
          .downloadTo(any(Path.class));
      blobs.add(backupBlob2);

      Blob backupBlob3 = mock(Blob.class);
      when(backupBlob3.getName())
          .thenReturn(
              "backup/cloudPath/test-backup-dir/ybdb_releases/2.20.0.0/yugabyte-2.20.0.0-b1-centos-x86_64.tar.gz");
      doAnswer(
              invocation -> {
                Path path = invocation.getArgument(0);
                Files.createFile(path);
                return null;
              })
          .when(backupBlob3)
          .downloadTo(any(Path.class));
      when(backupBlob3.getUpdateTimeOffsetDateTime())
          .thenReturn(OffsetDateTime.of(2025, 3, 28, 12, 0, 0, 0, ZoneOffset.UTC));
      blobs.add(backupBlob3);

      // Create a mock Page
      Page<Blob> mockPage = mock(Page.class);
      when(mockPage.iterateAll()).thenReturn(blobs);
      when(mockPage.getValues()).thenReturn(blobs);
      // Optional: if you need pagination
      when(mockPage.hasNextPage()).thenReturn(false);
      when(mockPage.getNextPage()).thenReturn(null);

      // Use the mock Page in the storage client mock
      when(mockStorageClient.list(any(String.class), any(Storage.BlobListOption.class)))
          .thenReturn(mockPage);

      // Test successful download of backup
      File downloadedBackup =
          mockGCPUtil.downloadYbaBackup(gcsData, "test-backup-dir", tempBackupPath);
      assertTrue(downloadedBackup.exists());
      assertEquals("backup_2025-03-27_12-00-00.tgz", downloadedBackup.getName());

      // Verify GCS client interactions
      verify(mockStorageClient)
          .list(
              eq("test-bucket"),
              argThat(
                  (Storage.BlobListOption option) ->
                      option.equals(
                          Storage.BlobListOption.prefix("backup/cloudPath/test-backup-dir"))));
    }
  }

  @Test
  public void testCleanupUploadedBackups() throws Exception {
    try (MockedStatic<GCPUtil> mockedStaticGCPUtil = mockStatic(GCPUtil.class)) {
      mockedStaticGCPUtil
          .when(() -> GCPUtil.getStorageService(any(CustomerConfigStorageGCSData.class)))
          .thenReturn(mockStorageClient);
      mockedStaticGCPUtil.when(() -> GCPUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Setup test data
      CustomerConfigStorageGCSData gcsData = new CustomerConfigStorageGCSData();
      gcsData.gcsCredentialsJson = "{}";
      gcsData.backupLocation = "gs://test-bucket/backup/cloudPath";

      // Create a temporary directory for the backup
      Path tempBackupPath = Files.createTempDirectory("test-backup");
      tempBackupPath.toFile().deleteOnExit();

      // Create mock blobs for backup files
      List<Blob> blobs = new ArrayList<>();

      Blob backupBlob1 = mock(Blob.class);
      when(backupBlob1.getName())
          .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-26_12-00-00.tgz");
      when(backupBlob1.getUpdateTimeOffsetDateTime())
          .thenReturn(OffsetDateTime.of(2025, 3, 26, 12, 0, 0, 0, ZoneOffset.UTC));
      when(backupBlob1.getBlobId())
          .thenReturn(
              BlobId.of(
                  "test-bucket",
                  "backup/cloudPath/test-backup-dir/backup_2025-03-26_12-00-00.tgz"));
      blobs.add(backupBlob1);

      Blob backupBlob2 = mock(Blob.class);
      when(backupBlob2.getName())
          .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-27_12-00-00.tgz");
      when(backupBlob2.getUpdateTimeOffsetDateTime())
          .thenReturn(OffsetDateTime.of(2025, 3, 27, 12, 0, 0, 0, ZoneOffset.UTC));
      when(backupBlob2.getBlobId())
          .thenReturn(
              BlobId.of(
                  "test-bucket",
                  "backup/cloudPath/test-backup-dir/backup_2025-03-27_12-00-00.tgz"));
      blobs.add(backupBlob2);

      Blob backupBlob3 = mock(Blob.class);
      when(backupBlob3.getName())
          .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-28_12-00-00.tgz");
      when(backupBlob3.getUpdateTimeOffsetDateTime())
          .thenReturn(OffsetDateTime.of(2025, 3, 28, 12, 0, 0, 0, ZoneOffset.UTC));
      when(backupBlob3.getBlobId())
          .thenReturn(
              BlobId.of(
                  "test-bucket",
                  "backup/cloudPath/test-backup-dir/backup_2025-03-28_12-00-00.tgz"));
      blobs.add(backupBlob3);

      // Create a mock Page
      Page<Blob> mockPage = mock(Page.class);
      when(mockPage.iterateAll()).thenReturn(blobs);
      when(mockPage.getValues()).thenReturn(blobs);
      // Optional: if you need pagination
      when(mockPage.hasNextPage()).thenReturn(false);
      when(mockPage.getNextPage()).thenReturn(null);

      // Use the mock Page in the storage client mock
      when(mockStorageClient.list(any(String.class), any(Storage.BlobListOption.class)))
          .thenReturn(mockPage);

      // Test successful cleanup of uploaded backups
      boolean result = mockGCPUtil.cleanupUploadedBackups(gcsData, "test-backup-dir");
      assertTrue(result);

      // Verify GCS client interactions
      verify(mockStorageClient)
          .list(
              eq("test-bucket"),
              argThat(
                  (Storage.BlobListOption option) ->
                      option.equals(
                          Storage.BlobListOption.prefix("backup/cloudPath/test-backup-dir"))));
      verify(mockStorageClient)
          .delete(
              argThat(
                  (BlobId blobId) ->
                      blobId.getBucket().equals("test-bucket")
                          && blobId
                              .getName()
                              .equals(
                                  "backup/cloudPath/test-backup-dir/backup_2025-03-26_12-00-00.tgz")));
    }
  }
}
