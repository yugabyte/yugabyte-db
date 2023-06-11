package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doCallRealMethod;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.ybc.CloudStoreSpec;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class StorageUtilTest extends FakeDBApplication {

  Customer testCustomer;
  CustomerConfig s3ConfigWithSlash, s3ConfigWithoutSlash, nfsConfigWithoutSlash, nfsConfigWithSlash;
  JsonNode s3FormDataWithSlash =
      Json.parse(
          "{\"configName\": \""
              + "test-S3_1"
              + "\", \"name\": \"S3\","
              + " \"type\": \"STORAGE\", \"data\": {\"BACKUP_LOCATION\":"
              + " \"s3://test/\","
              + " \"AWS_ACCESS_KEY_ID\": \"A-KEY\", \"AWS_SECRET_ACCESS_KEY\": \"A-SECRET\"}}");

  JsonNode s3FormDataWithoutSlash =
      Json.parse(
          "{\"configName\": \""
              + "test-S3_2"
              + "\", \"name\": \"S3\","
              + " \"type\": \"STORAGE\", \"data\": {\"BACKUP_LOCATION\":"
              + " \"s3://test\","
              + " \"AWS_ACCESS_KEY_ID\": \"A-KEY\", \"AWS_SECRET_ACCESS_KEY\": \"A-SECRET\"}}");

  JsonNode nfsFormDataNoSlash =
      Json.parse(
          "{\"configName\": \""
              + "test-NFS_1"
              + "\", \"name\": \"NFS\","
              + " \"type\": \"STORAGE\", \"data\": {\"BACKUP_LOCATION\":"
              + " \"/tmp/nfs\"}}");

  JsonNode nfsFormDataWithSlash =
      Json.parse(
          "{\"configName\": \""
              + "test-NFS_2"
              + "\", \"name\": \"NFS\","
              + " \"type\": \"STORAGE\", \"data\": {\"BACKUP_LOCATION\":"
              + " \"//\"}}");

  @Before
  public void setup() {
    testCustomer = ModelFactory.testCustomer();
    when(mockStorageUtilFactory.getStorageUtil(eq("NFS"))).thenReturn(mockNfsUtil);
    when(mockStorageUtilFactory.getStorageUtil(eq("S3"))).thenReturn(mockAWSUtil);
    when(mockStorageUtilFactory.getStorageUtil(eq("AZ"))).thenReturn(mockAZUtil);
    when(mockStorageUtilFactory.getStorageUtil(eq("GCS"))).thenReturn(mockGCPUtil);
    doCallRealMethod().when(mockAWSUtil).checkStoragePrefixValidity(anyString(), anyString());
    when(mockAWSUtil.createRestoreCloudStoreSpec(anyString(), anyString(), any(), anyBoolean()))
        .thenCallRealMethod();
    when(mockAWSUtil.getRegionLocationsMap(any())).thenCallRealMethod();
    when(mockNfsUtil.getRegionLocationsMap(any())).thenCallRealMethod();
    when(mockAWSUtil.createDsmCloudStoreSpec(anyString(), any())).thenCallRealMethod();
    when(mockAWSUtil.getBucketRegion(anyString(), any())).thenReturn("reg-1");
    when(mockAWSUtil.getOrCreateHostBase(any(), anyString(), anyString()))
        .thenReturn("s3.amazonaws.com");
    doCallRealMethod().when(mockGCPUtil).checkStoragePrefixValidity(anyString(), anyString());
    doCallRealMethod().when(mockNfsUtil).checkStoragePrefixValidity(anyString(), anyString());
    when(mockNfsUtil.createRestoreCloudStoreSpec(anyString(), anyString(), any(), anyBoolean()))
        .thenCallRealMethod();
    when(mockNfsUtil.createDsmCloudStoreSpec(anyString(), any())).thenCallRealMethod();
    doCallRealMethod().when(mockAZUtil).checkStoragePrefixValidity(anyString(), anyString());
    s3ConfigWithSlash =
        CustomerConfig.createWithFormData(testCustomer.getUuid(), s3FormDataWithSlash);
    s3ConfigWithoutSlash =
        CustomerConfig.createWithFormData(testCustomer.getUuid(), s3FormDataWithoutSlash);
    nfsConfigWithoutSlash =
        CustomerConfig.createWithFormData(testCustomer.getUuid(), nfsFormDataNoSlash);
    nfsConfigWithSlash =
        CustomerConfig.createWithFormData(testCustomer.getUuid(), nfsFormDataWithSlash);
  }

  @Test(expected = Test.None.class)
  @Parameters(
      value = {
        "NFS, /yugabyte_backup, /yugabyte_backup"
            + "/univ-00000000-0000-0000-0000-000000000000/backup-foo/bar",
        "NFS, /, /yugabyte_backup/univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar",
        "NFS, //, //yugabyte_backup/univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar",
        "NFS, /tmp/nfs/yugabyte_backup, /tmp/nfs/yugabyte_backup"
            + "/univ-00000000-0000-0000-0000-000000000000/backup-foo/bar",
        "NFS, /tmp/nfs/yugabyte_backup, /tmp/nfs/yugabyte_backup/yugabyte_backup"
            + "/univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar",
        "S3, s3://backup, s3://backup/univ-00000000-0000-0000-0000-000000000000/backup-foo/bar",
        "S3, s3://backup/test, s3://backup/univ-00000000-0000-0000-0000-000000000000"
            + "/backup-foo/bar",
        "S3, s3://backup, s3://backup/test/univ-00000000-0000-0000-0000-000000000000"
            + "/backup-foo/bar",
        "AZ, https://x.y.z.next/test, https://x.y.z.next/test"
            + "/univ-00000000-0000-0000-0000-000000000000/backup-foo/bar",
        "GCS, https://storage.googleapis.com/test, gs://test"
            + "/univ-00000000-0000-0000-0000-000000000000/backup-foo/bar",
        "GCS, gs://test, https://storage.googleapis.com/test"
            + "/univ-00000000-0000-0000-0000-000000000000/backup-foo/bar"
      })
  public void testStoragePrefixValidityValid(
      String configType, String configLocation, String backupLocation) {
    mockStorageUtilFactory
        .getStorageUtil(configType)
        .checkStoragePrefixValidity(configLocation, backupLocation);
  }

  @Test(expected = PlatformServiceException.class)
  @Parameters(
      value = {
        "NFS, /yugabyte_backup1, "
            + "/yugabyte_backup/univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar",
        "NFS, /, yugabyte_backup/univ-00000000-0000-0000-0000-000000000000/backup-foo/bar",
        "NFS, //, /yugabyte_backup/univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar",
        "NFS, /tmp/nfs/yugabyte_backup, /tmp/nfs/yugabte_backup"
            + "/univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar",
        "S3, s3://backup-1, s3://backup/univ-00000000-0000-0000-0000-000000000000/backup-foo/bar",
        "S3, s3://backup-1/test, s3://backup/univ-00000000-0000-0000-0000-000000000000"
            + "/backup-foo/bar",
        "S3, s3://backup, s3://backup-1/test/univ-00000000-0000-0000-0000-000000000000"
            + "/backup-foo/bar",
        "AZ, https://x.y.z.first/test, https://x.y.z.next/test"
            + "/univ-00000000-0000-0000-0000-000000000000/backup-foo/bar",
        "AZ, https://x.y.z.first/test, https://x.y.z.first/tes-1"
            + "/univ-00000000-0000-0000-0000-000000000000/backup-foo/bar",
        "GCS, https://storage.googleapis.com/test-1, gs://test"
            + "/univ-00000000-0000-0000-0000-000000000000/backup-foo/bar",
        "GCS, gs://test-1, https://storage.googleapis.com/test"
            + "/univ-00000000-0000-0000-0000-000000000000/backup-foo/bar"
      })
  public void testStoragePrefixValidityInvalid(
      String configType, String configLocation, String backupLocation)
      throws PlatformServiceException {
    mockStorageUtilFactory
        .getStorageUtil(configType)
        .checkStoragePrefixValidity(configLocation, backupLocation);
  }

  private CloudStoreSpec createDsmSpec(
      String cloudType, String storageLocation, CustomerConfigData configData) {
    return mockStorageUtilFactory
        .getStorageUtil(cloudType)
        .createDsmCloudStoreSpec(storageLocation, configData);
  }

  private CloudStoreSpec createRestoreSpec(
      String cloudType, String cloudDir, CustomerConfigData configData) {
    return mockStorageUtilFactory
        .getStorageUtil(cloudType)
        .createRestoreCloudStoreSpec(
            YbcBackupUtil.DEFAULT_REGION_STRING, cloudDir, configData, false);
  }

  @Test
  public void testCreateRestoreCloudStoreSpecS3Dsm() {
    String storageLocation =
        "s3://test/univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar";

    CloudStoreSpec s3SpecSlash =
        createDsmSpec("S3", storageLocation, s3ConfigWithSlash.getDataObject());
    assertEquals("test", s3SpecSlash.getBucket());
    assertEquals(
        "univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar/", s3SpecSlash.getCloudDir());

    CloudStoreSpec s3SpecNoSlash =
        createDsmSpec("S3", storageLocation, s3ConfigWithoutSlash.getDataObject());
    assertEquals("test", s3SpecNoSlash.getBucket());
    assertEquals(
        "univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar/",
        s3SpecNoSlash.getCloudDir());
  }

  @Test
  public void testCreateRestoreCloudStoreSpecS3Restore() {
    String cloudDir = "univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar/";
    CloudStoreSpec s3SpecSlash =
        createRestoreSpec("S3", cloudDir, s3ConfigWithSlash.getDataObject());
    assertEquals("test", s3SpecSlash.getBucket());
    assertEquals(
        "univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar/", s3SpecSlash.getCloudDir());

    CloudStoreSpec s3SpecNoSlash =
        createRestoreSpec("S3", cloudDir, s3ConfigWithoutSlash.getDataObject());
    assertEquals("test", s3SpecNoSlash.getBucket());
    assertEquals(
        "univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar/",
        s3SpecNoSlash.getCloudDir());
  }

  @Test
  public void testCreateRestoreCloudStoreSpecNFSDsmWithoutSlash() {
    String storageLocation =
        "/tmp/nfs/yugabyte_backup/univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar";
    CloudStoreSpec nfsSpec =
        createDsmSpec("NFS", storageLocation, nfsConfigWithoutSlash.getDataObject());
    assertEquals("yugabyte_backup", nfsSpec.getBucket());
    assertEquals("/tmp/nfs", nfsSpec.getCredsMap().get("YBC_NFS_DIR"));
    assertEquals(
        "univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar/", nfsSpec.getCloudDir());
  }

  @Test
  public void testCreateRestoreCloudStoreSpecNFSDsmWithSlash() {
    String storageLocation =
        "//yugabyte_backup/univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar";
    CloudStoreSpec nfsSpec =
        createDsmSpec("NFS", storageLocation, nfsConfigWithSlash.getDataObject());
    assertEquals("yugabyte_backup", nfsSpec.getBucket());
    assertEquals("//", nfsSpec.getCredsMap().get("YBC_NFS_DIR"));
    assertEquals(
        "univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar/", nfsSpec.getCloudDir());
  }

  @Test
  public void testCreateRestoreCloudStoreSpecNFSRestoreWithoutSlash() {
    String cloudDir = "univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar/";
    CloudStoreSpec nfsSpec =
        createRestoreSpec("NFS", cloudDir, nfsConfigWithoutSlash.getDataObject());
    assertEquals("yugabyte_backup", nfsSpec.getBucket());
    assertEquals("/tmp/nfs", nfsSpec.getCredsMap().get("YBC_NFS_DIR"));
    assertEquals(
        "univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar/", nfsSpec.getCloudDir());
  }

  @Test
  public void testCreateRestoreCloudStoreSpecNFSRestoreWithSlash() {
    String cloudDir = "univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar/";
    CloudStoreSpec nfsSpec = createRestoreSpec("NFS", cloudDir, nfsConfigWithSlash.getDataObject());
    assertEquals("yugabyte_backup", nfsSpec.getBucket());
    assertEquals("//", nfsSpec.getCredsMap().get("YBC_NFS_DIR"));
    assertEquals(
        "univ-00000000-0000-0000-0000-000000000000/ybc_backup-foo/bar/", nfsSpec.getCloudDir());
  }
}
