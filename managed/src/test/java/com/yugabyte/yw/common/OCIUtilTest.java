// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.yb.ybc.CloudType.OCI;
import static org.yb.ybc.CloudType.S3;

import com.yugabyte.yw.common.OCIUtil.CloudLocationInfoOci;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageOCIData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageOCIData.RegionLocations;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import java.util.Collections;
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
  public void testToS3CompatibleDataHardcodesPathStyleChunkedAndSigningRegion() {
    CustomerConfigStorageOCIData data = s3CompatData();
    CustomerConfigStorageS3Data s3Data = OCIUtil.toS3CompatibleData(data);
    assertTrue(s3Data.isPathStyleAccess);
    assertFalse(s3Data.useChunkedEncoding);
    assertEquals("test-access-key", s3Data.awsAccessKeyId);
    assertEquals("test-secret-key", s3Data.awsSecretAccessKey);
    assertEquals(
        "test-namespace.compat.objectstorage.us-sanjose-1.oraclecloud.com", s3Data.awsHostBase);
    // OCI_REGION maps to S3 signing region.
    assertEquals("us-sanjose-1", s3Data.fallbackRegion);
    assertFalse(s3Data.globalBucketAccess);
  }

  @Test
  public void testToS3CompatibleDataUsesRegionAsSigningRegionForRegionalLocations() {
    CustomerConfigStorageOCIData data = s3CompatData();
    RegionLocations regionLocation = new RegionLocations();
    regionLocation.region = "us-ashburn-1";
    regionLocation.location = "s3://ashburn-bucket/prefix";
    regionLocation.ociS3HostBase =
        "test-namespace.compat.objectstorage.us-ashburn-1.oraclecloud.com";
    data.regionLocations = Collections.singletonList(regionLocation);

    CustomerConfigStorageS3Data s3Data = OCIUtil.toS3CompatibleData(data);
    assertEquals(1, s3Data.regionLocations.size());
    assertEquals("us-ashburn-1", s3Data.regionLocations.get(0).fallbackRegion);
    assertEquals(
        "test-namespace.compat.objectstorage.us-ashburn-1.oraclecloud.com",
        s3Data.regionLocations.get(0).awsHostBase);
  }

  @Test
  public void testCreateCredsMapYbcUsesRegionalHostBaseAndSigningRegion() {
    CustomerConfigStorageOCIData data = s3CompatData();
    RegionLocations regionLocation = new RegionLocations();
    regionLocation.region = "us-ashburn-1";
    regionLocation.location = "s3://ashburn-bucket/prefix";
    regionLocation.ociS3HostBase =
        "test-namespace.compat.objectstorage.us-ashburn-1.oraclecloud.com";
    data.regionLocations = Collections.singletonList(regionLocation);

    Map<String, String> creds = ociUtil.createCredsMapYbc(data, "us-ashburn-1");
    assertEquals(
        "test-namespace.compat.objectstorage.us-ashburn-1.oraclecloud.com",
        creds.get(OCIUtil.YBC_AWS_ENDPOINT_FIELDNAME));
    assertEquals("us-ashburn-1", creds.get(OCIUtil.YBC_AWS_DEFAULT_REGION_FIELDNAME));
  }

  @Test
  public void testToS3CompatibleDataNormalizesNativeLocationToS3() {
    CustomerConfigStorageOCIData data = s3CompatData();
    data.backupLocation =
        "https://objectstorage.us-sanjose-1.oraclecloud.com/n/test-namespace/b/test-bucket/prefix";
    CustomerConfigStorageS3Data s3Data = OCIUtil.toS3CompatibleData(data);
    assertEquals("s3://test-bucket/prefix", s3Data.backupLocation);
  }

  @Test
  public void testToS3StyleLocationNativeBucketOnly() {
    assertEquals(
        "s3://test-bucket",
        OCIUtil.toS3StyleLocation(
            "https://objectstorage.us-sanjose-1.oraclecloud.com/n/test-namespace/b/test-bucket"));
  }

  @Test
  public void testToS3StyleLocationNativeBackupStorageLocation() {
    // Restore preflight / delete pass Backup.storageLocation (native HTTPS + backup path).
    assertEquals(
        "s3://test-bucket/univ-x/postgres/ybc_backup-abc/full/2026-08-09T17:15:55/multi-table-pg",
        OCIUtil.toS3StyleLocation(
            "https://objectstorage.us-sanjose-1.oraclecloud.com/n/test-namespace/b/test-bucket"
                + "/univ-x/postgres/ybc_backup-abc/full/2026-08-09T17:15:55/multi-table-pg"));
  }

  @Test
  public void testGetCloudLocationInfoIamWithS3StyleLocation() {
    CustomerConfigStorageOCIData data = new CustomerConfigStorageOCIData();
    data.useOciIam = true;
    data.ociRegion = "us-sanjose-1";
    data.ociNamespace = "test-namespace";
    data.backupLocation = "s3://test-bucket";
    CloudLocationInfoOci info =
        (CloudLocationInfoOci)
            ociUtil.getCloudLocationInfo(
                YbcBackupUtil.DEFAULT_REGION_STRING, data, null /* backupLocation */);
    assertEquals("test-bucket", info.bucket);
    assertEquals("", info.cloudPath);
    assertEquals("test-namespace", info.namespace);
    assertEquals("objectstorage.us-sanjose-1.oraclecloud.com", info.objectStorageHost);
  }

  @Test
  public void testGetCloudLocationInfoIamS3StyleUsesPlacementRegionForHost() {
    CustomerConfigStorageOCIData data = new CustomerConfigStorageOCIData();
    data.useOciIam = true;
    data.ociRegion = "us-sanjose-1";
    data.ociNamespace = "test-namespace";
    data.backupLocation = "s3://default-bucket";
    RegionLocations regionLocation = new RegionLocations();
    regionLocation.region = "us-ashburn-1";
    regionLocation.location = "s3://ashburn-bucket";
    data.regionLocations = Collections.singletonList(regionLocation);

    CloudLocationInfoOci info =
        (CloudLocationInfoOci)
            ociUtil.getCloudLocationInfo("us-ashburn-1", data, null /* backupLocation */);
    assertEquals("ashburn-bucket", info.bucket);
    assertEquals("objectstorage.us-ashburn-1.oraclecloud.com", info.objectStorageHost);
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
  public void testCreateCredsMapYbcIamUsesPlacementRegion() {
    RegionLocations regionLocation = new RegionLocations();
    regionLocation.region = "us-ashburn-1";
    regionLocation.location =
        "https://objectstorage.us-ashburn-1.oraclecloud.com/n/test-namespace/b/ashburn-bucket";
    iamData.regionLocations = Collections.singletonList(regionLocation);

    Map<String, String> creds = ociUtil.createCredsMapYbc(iamData, "us-ashburn-1");
    assertEquals("us-ashburn-1", creds.get(OCIUtil.YBC_OCI_REGION_FIELDNAME));
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
    assertNull(creds.get("USE_CHUNKED_ENCODING"));
    assertFalse(creds.containsKey(OCIUtil.YBC_USE_OCI_IAM_FIELDNAME));
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
  public void testGetSplitLocationValueNativeUrlWithCloudPath() {
    // Stored backup locations append a path after /b/<bucket> (no /o/).
    String[] split =
        OCIUtil.getSplitLocationValue(
            "https://objectstorage.us-sanjose-1.oraclecloud.com/n/test-namespace/b/test-bucket"
                + "/univ-uuid/backup-ts");
    assertEquals(2, split.length);
    assertEquals("test-bucket", split[0]);
    assertEquals("univ-uuid/backup-ts", split[1]);
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
    assertEquals("", parts.cloudPath);
  }

  @Test
  public void testGetCloudLocationInfoIamUsesHostFromUrlNotOc1Fallback() {
    CustomerConfigStorageOCIData data = new CustomerConfigStorageOCIData();
    data.useOciIam = true;
    data.ociRegion = "us-sanjose-1";
    // Namespace field is the YBC/SDK source of truth; URL host still preferred.
    data.ociNamespace = "gov-ns";
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
  public void testGetCloudLocationInfoIamStoredBackupPath() {
    CloudLocationInfoOci info =
        (CloudLocationInfoOci)
            ociUtil.getCloudLocationInfo(
                YbcBackupUtil.DEFAULT_REGION_STRING,
                iamData,
                iamData.backupLocation + "/univ-uuid/backup-ts/keyspace-foo");
    assertEquals("test-bucket", info.bucket);
    assertEquals("univ-uuid/backup-ts/keyspace-foo", info.cloudPath);
    assertEquals("test-namespace", info.namespace);
  }

  @Test
  public void testExtractRegionFromObjectStorageHost() {
    assertEquals(
        "us-ashburn-1",
        OCIUtil.extractRegionFromObjectStorageHost("objectstorage.us-ashburn-1.oraclecloud.com"));
    assertEquals(
        "us-langley-1",
        OCIUtil.extractRegionFromObjectStorageHost(
            "objectstorage.us-langley-1.oraclegovcloud.com"));
    assertEquals(
        "us-ashburn-1",
        OCIUtil.extractRegionFromObjectStorageHost(
            "myns.objectstorage.us-ashburn-1.oci.customer-oci.com"));
    assertNull(OCIUtil.extractRegionFromObjectStorageHost("random-host.example.com"));
  }

  @Test
  public void testToObjectStorageEndpointPreservesDedicatedAndGovHosts() {
    assertEquals(
        "https://objectstorage.us-sanjose-1.oraclecloud.com",
        OCIUtil.toObjectStorageEndpoint("objectstorage.us-sanjose-1.oraclecloud.com"));
    assertEquals(
        "https://objectstorage.us-langley-1.oraclegovcloud.com",
        OCIUtil.toObjectStorageEndpoint("objectstorage.us-langley-1.oraclegovcloud.com"));
    assertEquals(
        "https://myns.objectstorage.us-ashburn-1.oci.customer-oci.com",
        OCIUtil.toObjectStorageEndpoint("myns.objectstorage.us-ashburn-1.oci.customer-oci.com"));
    assertEquals(
        "https://objectstorage.us-phoenix-1.oci.customer-oci.com",
        OCIUtil.toObjectStorageEndpoint("https://objectstorage.us-phoenix-1.oci.customer-oci.com"));
    assertNull(OCIUtil.toObjectStorageEndpoint(null));
    assertNull(OCIUtil.toObjectStorageEndpoint(""));
  }

  @Test
  public void testObjectStorageEndpointOverrideSkipsSynthesizedOc1Host() {
    assertNull(
        OCIUtil.objectStorageEndpointOverride(
            "objectstorage.us-sanjose-1.oraclecloud.com", "us-sanjose-1"));
    assertNull(
        OCIUtil.objectStorageEndpointOverride(
            "https://objectstorage.us-langley-1.oraclecloud.com", "us-langley-1"));
    assertEquals(
        "https://objectstorage.us-langley-1.oraclegovcloud.com",
        OCIUtil.objectStorageEndpointOverride(
            "objectstorage.us-langley-1.oraclegovcloud.com", "us-langley-1"));
    assertEquals(
        "https://myns.objectstorage.us-ashburn-1.oci.customer-oci.com",
        OCIUtil.objectStorageEndpointOverride(
            "myns.objectstorage.us-ashburn-1.oci.customer-oci.com", "us-ashburn-1"));
  }

  @Test
  public void testCreateObjectStorageClientRequiresRegion() {
    try {
      ociUtil.createObjectStorageClient(iamData, (String) null);
      throw new AssertionError("expected PlatformServiceException");
    } catch (PlatformServiceException e) {
      assertTrue(e.getMessage().contains("OCI region is required"));
    }
    try {
      ociUtil.createObjectStorageClient(iamData, "  ");
      throw new AssertionError("expected PlatformServiceException");
    } catch (PlatformServiceException e) {
      assertTrue(e.getMessage().contains("OCI region is required"));
    }
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

  @Test
  public void testListBucketsIamReturnsEmpty() {
    // Match GCS IAM-only / Azure: no compartment-scoped ListBuckets without extra config fields.
    assertTrue(ociUtil.listBuckets(iamData).isEmpty());
  }

  private CustomerConfigStorageOCIData s3CompatData() {
    CustomerConfigStorageOCIData data = new CustomerConfigStorageOCIData();
    data.useOciIam = false;
    data.backupLocation = "s3://test-bucket/prefix";
    data.ociS3AccessKeyId = "test-access-key";
    data.ociS3SecretAccessKey = "test-secret-key";
    data.ociRegion = "us-sanjose-1";
    data.ociS3HostBase = "test-namespace.compat.objectstorage.us-sanjose-1.oraclecloud.com";
    return data;
  }
}
