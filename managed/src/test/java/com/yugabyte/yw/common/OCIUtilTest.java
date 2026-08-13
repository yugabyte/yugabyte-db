// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.yb.ybc.CloudType.OCI;
import static org.yb.ybc.CloudType.S3;

import com.yugabyte.yw.common.OCIUtil.CloudLocationInfoOci;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageOCIData;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.ybc.CloudStoreSpec;

@RunWith(MockitoJUnitRunner.class)
public class OCIUtilTest {

  private OCIUtil ociUtil;
  private CustomerConfigStorageOCIData iamData;
  private CustomerConfigStorageOCIData s3CompatData;

  @Before
  public void setUp() {
    ociUtil = new OCIUtil();

    iamData = new CustomerConfigStorageOCIData();
    iamData.useOciIam = true;
    iamData.ociRegion = "us-sanjose-1";
    iamData.ociNamespace = "test-namespace";
    iamData.backupLocation =
        "https://objectstorage.us-sanjose-1.oraclecloud.com/n/test-namespace/b/test-bucket";

    s3CompatData = new CustomerConfigStorageOCIData();
    s3CompatData.useOciIam = false;
    s3CompatData.ociRegion = "us-sanjose-1";
    s3CompatData.ociS3AccessKeyId = "test-access-key";
    s3CompatData.ociS3SecretAccessKey = "test-secret-key";
    s3CompatData.ociS3HostBase = "test-namespace.compat.objectstorage.us-sanjose-1.oraclecloud.com";
    s3CompatData.backupLocation = "s3://test-bucket/prefix";
  }

  @Test
  public void testCreateCredsMapYbcIam() {
    Map<String, String> creds = ociUtil.createCredsMapYbc(iamData);
    assertEquals("true", creds.get(OCIUtil.YBC_USE_OCI_IAM_FIELDNAME));
    assertEquals("us-sanjose-1", creds.get(OCIUtil.YBC_OCI_REGION_FIELDNAME));
    assertEquals("test-namespace", creds.get(OCIUtil.YBC_OCI_NAMESPACE_FIELDNAME));
    assertEquals(3, creds.size());
  }

  @Test
  public void testCreateCredsMapYbcS3Compat() {
    Map<String, String> creds = ociUtil.createCredsMapYbc(s3CompatData);
    assertEquals("test-access-key", creds.get(OCIUtil.YBC_AWS_ACCESS_KEY_ID_FIELDNAME));
    assertEquals("test-secret-key", creds.get(OCIUtil.YBC_AWS_SECRET_ACCESS_KEY_FIELDNAME));
    assertEquals(
        "test-namespace.compat.objectstorage.us-sanjose-1.oraclecloud.com",
        creds.get(OCIUtil.YBC_AWS_ENDPOINT_FIELDNAME));
    assertEquals("us-sanjose-1", creds.get(OCIUtil.YBC_AWS_DEFAULT_REGION_FIELDNAME));
    assertEquals("true", creds.get(OCIUtil.YBC_PATH_STYLE_ACCESS_FIELDNAME));
    assertEquals(5, creds.size());
  }

  @Test
  public void testGetCloudLocationInfoIam() {
    CloudLocationInfoOci info =
        (CloudLocationInfoOci)
            ociUtil.getCloudLocationInfo(
                YbcBackupUtil.DEFAULT_REGION_STRING, iamData, null /* backupLocation */);
    assertEquals("test-bucket", info.bucket);
    assertEquals("", info.cloudPath);
    assertEquals("test-namespace", info.namespace);
    assertEquals("objectstorage.us-sanjose-1.oraclecloud.com", info.objectStorageHost);
  }

  @Test
  public void testGetSplitLocationValueNativeUrl() {
    String[] split =
        OCIUtil.getSplitLocationValue(
            "https://objectstorage.us-sanjose-1.oraclecloud.com/n/test-namespace/b/test-bucket");
    assertEquals(1, split.length);
    assertEquals("test-bucket", split[0]);
  }

