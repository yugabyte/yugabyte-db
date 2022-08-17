// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.YbcBackupUtil.YbcBackupResponse.ResponseCloudStoreSpec;
import com.yugabyte.yw.common.YbcBackupUtil.YbcBackupResponse.ResponseCloudStoreSpec.BucketLocation;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.io.IOException;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.Test.None;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.ybc.BackupServiceTaskExtendedArgs;
import org.yb.ybc.CloudStoreConfig;
import org.yb.ybc.TableBackupSpec;
import play.libs.Json;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;

@RunWith(JUnitParamsRunner.class)
public class YbcBackupUtilTest extends FakeDBApplication {

  private static final Logger LOG = LoggerFactory.getLogger(YbcBackupUtilTest.class);

  @Mock UniverseInfoHandler universeInfoHandler;
  @Mock YbcClientService ybcService;
  @Mock BackupUtil backupUtil;
  @Mock CustomerConfigService configService;
  @Mock EncryptionAtRestManager encryptionAtRestManager;

  @InjectMocks YbcBackupUtil ybcBackupUtil;

  private ResponseCloudStoreSpec withoutRegion;
  private ResponseCloudStoreSpec withRegions;
  private Customer testCustomer;
  private JsonNode s3FormData, s3FormData_regions, s3FormData_noRegions;

  @Before
  public void setup() {
    MockitoAnnotations.initMocks(this);
    initResponseObjects();
    testCustomer = ModelFactory.testCustomer();
    s3FormData =
        Json.parse(
            "{\"configName\": \""
                + "test-S3_1"
                + "\", \"name\": \"S3\","
                + " \"type\": \"STORAGE\", \"data\": {\"BACKUP_LOCATION\": \"s3://foo\","
                + " \"AWS_ACCESS_KEY_ID\": \"A-KEY\", \"AWS_SECRET_ACCESS_KEY\": \"A-SECRET\","
                + "\"REGION_LOCATIONS\": [{\"REGION\":\"us-west1\",\"LOCATION\":\"s3://region-1\"},"
                + "{\"REGION\":\"us-west2\",\"LOCATION\":\"s3://region-2\"}]}}");
    s3FormData_regions =
        Json.parse(
            "{\"configName\": \""
                + "test-S3_2"
                + "\", \"name\": \"S3\","
                + " \"type\": \"STORAGE\", \"data\": {\"BACKUP_LOCATION\":"
                + " \"s3://def_bucket/default\","
                + " \"AWS_ACCESS_KEY_ID\": \"A-KEY\", \"AWS_SECRET_ACCESS_KEY\": \"A-SECRET\","
                + "\"REGION_LOCATIONS\": [{\"REGION\":\"us-west1\",\"LOCATION\":"
                + "\"s3://reg1_bucket/region_1\"},"
                + "{\"REGION\":\"us-west2\",\"LOCATION\":\"s3://reg2_bucket/region_2\"}]}}");
    s3FormData_noRegions =
        Json.parse(
            "{\"configName\": \""
                + "test-S3_3"
                + "\", \"name\": \"S3\","
                + " \"type\": \"STORAGE\", \"data\": {\"BACKUP_LOCATION\":"
                + " \"s3://def_bucket/default\","
                + " \"AWS_ACCESS_KEY_ID\": \"A-KEY\", \"AWS_SECRET_ACCESS_KEY\": \"A-SECRET\"}}");
  }

  private void initResponseObjects() {
    BucketLocation defaultBucketLocation = new BucketLocation();
    defaultBucketLocation.bucket = "def_bucket";
    defaultBucketLocation.cloudDir = "default/foo/keyspace-bar/";
    BucketLocation regionalLocation_1 = new BucketLocation();
    regionalLocation_1.bucket = "reg1_bucket";
    regionalLocation_1.cloudDir = "region_1/foo/keyspace-bar/";
    BucketLocation regionalLocation_2 = new BucketLocation();
    regionalLocation_2.bucket = "reg2_bucket";
    regionalLocation_2.cloudDir = "region_2/foo/keyspace-bar/";
    Map<String, BucketLocation> regionMap =
        new HashMap<String, BucketLocation>() {
          {
            put("us-west1", regionalLocation_1);
            put("us-west2", regionalLocation_2);
          }
        };
    withoutRegion = new ResponseCloudStoreSpec();
    withoutRegion.defaultLocation = defaultBucketLocation;
    withRegions = new ResponseCloudStoreSpec();
    withRegions.defaultLocation = defaultBucketLocation;
    withRegions.regionLocations = regionMap;
  }

