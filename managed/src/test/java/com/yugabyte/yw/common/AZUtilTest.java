package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.atLeast;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.inOrder;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.mockStatic;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.azure.core.http.rest.PagedIterable;
import com.azure.core.util.BinaryData;
import com.azure.storage.blob.BlobClient;
import com.azure.storage.blob.BlobContainerClient;
import com.azure.storage.blob.models.BlobItem;
import com.azure.storage.blob.models.BlobItemProperties;
import com.azure.storage.blob.models.BlobProperties;
import com.azure.storage.blob.models.BlobStorageException;
import com.yugabyte.yw.common.AZUtil.CloudLocationInfoAzure;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageAzureData;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InOrder;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.MockedStatic;
import org.mockito.MockitoAnnotations;
import org.mockito.Spy;

@RunWith(JUnitParamsRunner.class)
public class AZUtilTest {

  @Spy @InjectMocks AZUtil mockAZUtil;
  @Mock RuntimeConfGetter mockConfGetter;
  BlobContainerClient mockBlobContainerClient = mock(BlobContainerClient.class);
  BlobClient mockBlobClient = mock(BlobClient.class);

  @Before
  public void setup() {
    MockitoAnnotations.openMocks(this);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.numCloudYbaBackupsRetention)))
        .thenReturn(2);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.allowYbaRestoreWithOldBackup)))
        .thenReturn(false);
    when(mockBlobContainerClient.getBlobClient(any())).thenReturn(mockBlobClient);
  }

  @Test
  public void testUploadYbaBackupWithCloudPath() throws Exception {
    try (MockedStatic<AZUtil> mockedStaticAZUtil = mockStatic(AZUtil.class)) {
      // Setup test data
      CustomerConfigStorageAzureData azData = new CustomerConfigStorageAzureData();
      azData.azureSasToken = "test-token";
      azData.backupLocation = "https://test-account.blob.core.windows.net/test-container/backup";
      when(mockAZUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Verify container name parsing
      mockedStaticAZUtil
          .when(
              () ->
                  AZUtil.createBlobContainerClient(
                      eq("https://test-account.blob.core.windows.net"),
                      any(),
                      eq("test-container")))
          .thenReturn(mockBlobContainerClient);

      // Create a temporary test file
      File backupFile = File.createTempFile("test-backup", ".tgz");
      backupFile.deleteOnExit();

      // Test successful upload
      boolean result = mockAZUtil.uploadYbaBackup(azData, backupFile, "test-backup-dir");
      assertTrue(result);

      // Verify the sequence of operations
      InOrder inOrder = inOrder(mockBlobContainerClient, mockBlobClient);

      // Verify backup file operations
      String expectedBackupBlobName = "backup/test-backup-dir/" + backupFile.getName();
      inOrder.verify(mockBlobContainerClient).getBlobClient(eq(expectedBackupBlobName));
      inOrder.verify(mockBlobClient).upload(any(BinaryData.class));

      // Verify marker file operations
      String expectedMarkerBlobName = "backup/test-backup-dir/.yba_backup_marker";
      inOrder.verify(mockBlobContainerClient).getBlobClient(eq(expectedMarkerBlobName));
      inOrder.verify(mockBlobClient).upload(any(BinaryData.class));
    }
  }

  @Test
  public void testUploadYbaBackupFailure() throws Exception {
    try (MockedStatic<AZUtil> mockedStaticAZUtil = mockStatic(AZUtil.class)) {
      // Setup test data
      CustomerConfigStorageAzureData azData = new CustomerConfigStorageAzureData();
      azData.azureSasToken = "test-token";
      azData.backupLocation = "https://test-account.blob.core.windows.net/test-container/backup";
      when(mockAZUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Create a temporary test file
      File backupFile = File.createTempFile("test-backup", ".tgz");
      backupFile.deleteOnExit();

      // Mock BlobContainerClient and BlobClient to throw exception
      when(mockBlobContainerClient.getBlobClient(any())).thenReturn(mockBlobClient);
      doThrow(new BlobStorageException("Upload failed", null, null))
          .when(mockBlobClient)
          .upload(any(BinaryData.class));

      // Test upload failure
      boolean result = mockAZUtil.uploadYbaBackup(azData, backupFile, "test-backup-dir");
      assertFalse(result);
    }
  }

  @Test
  public void testUploadYbaBackupWithEmptyCloudPath() throws Exception {
    try (MockedStatic<AZUtil> mockedStaticAZUtil = mockStatic(AZUtil.class)) {
      // Setup test data
      CustomerConfigStorageAzureData azData = new CustomerConfigStorageAzureData();
      azData.azureSasToken = "test-token";
      azData.backupLocation = "https://test-account.blob.core.windows.net/test-container";
      when(mockAZUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Verify container name parsing
      mockedStaticAZUtil
          .when(
              () ->
                  AZUtil.createBlobContainerClient(
                      eq("https://test-account.blob.core.windows.net"),
                      any(),
                      eq("test-container")))
          .thenReturn(mockBlobContainerClient);

      // Create a temporary test file
      File backupFile = File.createTempFile("test-backup", ".tgz");
      backupFile.deleteOnExit();

      // Test upload with empty backup dir
      boolean result = mockAZUtil.uploadYbaBackup(azData, backupFile, "test-backup-dir");
      assertTrue(result);

      InOrder inOrder = inOrder(mockBlobContainerClient, mockBlobClient);

      // Verify backup file was uploaded without backup dir in path
      String expectedBackupBlobName = "test-backup-dir/" + backupFile.getName();
      inOrder.verify(mockBlobContainerClient).getBlobClient(eq(expectedBackupBlobName));
      inOrder.verify(mockBlobClient).upload(any(BinaryData.class));

      // Verify marker file was uploaded without backup dir in path
      String expectedMarkerBlobName = "test-backup-dir/.yba_backup_marker";
      inOrder.verify(mockBlobContainerClient).getBlobClient(eq(expectedMarkerBlobName));
      inOrder.verify(mockBlobClient).upload(any(BinaryData.class));
    }
  }

  @Test
  public void testGetYbaBackupDirs() throws Exception {
    try (MockedStatic<AZUtil> mockedStaticAZUtil = mockStatic(AZUtil.class)) {
      // Setup test data
      CustomerConfigStorageAzureData azData = new CustomerConfigStorageAzureData();
      azData.azureSasToken = "test-token";
      azData.backupLocation = "https://test-account.blob.core.windows.net/test-container/backup";
      when(mockAZUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Verify container name parsing
      mockedStaticAZUtil
          .when(
              () ->
                  AZUtil.createBlobContainerClient(
                      eq("https://test-account.blob.core.windows.net"),
                      any(),
                      eq("test-container")))
          .thenReturn(mockBlobContainerClient);

      // Create mock blobs with various scenarios
      List<BlobItem> blobs = new ArrayList<>();

      // Add objects with backup markers at different depths
      BlobItem marker1 = mock(BlobItem.class);
      when(marker1.getName()).thenReturn("backup/dir1/.yba_backup_marker");
      blobs.add(marker1);

      BlobItem marker2 = mock(BlobItem.class);
      when(marker2.getName()).thenReturn("backup/dir2/.yba_backup_marker");
      blobs.add(marker2);

      // Add objects without backup markers
      BlobItem noMarker1 = mock(BlobItem.class);
      when(noMarker1.getName()).thenReturn("backup/dir3/file.txt");
      blobs.add(noMarker1);

      BlobItem noMarker2 = mock(BlobItem.class);
      when(noMarker2.getName()).thenReturn("backup/dir4/subdir/file.txt");
      blobs.add(noMarker2);

      BlobItem noMarker3 = mock(BlobItem.class);
      when(noMarker3.getName()).thenReturn("backup/dir5/subdir/.yba_backup_marker");
      blobs.add(noMarker3);

      // Mock the listBlobs method to return our test blobs
      PagedIterable<BlobItem> mockPagedIterable = mock(PagedIterable.class);
      when(mockPagedIterable.iterator()).thenReturn(blobs.iterator());
      when(mockBlobContainerClient.listBlobs(any(), any())).thenReturn(mockPagedIterable);

      // Test successful retrieval of backup directories
      List<String> backupDirs = mockAZUtil.getYbaBackupDirs(azData);

      // Verify we got the correct backup directories
      assertEquals(2, backupDirs.size());
      assertTrue(backupDirs.contains("dir1"));
      assertTrue(backupDirs.contains("dir2"));
    }
  }

  @Test
  public void testGetYbaBackupDirsFailure() throws Exception {
    try (MockedStatic<AZUtil> mockedStaticAZUtil = mockStatic(AZUtil.class)) {
      // Setup test data
      CustomerConfigStorageAzureData azData = new CustomerConfigStorageAzureData();
      azData.azureSasToken = "test-token";
      azData.backupLocation = "https://test-account.blob.core.windows.net/test-container/backup";
      when(mockAZUtil.getSplitLocationValue(any())).thenCallRealMethod();
      mockedStaticAZUtil
          .when(() -> AZUtil.createBlobContainerClient(anyString(), anyString(), anyString()))
          .thenReturn(mockBlobContainerClient);

      // Mock the listBlobs method to throw exception
      doThrow(new BlobStorageException("List blobs failed", null, null))
          .when(mockBlobContainerClient)
          .listBlobs(any(), any());

      // Test failure scenario
      List<String> backupDirs = mockAZUtil.getYbaBackupDirs(azData);

      // Verify we get an empty list on failure
      assertTrue(backupDirs.isEmpty());
    }
  }

  @Test
  public void testUploadYBDBRelease() throws Exception {
    try (MockedStatic<AZUtil> mockedStaticAZUtil = mockStatic(AZUtil.class)) {
      // Setup test data
      CustomerConfigStorageAzureData azData = new CustomerConfigStorageAzureData();
      azData.azureSasToken = "test-token";
      azData.backupLocation =
          "https://test-account.blob.core.windows.net/test-container/backup/cloudPath";
      when(mockAZUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Verify container name parsing
      mockedStaticAZUtil
          .when(
              () ->
                  AZUtil.createBlobContainerClient(
                      eq("https://test-account.blob.core.windows.net"),
                      any(),
                      eq("test-container")))
          .thenReturn(mockBlobContainerClient);

      // Create a temporary test file
      File releaseFile = File.createTempFile("test-release", ".tgz");
      releaseFile.deleteOnExit();

      // Test successful upload
      boolean result =
          mockAZUtil.uploadYBDBRelease(azData, releaseFile, "test-backup-dir", "1.0.0");
      assertTrue(result);

      // Verify backup file uploaded with correct blob name
      String expectedBackupBlobName =
          "backup/cloudPath/test-backup-dir/ybdb_releases/1.0.0/" + releaseFile.getName();
      verify(mockBlobContainerClient).getBlobClient(eq(expectedBackupBlobName));
      verify(mockBlobClient).upload(any(BinaryData.class));
    }
  }

  @Test
  public void testGetRemoteReleaseVersions() throws Exception {
    try (MockedStatic<AZUtil> mockedStaticAZUtil = mockStatic(AZUtil.class)) {
      // Setup test data
      CustomerConfigStorageAzureData azData = new CustomerConfigStorageAzureData();
      azData.azureSasToken = "test-token";
      azData.backupLocation =
          "https://test-account.blob.core.windows.net/test-container/backup/cloudPath";
      when(mockAZUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Verify container name parsing
      mockedStaticAZUtil
          .when(
              () ->
                  AZUtil.createBlobContainerClient(
                      eq("https://test-account.blob.core.windows.net"),
                      any(),
                      eq("test-container")))
          .thenReturn(mockBlobContainerClient);

      // Create mock blobs with various scenarios
      List<BlobItem> blobs = new ArrayList<>();

      // Add objects with release versions
      BlobItem version1 = mock(BlobItem.class);
      when(version1.getName())
          .thenReturn("backup/cloudPath/test-backup-dir/ybdb_releases/1.0.0.0-b1/release.tar.gz");
      blobs.add(version1);

      BlobItem version2 = mock(BlobItem.class);
      when(version2.getName())
          .thenReturn("backup/cloudPath/test-backup-dir/ybdb_releases/2.0.0.0/release.tar.gz");
      blobs.add(version2);

      BlobItem version3 = mock(BlobItem.class);
      when(version3.getName())
          .thenReturn("backup/test-backup-dir/ybdb_releases/3.0.0.0-b2/release.tar.gz");
      blobs.add(version3);

      BlobItem version4 = mock(BlobItem.class);
      when(version4.getName()).thenReturn("backup/test-backup-dir/4.0.0.0-b2/release.tar.gz");

      // Add objects without release versions
      BlobItem noVersion1 = mock(BlobItem.class);
      when(noVersion1.getName()).thenReturn("backup/cloudPath/test-backup-dir/file.txt");
      blobs.add(noVersion1);

      // Create a mock Page
      PagedIterable<BlobItem> mockPage = mock(PagedIterable.class);
      when(mockPage.iterator()).thenReturn(blobs.iterator());
      when(mockBlobContainerClient.listBlobs(any(), any())).thenReturn(mockPage);

      // Test successful retrieval of release versions
      Set<String> releaseVersions = mockAZUtil.getRemoteReleaseVersions(azData, "test-backup-dir");
      assertEquals(2, releaseVersions.size());
      assertTrue(releaseVersions.contains("1.0.0.0-b1"));
      assertTrue(releaseVersions.contains("2.0.0.0"));
    }
  }

  @Test
  public void testDownloadRemoteReleases() throws Exception {
    try (MockedStatic<AZUtil> mockedStaticAZUtil = mockStatic(AZUtil.class)) {
      // Setup test data
      CustomerConfigStorageAzureData azData = new CustomerConfigStorageAzureData();
      azData.azureSasToken = "test-token";
      azData.backupLocation =
          "https://test-account.blob.core.windows.net/test-container/backup/cloudPath";
      when(mockAZUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Verify container name parsing
      mockedStaticAZUtil
          .when(
              () ->
                  AZUtil.createBlobContainerClient(
                      eq("https://test-account.blob.core.windows.net"),
                      any(),
                      eq("test-container")))
          .thenReturn(mockBlobContainerClient);

      // Create a temporary directory for releases
      Path tempReleasesPath = Files.createTempDirectory("test-releases");
      tempReleasesPath.toFile().deleteOnExit();

      Set<String> releaseVersions = Collections.singleton("2.20.0.0");
      String backupDir = "test-backup-dir";

      // Create mock blobs for different architectures
      List<BlobItem> blobs = new ArrayList<>();

      BlobItem x86Blob = mock(BlobItem.class);
      when(x86Blob.getName())
          .thenReturn(
              "backup/cloudPath/test-backup-dir/ybdb_releases/2.20.0.0/yugabyte-2.20.0.0-b1-centos-x86_64.tar.gz");
      blobs.add(x86Blob);

      BlobItem aarch64Blob = mock(BlobItem.class);
      when(aarch64Blob.getName())
          .thenReturn(
              "backup/cloudPath/test-backup-dir/ybdb_releases/2.20.0.0/yugabyte-2.20.0.0-b1-centos-aarch64.tar.gz");
      blobs.add(aarch64Blob);

      // Create a mock Page
      PagedIterable<BlobItem> mockPage = mock(PagedIterable.class);
      when(mockPage.iterator()).thenReturn(blobs.iterator());
      when(mockBlobContainerClient.listBlobs(any(), any())).thenReturn(mockPage);

      // Test successful download of remote releases
      boolean result =
          mockAZUtil.downloadRemoteReleases(
              azData, releaseVersions, tempReleasesPath.toString(), backupDir);
      assertTrue(result);

      InOrder inOrder = inOrder(mockBlobContainerClient, mockBlobClient);

      // Verify download of x86 release
      String x86ReleasePath =
          tempReleasesPath.toString() + "/2.20.0.0/yugabyte-2.20.0.0-b1-centos-x86_64.tar.gz";
      inOrder.verify(mockBlobContainerClient).getBlobClient(eq(x86Blob.getName()));
      inOrder.verify(mockBlobClient).downloadToFile(eq(x86ReleasePath.toString()));

      // Verify download of aarch64 release
      String aarch64ReleasePath =
          tempReleasesPath.toString() + "/2.20.0.0/yugabyte-2.20.0.0-b1-centos-aarch64.tar.gz";
      inOrder.verify(mockBlobContainerClient).getBlobClient(eq(aarch64Blob.getName()));
      inOrder.verify(mockBlobClient).downloadToFile(eq(aarch64ReleasePath.toString()));
    }
  }

  @Test
  public void testDownloadYbaBackupOldFailure() throws Exception {
    try (MockedStatic<AZUtil> mockedStaticAZUtil = mockStatic(AZUtil.class)) {
      // Setup test data
      CustomerConfigStorageAzureData azData = new CustomerConfigStorageAzureData();
      azData.azureSasToken = "test-token";
      azData.backupLocation =
          "https://test-account.blob.core.windows.net/test-container/backup/cloudPath";
      when(mockAZUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Verify container name parsing
      mockedStaticAZUtil
          .when(
              () ->
                  AZUtil.createBlobContainerClient(
                      eq("https://test-account.blob.core.windows.net"),
                      any(),
                      eq("test-container")))
          .thenReturn(mockBlobContainerClient);

      // Create a temporary directory for the backup
      Path tempBackupPath = Files.createTempDirectory("test-backup");
      tempBackupPath.toFile().deleteOnExit();

      // Create mock blobs for backup files
      List<BlobItem> blobs = new ArrayList<>();

      BlobItem backupBlob1 = mock(BlobItem.class);
      when(backupBlob1.getName())
          .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-26_12-00-00.tgz");
      BlobItemProperties mockProperties = mock(BlobItemProperties.class);
      when(backupBlob1.getProperties()).thenReturn(mockProperties);
      when(mockProperties.getLastModified())
          .thenReturn(OffsetDateTime.of(2025, 3, 26, 12, 0, 0, 0, ZoneOffset.UTC));
      blobs.add(backupBlob1);

      // Create a mock Page
      PagedIterable<BlobItem> mockPage = mock(PagedIterable.class);
      when(mockPage.iterator()).thenReturn(blobs.iterator());
      when(mockBlobContainerClient.listBlobs(any(), any())).thenReturn(mockPage);

      assertThrows(
          "YB Anywhere restore is not allowed when backup file is more than 1 day old, enable"
              + " runtime flag yb.yba_backup.allow_restore_with_old_backup to continue",
          PlatformServiceException.class,
          () -> {
            mockAZUtil.downloadYbaBackup(azData, "test-backup-dir", tempBackupPath);
          });
    }
  }

  @Test
  public void testDownloadYbaBackupNoBackupsFound() throws Exception {
    try (MockedStatic<AZUtil> mockedStaticAZUtil = mockStatic(AZUtil.class)) {
      CustomerConfigStorageAzureData azData = new CustomerConfigStorageAzureData();
      azData.azureSasToken = "test-token";
      azData.backupLocation =
          "https://test-account.blob.core.windows.net/test-container/backup/cloudPath";
      when(mockAZUtil.getSplitLocationValue(any())).thenCallRealMethod();

      mockedStaticAZUtil
          .when(
              () ->
                  AZUtil.createBlobContainerClient(
                      eq("https://test-account.blob.core.windows.net"),
                      any(),
                      eq("test-container")))
          .thenReturn(mockBlobContainerClient);

      Path tempBackupPath = Files.createTempDirectory("test-backup");
      tempBackupPath.toFile().deleteOnExit();

      PagedIterable<BlobItem> mockPage = mock(PagedIterable.class);
      when(mockPage.iterator()).thenReturn(Collections.emptyIterator());
      when(mockBlobContainerClient.listBlobs(any(), any())).thenReturn(mockPage);

      assertThrows(
          "Could not find YB Anywhere backup in Azure container",
          PlatformServiceException.class,
          () -> mockAZUtil.downloadYbaBackup(azData, "test-backup-dir", tempBackupPath));
    }
  }

  @Test
  public void testDownloadYbaBackupNoMatchingBackup() throws Exception {
    try (MockedStatic<AZUtil> mockedStaticAZUtil = mockStatic(AZUtil.class)) {
      CustomerConfigStorageAzureData azData = new CustomerConfigStorageAzureData();
      azData.azureSasToken = "test-token";
      azData.backupLocation =
          "https://test-account.blob.core.windows.net/test-container/backup/cloudPath";
      when(mockAZUtil.getSplitLocationValue(any())).thenCallRealMethod();

      mockedStaticAZUtil
          .when(
              () ->
                  AZUtil.createBlobContainerClient(
                      eq("https://test-account.blob.core.windows.net"),
                      any(),
                      eq("test-container")))
          .thenReturn(mockBlobContainerClient);

      Path tempBackupPath = Files.createTempDirectory("test-backup");
      tempBackupPath.toFile().deleteOnExit();

      List<BlobItem> blobs = new ArrayList<>();
      BlobItem nonBackupBlob = mock(BlobItem.class);
      when(nonBackupBlob.getName()).thenReturn("backup/cloudPath/test-backup-dir/readme.txt");
      blobs.add(nonBackupBlob);

      PagedIterable<BlobItem> mockPage = mock(PagedIterable.class);
      when(mockPage.iterator()).thenReturn(blobs.iterator());
      when(mockBlobContainerClient.listBlobs(any(), any())).thenReturn(mockPage);

      assertThrows(
          "Could not find YB Anywhere backup in Azure container",
          PlatformServiceException.class,
          () -> mockAZUtil.downloadYbaBackup(azData, "test-backup-dir", tempBackupPath));
    }
  }

  @Test
  public void testDownloadYbaBackup() throws Exception {
    try (MockedStatic<AZUtil> mockedStaticAZUtil = mockStatic(AZUtil.class)) {
      // Setup test data
      CustomerConfigStorageAzureData azData = new CustomerConfigStorageAzureData();
      azData.azureSasToken = "test-token";
      azData.backupLocation =
          "https://test-account.blob.core.windows.net/test-container/backup/cloudPath";
      when(mockAZUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Verify container name parsing
      mockedStaticAZUtil
          .when(
              () ->
                  AZUtil.createBlobContainerClient(
                      eq("https://test-account.blob.core.windows.net"),
                      any(),
                      eq("test-container")))
          .thenReturn(mockBlobContainerClient);

      // Create a temporary directory for the backup
      Path tempBackupPath = Files.createTempDirectory("test-backup");
      tempBackupPath.toFile().deleteOnExit();

      // Create mock blobs for backup files
      List<BlobItem> blobs = new ArrayList<>();

      BlobItem backupBlob1 = mock(BlobItem.class);
      when(backupBlob1.getName())
          .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-27_11-00-00.tgz");
      BlobItemProperties mockProperties = mock(BlobItemProperties.class);
      when(backupBlob1.getProperties()).thenReturn(mockProperties);
      when(mockProperties.getLastModified())
          .thenReturn(OffsetDateTime.now().minus(2, ChronoUnit.HOURS));
      blobs.add(backupBlob1);

      BlobItem backupBlob2 = mock(BlobItem.class);
      when(backupBlob2.getName())
          .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-27_12-00-00.tgz");
      BlobItemProperties mockProperties2 = mock(BlobItemProperties.class);
      when(backupBlob2.getProperties()).thenReturn(mockProperties2);
      when(mockProperties2.getLastModified())
          .thenReturn(OffsetDateTime.now().minus(1, ChronoUnit.HOURS));
      blobs.add(backupBlob2);

      BlobItem backupBlob3 = mock(BlobItem.class);
      when(backupBlob3.getName())
          .thenReturn(
              "backup/cloudPath/test-backup-dir/ybdb_releases/2.20.0.0/yugabyte-2.20.0.0-b1-centos-x86_64.tar.gz");
      BlobItemProperties mockProperties3 = mock(BlobItemProperties.class);
      when(backupBlob3.getProperties()).thenReturn(mockProperties3);
      when(mockProperties3.getLastModified())
          .thenReturn(OffsetDateTime.of(2025, 3, 28, 12, 0, 0, 0, ZoneOffset.UTC));
      blobs.add(backupBlob3);

      // Create a mock Page
      PagedIterable<BlobItem> mockPage = mock(PagedIterable.class);
      when(mockPage.iterator()).thenReturn(blobs.iterator());
      when(mockBlobContainerClient.listBlobs(any(), any())).thenReturn(mockPage);

      // Test successful download of the most recent backup
      File downloadedBackup =
          mockAZUtil.downloadYbaBackup(azData, "test-backup-dir", tempBackupPath);
      assertNotNull(downloadedBackup);
      verify(mockBlobContainerClient).getBlobClient(eq(backupBlob2.getName()));
      verify(mockBlobClient).downloadToFile(eq(downloadedBackup.getAbsolutePath()));
      assertEquals("backup_2025-03-27_12-00-00.tgz", downloadedBackup.getName());
    }
  }

  @Test
  public void testCleanupUploadedBackups() throws Exception {
    try (MockedStatic<AZUtil> mockedStaticAZUtil = mockStatic(AZUtil.class)) {
      // Setup test data
      CustomerConfigStorageAzureData azData = new CustomerConfigStorageAzureData();
      azData.azureSasToken = "test-token";
      azData.backupLocation =
          "https://test-account.blob.core.windows.net/test-container/backup/cloudPath";
      when(mockAZUtil.getSplitLocationValue(any())).thenCallRealMethod();

      // Verify container name parsing
      mockedStaticAZUtil
          .when(
              () ->
                  AZUtil.createBlobContainerClient(
                      eq("https://test-account.blob.core.windows.net"),
                      any(),
                      eq("test-container")))
          .thenReturn(mockBlobContainerClient);

      // Create mock blobs for backup files
      List<BlobItem> blobs = new ArrayList<>();

      BlobItem backupBlob1 = mock(BlobItem.class);
      when(backupBlob1.getName())
          .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-26_12-00-00.tgz");
      blobs.add(backupBlob1);

      BlobItem backupBlob2 = mock(BlobItem.class);
      when(backupBlob2.getName())
          .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-27_12-00-00.tgz");
      blobs.add(backupBlob2);

      BlobItem backupBlob3 = mock(BlobItem.class);
      when(backupBlob3.getName())
          .thenReturn("backup/cloudPath/test-backup-dir/backup_2025-03-28_12-00-00.tgz");
      blobs.add(backupBlob3);

      // Mock getBlobClient for each blob
      for (BlobItem blob : blobs) {
        when(mockBlobContainerClient.getBlobClient(blob.getName())).thenReturn(mockBlobClient);
      }

      // Mock the properties chain for sorting
      BlobProperties mockBlobProperties = mock(BlobProperties.class);
      when(mockBlobClient.getProperties()).thenReturn(mockBlobProperties);
      when(mockBlobProperties.getLastModified())
          .thenReturn(OffsetDateTime.of(2025, 3, 26, 12, 0, 0, 0, ZoneOffset.UTC)) // for first blob
          .thenReturn(
              OffsetDateTime.of(2025, 3, 27, 12, 0, 0, 0, ZoneOffset.UTC)) // for second blob
          .thenReturn(
              OffsetDateTime.of(2025, 3, 28, 12, 0, 0, 0, ZoneOffset.UTC)); // for third blob

      // Create a mock Page
      PagedIterable<BlobItem> mockPage = mock(PagedIterable.class);
      when(mockPage.spliterator()).thenReturn(blobs.spliterator());
      when(mockBlobContainerClient.listBlobs(any(), any())).thenReturn(mockPage);

      // Test successful cleanup of uploaded backups
      boolean result = mockAZUtil.cleanupUploadedBackups(azData, "test-backup-dir");
      assertTrue(result);

      // Verify the sequence of operations
      InOrder inOrder = inOrder(mockBlobContainerClient, mockBlobClient);

      // Verify all getBlobClient calls in the correct order (sorting then deletion)
      inOrder.verify(mockBlobContainerClient).getBlobClient(eq(backupBlob1.getName()));
      inOrder.verify(mockBlobContainerClient).getBlobClient(eq(backupBlob2.getName()));
      inOrder.verify(mockBlobContainerClient).getBlobClient(eq(backupBlob3.getName()));
      inOrder.verify(mockBlobContainerClient).getBlobClient(eq(backupBlob1.getName()));
      inOrder.verify(mockBlobClient).delete();
    }
  }

  @Test
  public void testBackupLocationWithSubDirectory() throws Exception {
    CustomerConfigStorageAzureData azData = new CustomerConfigStorageAzureData();
    azData.azureSasToken = "test-token";
    azData.backupLocation = "https://test-account.blob.core.windows.net/test-container/sub/dir";
    String azureUrl = "https://test-account.blob.core.windows.net";
    String bucket = "test-container";
    String cloudPath = "sub/dir";
    CloudLocationInfoAzure cLInfo =
        mockAZUtil.new CloudLocationInfoAzure(azureUrl, bucket, cloudPath);
    doReturn(cLInfo).when(mockAZUtil).getCloudLocationInfo(any(), any(), any());
    doReturn(mockBlobContainerClient)
        .when(mockAZUtil)
        .createBlobContainerClient(any(CustomerConfigStorageAzureData.class), anyString());
    UUID randomFile = UUID.randomUUID();
    when(mockAZUtil.getRandomUUID()).thenReturn(randomFile);
    mockAZUtil.validate(azData, new ArrayList<>());
    ArgumentCaptor<String> fileNameCaptor = ArgumentCaptor.forClass(String.class);
    verify(mockBlobContainerClient, atLeast(1)).getBlobClient(fileNameCaptor.capture());
    String expectedFileName = "sub/dir/" + randomFile.toString() + ".txt";
    assertEquals(expectedFileName, fileNameCaptor.getValue());
  }
}