  @Test
  public void testGetSplitLocationValueParUrl() {
    // Optional /p/<token>/ must not shift namespace/bucket indices.
    String[] split =
        OCIUtil.getSplitLocationValue(
            "https://objectstorage.us-phoenix-1.oraclecloud.com/p/abcToken123/n/MyNamespace/b/MyParBucket");
    assertEquals(1, split.length);
    assertEquals("MyParBucket", split[0]);
  }

  @Test
  public void testParseNativeOciUrlDedicatedHostAndPar() {
    OCIUtil.OciNativeUrlParts parts =
        OCIUtil.parseNativeOciUrl(
            "https://myns.objectstorage.us-ashburn-1.oci.customer-oci.com/p/tok/n/myns/b/mybucket");
    assertEquals("myns.objectstorage.us-ashburn-1.oci.customer-oci.com", parts.host);
    assertEquals("myns", parts.namespace);
    assertEquals("mybucket", parts.bucket);
  }

  @Test
  public void testGetCloudLocationInfoIamUsesHostFromUrlNotOc1Fallback() {
    CustomerConfigStorageOCIData data = new CustomerConfigStorageOCIData();
    data.useOciIam = true;
    data.ociRegion = "us-sanjose-1";
    data.ociNamespace = "config-namespace";
    data.backupLocation =
        "https://objectstorage.us-langley-1.oraclegovcloud.com/n/gov-ns/b/gov-bucket";

    CloudLocationInfoOci info =
        (CloudLocationInfoOci)
            ociUtil.getCloudLocationInfo(
                YbcBackupUtil.DEFAULT_REGION_STRING, data, null /* backupLocation */);
    assertEquals("objectstorage.us-langley-1.oraclegovcloud.com", info.objectStorageHost);
    assertEquals("gov-ns", info.namespace);
    assertEquals("gov-bucket", info.bucket);
    assertEquals("", info.cloudPath);
  }

  @Test
  public void testGetCloudLocationInfoS3Compat() {
    CloudLocationInfoOci info =
        (CloudLocationInfoOci)
            ociUtil.getCloudLocationInfo(
                YbcBackupUtil.DEFAULT_REGION_STRING, s3CompatData, null /* backupLocation */);
    assertEquals("test-bucket", info.bucket);
    assertEquals("prefix", info.cloudPath);
    assertEquals(
        "test-namespace.compat.objectstorage.us-sanjose-1.oraclecloud.com", info.objectStorageHost);
  }

  @Test
  public void testCreateCloudStoreSpecIamUsesOciCloudType() {
    CloudStoreSpec spec =
        ociUtil.createCloudStoreSpec(
            YbcBackupUtil.DEFAULT_REGION_STRING,
            "univ-uuid/backup-ts/keyspace-foo",
            null /* previousBackupLocation */,
            iamData,
            null /* universe */);
    assertEquals(OCI, spec.getType());
    assertEquals("test-bucket", spec.getBucket());
    assertTrue(spec.getCloudDir().endsWith("/"));
    assertEquals("true", spec.getCredsMap().get(OCIUtil.YBC_USE_OCI_IAM_FIELDNAME));
    assertEquals("us-sanjose-1", spec.getCredsMap().get(OCIUtil.YBC_OCI_REGION_FIELDNAME));
    assertEquals("test-namespace", spec.getCredsMap().get(OCIUtil.YBC_OCI_NAMESPACE_FIELDNAME));
  }

  @Test
  public void testCreateCloudStoreSpecS3CompatUsesS3CloudType() {
    CloudStoreSpec spec =
        ociUtil.createCloudStoreSpec(
            YbcBackupUtil.DEFAULT_REGION_STRING,
            "univ-uuid/backup-ts/keyspace-foo",
            null /* previousBackupLocation */,
            s3CompatData,
            null /* universe */);
    assertEquals(S3, spec.getType());
    assertEquals("test-bucket", spec.getBucket());
    assertEquals("true", spec.getCredsMap().get(OCIUtil.YBC_PATH_STYLE_ACCESS_FIELDNAME));
    assertEquals(
        "test-access-key", spec.getCredsMap().get(OCIUtil.YBC_AWS_ACCESS_KEY_ID_FIELDNAME));
  }

  @Test
  public void testGetCloudTypeOci() {
    assertEquals(OCI, YbcBackupUtil.getCloudType(Util.OCI));
  }
}