  @SuppressWarnings("unused")
  private Object[] getBackupSuccessFileYbc() {
    String backupSuccessWithRegions = "backup/ybc_success_file_with_regions.json";
    String backupSuccessWithNoRegions = "backup/ybc_success_file_without_regions.json";
    return new Object[] {
      new Object[] {backupSuccessWithNoRegions, false},
      new Object[] {backupSuccessWithRegions, true}
    };
  }

  @Test
  @Parameters(method = "getBackupSuccessFileYbc")
  public void testExtractSuccessFile(String dataFile, boolean regions) throws IOException {
    String success = TestUtils.readResource(dataFile);
    YbcBackupResponse ybcBackupResponse = ybcBackupUtil.parseYbcBackupResponse(success);
    if (!regions) {
      assertNull(ybcBackupResponse.responseCloudStoreSpec.regionLocations);
      assertTrue(
          ybcBackupResponse.responseCloudStoreSpec.defaultLocation.bucket.equals(
              withoutRegion.defaultLocation.bucket));
      assertTrue(
          ybcBackupResponse.responseCloudStoreSpec.defaultLocation.cloudDir.equals(
              withoutRegion.defaultLocation.cloudDir));
    } else {
      assertNotNull(ybcBackupResponse.responseCloudStoreSpec.regionLocations);
      assertTrue(
          ybcBackupResponse.responseCloudStoreSpec.defaultLocation.bucket.equals(
              withRegions.defaultLocation.bucket));
      assertTrue(
          ybcBackupResponse.responseCloudStoreSpec.defaultLocation.cloudDir.equals(
              withRegions.defaultLocation.cloudDir));
      assertEquals(
          ybcBackupResponse.responseCloudStoreSpec.regionLocations.keySet(),
          withRegions.regionLocations.keySet());
    }
  }

  @Test
  @Parameters(value = {"backup/ybc_invalid_success_file.json"})
  public void testExtractSuccessFileInvalid(String dataFile) throws IOException {
    String success = TestUtils.readResource(dataFile);
    Exception e =
        assertThrows(
            Exception.class,
            () -> {
              ybcBackupUtil.parseYbcBackupResponse(success);
            });
    assertEquals(
        "errorJson: {\"responseCloudStoreSpec.defaultLocation\":\"may not be null\"}",
        e.getMessage());
  }

  @Test(expected = None.class)
  @Parameters(method = "getBackupSuccessFileYbc")
  public void testValidateSuccessFileWithCloudStoreConfigValid(String dataFile, boolean regions) {
    String success = TestUtils.readResource(dataFile);
    YbcBackupResponse ybcBackupResponse = ybcBackupUtil.parseYbcBackupResponse(success);
    CustomerConfig storageConfig = null;
    if (regions) {
      storageConfig = CustomerConfig.createWithFormData(testCustomer.uuid, s3FormData_regions);
    } else {
      storageConfig = CustomerConfig.createWithFormData(testCustomer.uuid, s3FormData_noRegions);
    }
    String commonDir = "foo/keyspace-bar";
    when(mockAWSUtil.createCloudStoreSpec(anyString(), anyString(), any())).thenCallRealMethod();
    when(mockAWSUtil.getOrCreateHostBase(any(), eq("def_bucket"), eq("us-east-1")))
        .thenReturn("s3.us-east-1.amazonaws.com");
    when(mockAWSUtil.getOrCreateHostBase(any(), eq("reg1_bucket"), eq("ap-south-1")))
        .thenReturn("s3.ap-south-1.amazonaws.com");
    when(mockAWSUtil.getOrCreateHostBase(any(), eq("reg2_bucket"), eq("eu-south-1")))
        .thenReturn("s3.eu-south-1.amazonaws.com");
    when(mockAWSUtil.getBucketRegion(eq("def_bucket"), any())).thenReturn("us-east-1");
    when(mockAWSUtil.getBucketRegion(eq("reg1_bucket"), any())).thenReturn("ap-south-1");
    when(mockAWSUtil.getBucketRegion(eq("reg2_bucket"), any())).thenReturn("eu-south-1");
    when(mockAWSUtil.getRegionLocationsMap(any())).thenCallRealMethod();
    CloudStoreConfig csConfig =
        ybcBackupUtil.createCloudStoreConfig(storageConfig, commonDir, false);
    ybcBackupUtil.validateConfigWithSuccessMarker(ybcBackupResponse, csConfig);
  }

  @Test
  @Parameters(value = {"backup/ybc_success_file_with_regions.json"})
  public void testValidateSuccessFileWithCloudStoreConfig_Invalid_NoRegion(String dataFile) {
    String success = TestUtils.readResource(dataFile);
    YbcBackupResponse ybcBackupResponse = ybcBackupUtil.parseYbcBackupResponse(success);
    CustomerConfig storageConfig =
        CustomerConfig.createWithFormData(testCustomer.uuid, s3FormData_noRegions);
    String commonDir = "foo/keyspace-bar";
    when(mockAWSUtil.createCloudStoreSpec(anyString(), anyString(), any())).thenCallRealMethod();
    when(mockAWSUtil.getOrCreateHostBase(any(), eq("def_bucket"), eq("us-east-1")))
        .thenReturn("s3.us-east-1.amazonaws.com");
    when(mockAWSUtil.getBucketRegion(eq("def_bucket"), any())).thenReturn("us-east-1");
    when(mockAWSUtil.getRegionLocationsMap(any())).thenCallRealMethod();
    CloudStoreConfig csConfig =
        ybcBackupUtil.createCloudStoreConfig(storageConfig, commonDir, false);
    assertThrows(
        PlatformServiceException.class,
        () -> {
          ybcBackupUtil.validateConfigWithSuccessMarker(ybcBackupResponse, csConfig);
        });
  }

  @Test
  @Parameters(value = {"backup/ybc_success_file_without_regions.json"})
  public void testValidateSuccessFileWithCloudStoreConfig_Invalid_DefaultDir(String dataFile) {
    String success = TestUtils.readResource(dataFile);
    YbcBackupResponse ybcBackupResponse = ybcBackupUtil.parseYbcBackupResponse(success);
    CustomerConfig storageConfig =
        CustomerConfig.createWithFormData(testCustomer.uuid, s3FormData_noRegions);
    String commonDir = "wrong-foo/keyspace-bar";
    when(mockAWSUtil.createCloudStoreSpec(anyString(), anyString(), any())).thenCallRealMethod();
    when(mockAWSUtil.getOrCreateHostBase(any(), eq("def_bucket"), eq("us-east-1")))
        .thenReturn("s3.us-east-1.amazonaws.com");
    when(mockAWSUtil.getBucketRegion(eq("def_bucket"), any())).thenReturn("us-east-1");
    when(mockAWSUtil.getRegionLocationsMap(any())).thenCallRealMethod();
    CloudStoreConfig csConfig =
        ybcBackupUtil.createCloudStoreConfig(storageConfig, commonDir, false);
    assertThrows(
        PlatformServiceException.class,
        () -> {
          ybcBackupUtil.validateConfigWithSuccessMarker(ybcBackupResponse, csConfig);
        });
  }

  @SuppressWarnings("unused")
  private Object[] getExtendedArgs() {
    return new Object[] {
      new Object[] {"{\"universe_keys\":[{\"key_ref\":\"foo\",\"key_provider\":\"AWS\"}]}"},
      new Object[] {
        "{\"universe_keys\":[{\"key_ref\":\"foo\",\"key_provider\":\"AWS\"},"
            + "{\"key_ref\":\"bar\",\"key_provider\":\"AWS\"}]}"
      }
    };
  }

  @Test
  @Parameters(method = "getExtendedArgs")
  public void testExtractUniverseKeys(String extendedArgs) {
    JsonNode universeKeys = ybcBackupUtil.getUniverseKeysJsonFromSuccessMarker(extendedArgs);
    ObjectMapper mapper = new ObjectMapper();
    try {
      JsonNode expectedKeys = mapper.readTree(extendedArgs).get("universe_keys");
      assertTrue(universeKeys.isArray());
      assertTrue(universeKeys.size() == expectedKeys.size());
      Iterator<JsonNode> it1 = universeKeys.elements();
      Iterator<JsonNode> it2 = universeKeys.elements();
      while (it1.hasNext() && it2.hasNext()) {
        assertTrue(it1.next().get("key_ref").asText().equals(it2.next().get("key_ref").asText()));
        assertTrue(
            it1.next()
                .get("key_provider")
                .asText()
                .equals(it2.next().get("key_provider").asText()));
      }
    } catch (Exception e) {
    }
  }

  @Test
  public void testExtractRegionsFromMetadata() {
    CustomerConfig storageConfig = CustomerConfig.createWithFormData(testCustomer.uuid, s3FormData);
    when(configService.getOrBadRequest(testCustomer.uuid, storageConfig.configUUID))
        .thenReturn(storageConfig);
    BackupTableParams tableParams = new BackupTableParams();
    tableParams.universeUUID = UUID.randomUUID();
    tableParams.customerUuid = testCustomer.uuid;
    tableParams.storageConfigUUID = storageConfig.configUUID;
    tableParams.storageLocation =
        "s3://foo/univ-" + tableParams.universeUUID + "backup-timestamp/keyspace-bar";
    BucketLocation bL1 = new BucketLocation();
    bL1.bucket = "region-1";
    bL1.cloudDir = "univ-" + tableParams.universeUUID + "backup-timestamp/keyspace-bar";
    BucketLocation bL2 = new BucketLocation();
    bL2.bucket = "region-2";
    bL2.cloudDir = "univ-" + tableParams.universeUUID + "backup-timestamp/keyspace-bar";
    Map<String, BucketLocation> regionMap =
        new HashMap<String, BucketLocation>() {
          {
            put("us-west1", bL1);
            put("us-west2", bL2);
          }
        };
    when(mockAWSUtil.getRegionLocationsMap(any())).thenCallRealMethod();
    List<BackupUtil.RegionLocations> regionLocations =
        ybcBackupUtil.extractRegionLocationFromMetadata(regionMap, tableParams);
    String expectedLoc1 =
        "s3://region-1/univ-" + tableParams.universeUUID + "backup-timestamp/keyspace-bar";
    String expectedLoc2 =
        "s3://region-2/univ-" + tableParams.universeUUID + "backup-timestamp/keyspace-bar";
    Map<String, String> regionLocationMap = new HashMap<>();
    regionLocations.stream().forEach(rL -> regionLocationMap.put(rL.REGION, rL.LOCATION));
    assertEquals(regionLocationMap.get("us-west1"), expectedLoc1);
    assertEquals(regionLocationMap.get("us-west2"), expectedLoc2);
  }

  @Test
  @Parameters(value = {"backup/ybc_extended_args_backup_keys.json"})
  public void testGetExtendedBackupArgs(String filePath) throws Exception {
    BackupTableParams tableParams = new BackupTableParams();
    tableParams.useTablespaces = true;
    tableParams.universeUUID = UUID.randomUUID();
    String backupKeys = TestUtils.readResource(filePath);
    ObjectMapper mapper = new ObjectMapper();
    ObjectNode keysNode = mapper.readValue(backupKeys, ObjectNode.class);
    when(encryptionAtRestManager.backupUniverseKeyHistory(tableParams.universeUUID))
        .thenReturn(keysNode);
    String keys = mapper.writeValueAsString(keysNode);
    BackupServiceTaskExtendedArgs extArgs = ybcBackupUtil.getExtendedArgsForBackup(tableParams);
    assertEquals(true, extArgs.getUseTablespaces());
    assertEquals(keys, extArgs.getBackupConfigData());
  }

  @Test
  public void testCreateCloudStoreConfig() {
    CustomerConfig storageConfig = CustomerConfig.createWithFormData(testCustomer.uuid, s3FormData);
    UUID uniUUID = UUID.randomUUID();
    String commonDir = "univ-" + uniUUID + "/backup-timestamp/keyspace-foo";
    when(mockAWSUtil.createCloudStoreSpec(anyString(), anyString(), any())).thenCallRealMethod();
    when(mockAWSUtil.getOrCreateHostBase(any(), eq("foo"), eq("us-east-1")))
        .thenReturn("s3.us-east-1.amazonaws.com");
    when(mockAWSUtil.getOrCreateHostBase(any(), eq("region-1"), eq("ap-south-1")))
        .thenReturn("s3.ap-south-1.amazonaws.com");
    when(mockAWSUtil.getOrCreateHostBase(any(), eq("region-2"), eq("eu-south-1")))
        .thenReturn("s3.eu-south-1.amazonaws.com");
    when(mockAWSUtil.getBucketRegion(eq("foo"), any())).thenReturn("us-east-1");
    when(mockAWSUtil.getBucketRegion(eq("region-1"), any())).thenReturn("ap-south-1");
    when(mockAWSUtil.getBucketRegion(eq("region-2"), any())).thenReturn("eu-south-1");
    when(mockAWSUtil.getRegionLocationsMap(any())).thenCallRealMethod();
    CloudStoreConfig csConfig =
        ybcBackupUtil.createCloudStoreConfig(storageConfig, commonDir, false);
    Map<String, String> s3DefaultCredsMap =
        new HashMap<String, String>() {
          {
            put("AWS_ACCESS_KEY_ID", "A-KEY");
            put("AWS_SECRET_ACCESS_KEY", "A-SECRET");
            put("AWS_ENDPOINT", "s3.us-east-1.amazonaws.com");
            put("AWS_DEFAULT_REGION", "us-east-1");
          }
        };
    Map<String, String> s3Region_1CredsMap =
        new HashMap<String, String>() {
          {
            put("AWS_ACCESS_KEY_ID", "A-KEY");
            put("AWS_SECRET_ACCESS_KEY", "A-SECRET");
            put("AWS_ENDPOINT", "s3.ap-south-1.amazonaws.com");
            put("AWS_DEFAULT_REGION", "ap-south-1");
          }
        };
    Map<String, String> s3Region_2CredsMap =
        new HashMap<String, String>() {
          {
            put("AWS_ACCESS_KEY_ID", "A-KEY");
            put("AWS_SECRET_ACCESS_KEY", "A-SECRET");
            put("AWS_ENDPOINT", "s3.eu-south-1.amazonaws.com");
            put("AWS_DEFAULT_REGION", "eu-south-1");
          }
        };
    assertTrue(csConfig.getDefaultSpec().getCredsMap().equals(s3DefaultCredsMap));
    assertTrue(
        csConfig.getRegionSpecMapMap().get("us-west1").getCredsMap().equals(s3Region_1CredsMap));
    assertTrue(
        csConfig.getRegionSpecMapMap().get("us-west2").getCredsMap().equals(s3Region_2CredsMap));
    String expectedDir = commonDir.concat("/");
    assertEquals(expectedDir, csConfig.getDefaultSpec().getCloudDir());
  }

  @Test
  public void testGetTableBackupSpec() {
    BackupTableParams tableParams = new BackupTableParams();
    tableParams.tableNameList = Arrays.asList("table-1", "table-2");
    Map<String, String> expectedTBMap =
        new HashMap<String, String>() {
          {
            put("table-1", "foo");
            put("table-2", "foo");
          }
        };
    tableParams.setKeyspace("foo");
    TableBackupSpec tBSpec = ybcBackupUtil.getTableBackupSpec(tableParams);
    Map<String, String> actualTBMap = new HashMap<>();
    tBSpec
        .getTablesList()
        .stream()
        .forEach(
            tB -> {
              actualTBMap.put(tB.getTable(), tB.getKeyspace());
            });
    assertEquals(expectedTBMap, actualTBMap);
  }
}
