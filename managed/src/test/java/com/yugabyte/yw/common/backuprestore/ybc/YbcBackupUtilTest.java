// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.backuprestore.ybc;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.nullable;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.BackupUtil.PerBackupLocationKeyspaceTables;
import com.yugabyte.yw.common.backuprestore.BackupUtil.PerLocationBackupInfo;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.SnapshotObjectType;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.TablesMetadata;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse.ResponseCloudStoreSpec;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse.ResponseCloudStoreSpec.BucketLocation;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse.SnapshotObjectDetails.TableData;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.apache.commons.collections4.CollectionUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.Test.None;
import org.junit.runner.RunWith;
import org.yb.CommonTypes.TableType;
import org.yb.ybc.BackupServiceTaskExtendedArgs;
import org.yb.ybc.CloudStoreConfig;
import org.yb.ybc.TableBackupSpec;
import org.yb.ybc.TableRestoreSpec;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class YbcBackupUtilTest extends FakeDBApplication {

  private UniverseInfoHandler universeInfoHandler;
  private CustomerConfigService configService;
  private EncryptionAtRestManager encryptionAtRestManager;
  private YbcBackupUtil ybcBackupUtil;

  private ResponseCloudStoreSpec withoutRegion;
  private ResponseCloudStoreSpec withRegions;
  private Customer testCustomer;
  private JsonNode s3FormData, s3FormData_regions, s3FormData_noRegions;

  private final String objectID = "000033e1000030008000000000004010";

  @Before
  public void setup() {
    universeInfoHandler = mock(UniverseInfoHandler.class);
    configService = mock(CustomerConfigService.class);
    encryptionAtRestManager = mock(EncryptionAtRestManager.class);
    ybcBackupUtil =
        new YbcBackupUtil(
            universeInfoHandler,
            configService,
            encryptionAtRestManager,
            mockBackupHelper,
            mockStorageUtilFactory);

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
  @Parameters(value = {"backup/ybc_success_file_without_regions.json"})
  public void testExtractSuccessFileWithoutRegion(String dataFile) throws IOException {
    String success = TestUtils.readResource(dataFile);
    YbcBackupResponse ybcBackupResponse = YbcBackupUtil.parseYbcBackupResponse(success);
    assertNull(ybcBackupResponse.responseCloudStoreSpec.regionLocations);
    assertTrue(
        ybcBackupResponse.responseCloudStoreSpec.defaultLocation.bucket.equals(
            withoutRegion.defaultLocation.bucket));
    assertTrue(
        ybcBackupResponse.responseCloudStoreSpec.defaultLocation.cloudDir.equals(
            withoutRegion.defaultLocation.cloudDir));
    assertTrue(
        ybcBackupResponse
            .snapshotObjectDetails
            .get(0)
            .type
            .equals(YbcBackupUtil.SnapshotObjectType.NAMESPACE));
    assertTrue(
        ybcBackupResponse
            .snapshotObjectDetails
            .get(1)
            .type
            .equals(YbcBackupUtil.SnapshotObjectType.TABLE));
    assertTrue(
        ybcBackupResponse
            .snapshotObjectDetails
            .get(2)
            .type
            .equals(YbcBackupUtil.SnapshotObjectType.DEFAULT_TYPE));

    assertTrue(
        ybcBackupResponse.snapshotObjectDetails.get(0).data
            instanceof YbcBackupResponse.SnapshotObjectDetails.NamespaceData);
    assertTrue(
        ybcBackupResponse.snapshotObjectDetails.get(1).data
            instanceof YbcBackupResponse.SnapshotObjectDetails.TableData);
    // Verify custom type does not fail
    assertTrue(
        ybcBackupResponse.snapshotObjectDetails.get(2).data
            instanceof YbcBackupResponse.SnapshotObjectDetails.SnapshotObjectData);
  }

  @Test
  @Parameters(value = {"backup/ybc_success_file_with_regions.json"})
  public void testExtractSuccessFileWithRegion(String dataFile) throws IOException {
    String success = TestUtils.readResource(dataFile);
    YbcBackupResponse ybcBackupResponse = YbcBackupUtil.parseYbcBackupResponse(success);
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
    assertTrue(
        ybcBackupResponse
            .snapshotObjectDetails
            .get(0)
            .type
            .equals(YbcBackupUtil.SnapshotObjectType.NAMESPACE));
    assertTrue(
        ybcBackupResponse
            .snapshotObjectDetails
            .get(1)
            .type
            .equals(YbcBackupUtil.SnapshotObjectType.TABLE));
    assertTrue(
        ybcBackupResponse.snapshotObjectDetails.get(0).data
            instanceof YbcBackupResponse.SnapshotObjectDetails.NamespaceData);
    assertTrue(
        ybcBackupResponse.snapshotObjectDetails.get(1).data
            instanceof YbcBackupResponse.SnapshotObjectDetails.TableData);
  }

  @Test
  @Parameters(value = {"backup/ybc_invalid_success_file.json"})
  public void testExtractSuccessFileInvalid(String dataFile) throws IOException {
    String success = TestUtils.readResource(dataFile);
    Exception e =
        assertThrows(
            Exception.class,
            () -> {
              YbcBackupUtil.parseYbcBackupResponse(success);
            });
    assertEquals(
        "errorJson: {\"responseCloudStoreSpec.defaultLocation\":\"must not be null\"}",
        e.getMessage());
  }

  @Test(expected = None.class)
  @Parameters(method = "getBackupSuccessFileYbc")
  public void testValidateSuccessFileWithCloudStoreConfigValid(String dataFile, boolean regions) {
    String success = TestUtils.readResource(dataFile);
    YbcBackupResponse ybcBackupResponse = YbcBackupUtil.parseYbcBackupResponse(success);
    CustomerConfig storageConfig = null;
    if (regions) {
      storageConfig = CustomerConfig.createWithFormData(testCustomer.getUuid(), s3FormData_regions);
    } else {
      storageConfig =
          CustomerConfig.createWithFormData(testCustomer.getUuid(), s3FormData_noRegions);
    }
    String commonDir = "foo/keyspace-bar";
    when(mockStorageUtilFactory.getStorageUtil(eq("S3"))).thenReturn(mockAWSUtil);
    when(mockAWSUtil.createCloudStoreSpec(anyString(), anyString(), nullable(String.class), any()))
        .thenCallRealMethod();
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
    CloudStoreConfig csConfig = ybcBackupUtil.createBackupConfig(storageConfig, commonDir);
    YbcBackupUtil.validateConfigWithSuccessMarker(ybcBackupResponse, csConfig, false);
  }

  @Test
  @Parameters(value = {"backup/ybc_success_file_with_regions.json"})
  public void testValidateSuccessFileWithCloudStoreConfig_Invalid_NoRegion(String dataFile) {
    String success = TestUtils.readResource(dataFile);
    YbcBackupResponse ybcBackupResponse = YbcBackupUtil.parseYbcBackupResponse(success);
    CustomerConfig storageConfig =
        CustomerConfig.createWithFormData(testCustomer.getUuid(), s3FormData_noRegions);
    String commonDir = "foo/keyspace-bar";
    when(mockStorageUtilFactory.getStorageUtil(eq("S3"))).thenReturn(mockAWSUtil);
    when(mockAWSUtil.createCloudStoreSpec(anyString(), anyString(), nullable(String.class), any()))
        .thenCallRealMethod();
    when(mockAWSUtil.getOrCreateHostBase(any(), eq("def_bucket"), eq("us-east-1")))
        .thenReturn("s3.us-east-1.amazonaws.com");
    when(mockAWSUtil.getBucketRegion(eq("def_bucket"), any())).thenReturn("us-east-1");
    when(mockAWSUtil.getRegionLocationsMap(any())).thenCallRealMethod();
    CloudStoreConfig csConfig = ybcBackupUtil.createBackupConfig(storageConfig, commonDir);
    assertThrows(
        PlatformServiceException.class,
        () -> {
          YbcBackupUtil.validateConfigWithSuccessMarker(ybcBackupResponse, csConfig, false);
        });
  }

  @Test
  @Parameters(value = {"backup/ybc_success_file_without_regions.json"})
  public void testValidateSuccessFileWithCloudStoreConfig_Invalid_DefaultDir(String dataFile) {
    String success = TestUtils.readResource(dataFile);
    YbcBackupResponse ybcBackupResponse = YbcBackupUtil.parseYbcBackupResponse(success);
    CustomerConfig storageConfig =
        CustomerConfig.createWithFormData(testCustomer.getUuid(), s3FormData_noRegions);
    String commonDir = "wrong-foo/keyspace-bar";
    when(mockStorageUtilFactory.getStorageUtil(eq("S3"))).thenReturn(mockAWSUtil);
    when(mockAWSUtil.createCloudStoreSpec(anyString(), anyString(), nullable(String.class), any()))
        .thenCallRealMethod();
    when(mockAWSUtil.getOrCreateHostBase(any(), eq("def_bucket"), eq("us-east-1")))
        .thenReturn("s3.us-east-1.amazonaws.com");
    when(mockAWSUtil.getBucketRegion(eq("def_bucket"), any())).thenReturn("us-east-1");
    when(mockAWSUtil.getRegionLocationsMap(any())).thenCallRealMethod();
    CloudStoreConfig csConfig = ybcBackupUtil.createBackupConfig(storageConfig, commonDir);
    assertThrows(
        PlatformServiceException.class,
        () -> {
          YbcBackupUtil.validateConfigWithSuccessMarker(ybcBackupResponse, csConfig, false);
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
    JsonNode universeKeys = YbcBackupUtil.getUniverseKeysJsonFromSuccessMarker(extendedArgs);
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
    CustomerConfig storageConfig =
        CustomerConfig.createWithFormData(testCustomer.getUuid(), s3FormData);
    when(configService.getOrBadRequest(testCustomer.getUuid(), storageConfig.getConfigUUID()))
        .thenReturn(storageConfig);
    BackupTableParams tableParams = new BackupTableParams();
    tableParams.setUniverseUUID(UUID.randomUUID());
    tableParams.customerUuid = testCustomer.getUuid();
    tableParams.storageConfigUUID = storageConfig.getConfigUUID();
    tableParams.storageLocation =
        "s3://foo/univ-" + tableParams.getUniverseUUID() + "/ybc_backup-timestamp/keyspace-bar";
    BucketLocation bL1 = new BucketLocation();
    bL1.bucket = "region-1";
    bL1.cloudDir = "univ-" + tableParams.getUniverseUUID() + "/ybc_backup-timestamp/keyspace-bar";
    BucketLocation bL2 = new BucketLocation();
    bL2.bucket = "region-2";
    bL2.cloudDir = "univ-" + tableParams.getUniverseUUID() + "/ybc_backup-timestamp/keyspace-bar";
    Map<String, BucketLocation> regionMap =
        new HashMap<String, BucketLocation>() {
          {
            put("us-west1", bL1);
            put("us-west2", bL2);
          }
        };
    when(mockStorageUtilFactory.getStorageUtil(eq("S3"))).thenReturn(mockAWSUtil);
    when(mockAWSUtil.getRegionLocationsMap(any())).thenCallRealMethod();
    List<BackupUtil.RegionLocations> regionLocations =
        ybcBackupUtil.extractRegionLocationFromMetadata(regionMap, tableParams);
    String expectedLoc1 =
        "s3://region-1/univ-"
            + tableParams.getUniverseUUID()
            + "/ybc_backup-timestamp/keyspace-bar";
    String expectedLoc2 =
        "s3://region-2/univ-"
            + tableParams.getUniverseUUID()
            + "/ybc_backup-timestamp/keyspace-bar";
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
    tableParams.setUniverseUUID(UUID.randomUUID());
    String backupKeys = TestUtils.readResource(filePath);
    ObjectMapper mapper = new ObjectMapper();
    ObjectNode keysNode = mapper.readValue(backupKeys, ObjectNode.class);
    when(encryptionAtRestManager.backupUniverseKeyHistory(tableParams.getUniverseUUID()))
        .thenReturn(keysNode);
    String keys = mapper.writeValueAsString(keysNode);
    BackupServiceTaskExtendedArgs extArgs = ybcBackupUtil.getExtendedArgsForBackup(tableParams);
    assertEquals(true, extArgs.getUseTablespaces());
    assertEquals(keys, extArgs.getBackupConfigData());
  }

  @Test
  public void testCreateBackupConfig() {
    CustomerConfig storageConfig =
        CustomerConfig.createWithFormData(testCustomer.getUuid(), s3FormData);
    UUID uniUUID = UUID.randomUUID();
    String commonDir = "univ-" + uniUUID + "/backup-timestamp/keyspace-foo";
    when(mockStorageUtilFactory.getStorageUtil(eq("S3"))).thenReturn(mockAWSUtil);
    when(mockAWSUtil.createCloudStoreSpec(anyString(), anyString(), nullable(String.class), any()))
        .thenCallRealMethod();
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
    CloudStoreConfig csConfig = ybcBackupUtil.createBackupConfig(storageConfig, commonDir);
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
    tBSpec.getTablesList().stream()
        .forEach(
            tB -> {
              actualTBMap.put(tB.getTable(), tB.getKeyspace());
            });
    assertEquals(expectedTBMap, actualTBMap);
  }

  @Test
  @Parameters({"backup/ybc_success_file_with_index_tables.json"})
  public void getTablesListFromSuccessMarkerFilterIndexes(String filePath) {
    String successString = TestUtils.readResource(filePath);
    YbcBackupResponse sM = YbcBackupUtil.parseYbcBackupResponse(successString);
    TablesMetadata tablesMetadata =
        YbcBackupUtil.getTableListFromSuccessMarker(sM, TableType.YQL_TABLE_TYPE, true);
    Set<String> tablesList = tablesMetadata.getParentTables();
    // Index table name from success file
    String indexTable = "cassandrasecondaryindexbyvalue";
    List<String> nonIndexTables = Arrays.asList("batch_ts_metrics_raw", "cassandrasecondaryindex");

    sM.snapshotObjectDetails.stream()
        .filter(sOD -> tablesList.contains(sOD.data.snapshotObjectName))
        .forEach(
            sOD -> {
              assertTrue(
                  sOD.id.equals(
                      tablesMetadata
                          .getTableDetailsMap()
                          .get(sOD.data.snapshotObjectName)
                          .getTableIdentifier()
                          .toString()
                          .replace("-", "")));
            });
    assertTrue(tablesList.containsAll(nonIndexTables));
    assertTrue(!tablesList.contains(indexTable));
  }

  @Test
  @Parameters({"backup/ybc_success_file_with_index_tables.json"})
  public void getTablesListFromSuccessMarkerNotFilterIndexes(String filePath) {
    String successString = TestUtils.readResource(filePath);
    YbcBackupResponse sM = YbcBackupUtil.parseYbcBackupResponse(successString);
    TablesMetadata tablesMetadata =
        YbcBackupUtil.getTableListFromSuccessMarker(sM, TableType.YQL_TABLE_TYPE, false);
    Set<String> parentTables = tablesMetadata.getParentTables();
    Set<String> indexTables = tablesMetadata.getAllIndexTables();
    // Index table name from success file
    String indexTable = "cassandrasecondaryindexbyvalue";
    List<String> nonIndexTables = Arrays.asList("batch_ts_metrics_raw", "cassandrasecondaryindex");

    sM.snapshotObjectDetails.stream()
        .filter(sOD -> parentTables.contains(sOD.data.snapshotObjectName))
        .forEach(
            sOD -> {
              assertTrue(
                  sOD.id.equals(
                      tablesMetadata
                          .getTableDetailsMap()
                          .get(sOD.data.snapshotObjectName)
                          .getTableIdentifier()
                          .toString()
                          .replace("-", "")));
            });
    assertTrue(parentTables.containsAll(nonIndexTables));
    assertTrue(!parentTables.contains(indexTable));
    assertTrue(indexTables.contains(indexTable));
    assertFalse(nonIndexTables.parallelStream().anyMatch(t -> indexTables.contains(t)));
  }

  @Test(expected = Test.None.class)
  public void testGenerateMapToRestoreYCQLFullRestoreNoOverwrite() {
    String backupLocation_1 = "s3://foo/univ-000/backup-001";
    List<String> tL1 = Arrays.asList("t1", "t2", "t3", "t4");
    Map<String, Set<String>> indexTablesMap = new HashMap<>();
    String indexedTable = "t1";
    indexTablesMap.put(indexedTable, new HashSet<>(Arrays.asList("iT1", "iT2")));
    String keyspace1 = "foo";
    PerBackupLocationKeyspaceTables bL1_KeyspaceTables =
        PerBackupLocationKeyspaceTables.builder()
            .originalKeyspace(keyspace1)
            .tablesWithIndexesMap(indexTablesMap)
            .tableNameList(tL1)
            .build();
    PerLocationBackupInfo bInfo1 =
        PerLocationBackupInfo.builder()
            .backupLocation(backupLocation_1)
            .perBackupLocationKeyspaceTables(bL1_KeyspaceTables)
            .build();
    String backupLocation_2 = "s3://foo/univ-000/backup-002";
    List<String> tL2 = Arrays.asList("t1", "t2", "t3", "t4");
    String keyspace2 = "bar";
    PerBackupLocationKeyspaceTables bL2_KeyspaceTables =
        PerBackupLocationKeyspaceTables.builder()
            .originalKeyspace(keyspace2)
            .tableNameList(tL2)
            .build();
    PerLocationBackupInfo bInfo2 =
        PerLocationBackupInfo.builder()
            .backupLocation(backupLocation_2)
            .perBackupLocationKeyspaceTables(bL2_KeyspaceTables)
            .build();
    Map<String, PerLocationBackupInfo> locationBInfoMap = new HashMap<>();
    locationBInfoMap.put(backupLocation_1, bInfo1);
    locationBInfoMap.put(backupLocation_2, bInfo2);

    // Case 1: Full restore
    BackupStorageInfo rInfo_1 = new BackupStorageInfo();
    rInfo_1.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_1.storageLocation = backupLocation_1;
    rInfo_1.keyspace = "foo_r";
    BackupStorageInfo rInfo_2 = new BackupStorageInfo();
    rInfo_2.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_2.storageLocation = backupLocation_2;
    rInfo_2.keyspace = "bar_r";
    List<BackupStorageInfo> bInfosList = Arrays.asList(rInfo_1, rInfo_2);
    Map<TableType, Map<String, Set<String>>> restoreMap =
        YbcBackupUtil.generateMapToRestoreNonRedisYBC(bInfosList, locationBInfoMap);
    Map<String, Set<String>> ycqlRestore = restoreMap.get(TableType.YQL_TABLE_TYPE);

    assertTrue(ycqlRestore.size() == 2);
    assertTrue(restoreMap.get(TableType.PGSQL_TABLE_TYPE).size() == 0);
    assertTrue(ycqlRestore.keySet().containsAll(Arrays.asList(rInfo_1.keyspace, rInfo_2.keyspace)));
    assertTrue(ycqlRestore.get(rInfo_1.keyspace).containsAll(tL1));
    // Verify also contains index table
    assertTrue(ycqlRestore.get(rInfo_1.keyspace).containsAll(indexTablesMap.get(indexedTable)));
    assertTrue(ycqlRestore.get(rInfo_2.keyspace).containsAll(tL2));
  }

  @Test(expected = Test.None.class)
  public void testGenerateMapToRestoreYCQLPartialRestoreNoOverwrite() {
    String backupLocation_1 = "s3://foo/univ-000/backup-001";
    List<String> tL1 = Arrays.asList("t1", "t2", "t3", "t4");
    String keyspace1 = "foo";
    PerBackupLocationKeyspaceTables bL1_KeyspaceTables =
        PerBackupLocationKeyspaceTables.builder()
            .originalKeyspace(keyspace1)
            .tableNameList(tL1)
            .build();
    PerLocationBackupInfo bInfo1 =
        PerLocationBackupInfo.builder()
            .backupLocation(backupLocation_1)
            .perBackupLocationKeyspaceTables(bL1_KeyspaceTables)
            .build();
    String backupLocation_2 = "s3://foo/univ-000/backup-002";
    List<String> tL2 = Arrays.asList("t1", "t2", "t3", "t4");
    String keyspace2 = "bar";
    PerBackupLocationKeyspaceTables bL2_KeyspaceTables =
        PerBackupLocationKeyspaceTables.builder()
            .originalKeyspace(keyspace2)
            .tableNameList(tL2)
            .build();
    PerLocationBackupInfo bInfo2 =
        PerLocationBackupInfo.builder()
            .backupLocation(backupLocation_2)
            .perBackupLocationKeyspaceTables(bL2_KeyspaceTables)
            .build();
    Map<String, PerLocationBackupInfo> locationBInfoMap = new HashMap<>();
    locationBInfoMap.put(backupLocation_1, bInfo1);
    locationBInfoMap.put(backupLocation_2, bInfo2);

    // Case 2: Partial restore
    BackupStorageInfo rInfo_1 = new BackupStorageInfo();
    rInfo_1.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_1.storageLocation = backupLocation_1;
    rInfo_1.keyspace = "foo_r";
    rInfo_1.selectiveTableRestore = true;
    rInfo_1.tableNameList = Arrays.asList("t1", "t2");
    BackupStorageInfo rInfo_2 = new BackupStorageInfo();
    rInfo_2.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_2.storageLocation = backupLocation_2;
    rInfo_2.keyspace = "bar_r";
    List<BackupStorageInfo> bInfosList = Arrays.asList(rInfo_1, rInfo_2);
    Map<TableType, Map<String, Set<String>>> restoreMap =
        YbcBackupUtil.generateMapToRestoreNonRedisYBC(bInfosList, locationBInfoMap);
    Map<String, Set<String>> ycqlRestore = restoreMap.get(TableType.YQL_TABLE_TYPE);

    assertTrue(ycqlRestore.size() == 2);
    assertTrue(restoreMap.get(TableType.PGSQL_TABLE_TYPE).size() == 0);
    assertTrue(ycqlRestore.keySet().containsAll(Arrays.asList(rInfo_1.keyspace, rInfo_2.keyspace)));
    @SuppressWarnings("unchecked")
    Collection<String> intersectionList =
        CollectionUtils.intersection(tL1, ycqlRestore.get(rInfo_1.keyspace));
    assertTrue(CollectionUtils.isEqualCollection(intersectionList, rInfo_1.tableNameList));
    assertTrue(ycqlRestore.get(rInfo_2.keyspace).containsAll(tL2));
  }

  @Test(expected = Test.None.class)
  public void testGeneratedMapToRestoreYCQLTableByTable() {
    String backupLocation_1 = "s3://foo/univ-000/backup-001";
    List<String> tL1 = Arrays.asList("t1");
    String backupLocation_2 = "s3://foo/univ-000/backup-002";
    List<String> tL2 = Arrays.asList("t2");
    String backupLocation_3 = "s3://foo/univ-000/backup-003";
    List<String> tL3 = Arrays.asList("t3");
    String keyspace1 = "foo";
    PerBackupLocationKeyspaceTables bL1_KeyspaceTables =
        PerBackupLocationKeyspaceTables.builder()
            .originalKeyspace(keyspace1)
            .tableNameList(tL1)
            .build();
    PerBackupLocationKeyspaceTables bL2_KeyspaceTables =
        PerBackupLocationKeyspaceTables.builder()
            .originalKeyspace(keyspace1)
            .tableNameList(tL2)
            .build();
    PerBackupLocationKeyspaceTables bL3_KeyspaceTables =
        PerBackupLocationKeyspaceTables.builder()
            .originalKeyspace(keyspace1)
            .tableNameList(tL3)
            .build();
    PerLocationBackupInfo bInfo1 =
        PerLocationBackupInfo.builder()
            .backupLocation(backupLocation_1)
            .perBackupLocationKeyspaceTables(bL1_KeyspaceTables)
            .build();
    PerLocationBackupInfo bInfo2 =
        PerLocationBackupInfo.builder()
            .backupLocation(backupLocation_2)
            .perBackupLocationKeyspaceTables(bL2_KeyspaceTables)
            .build();
    PerLocationBackupInfo bInfo3 =
        PerLocationBackupInfo.builder()
            .backupLocation(backupLocation_3)
            .perBackupLocationKeyspaceTables(bL3_KeyspaceTables)
            .build();
    Map<String, PerLocationBackupInfo> locationBInfoMap = new HashMap<>();
    locationBInfoMap.put(backupLocation_1, bInfo1);
    locationBInfoMap.put(backupLocation_2, bInfo2);
    locationBInfoMap.put(backupLocation_3, bInfo3);

    // Case: Table by Table backup restore
    BackupStorageInfo rInfo_1 = new BackupStorageInfo();
    rInfo_1.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_1.storageLocation = backupLocation_1;
    rInfo_1.keyspace = "foo_r";
    BackupStorageInfo rInfo_2 = new BackupStorageInfo();
    rInfo_2.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_2.storageLocation = backupLocation_2;
    rInfo_2.keyspace = "foo_r";
    BackupStorageInfo rInfo_3 = new BackupStorageInfo();
    rInfo_3.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_3.storageLocation = backupLocation_3;
    rInfo_3.keyspace = "foo_r";
    List<BackupStorageInfo> bInfosList = Arrays.asList(rInfo_1, rInfo_2, rInfo_3);
    Map<TableType, Map<String, Set<String>>> restoreMap =
        YbcBackupUtil.generateMapToRestoreNonRedisYBC(bInfosList, locationBInfoMap);
    Map<String, Set<String>> ycqlRestore = restoreMap.get(TableType.YQL_TABLE_TYPE);

    assertTrue(ycqlRestore.size() == 1);
    assertTrue(ycqlRestore.get("foo_r").size() == 3);
    assertTrue(restoreMap.get(TableType.PGSQL_TABLE_TYPE).size() == 0);
  }

  @Test(expected = Test.None.class)
  public void testGenerateMapToRestoreYCQLSameBackupLocationNoOverwrite() {
    String backupLocation_1 = "s3://foo/univ-000/backup-001";
    List<String> tL1 = Arrays.asList("t1", "t2", "t3", "t4");
    String keyspace1 = "foo";
    PerBackupLocationKeyspaceTables bL1_KeyspaceTables =
        PerBackupLocationKeyspaceTables.builder()
            .originalKeyspace(keyspace1)
            .tableNameList(tL1)
            .build();
    PerLocationBackupInfo bInfo1 =
        PerLocationBackupInfo.builder()
            .backupLocation(backupLocation_1)
            .perBackupLocationKeyspaceTables(bL1_KeyspaceTables)
            .build();
    Map<String, PerLocationBackupInfo> locationBInfoMap = new HashMap<>();
    locationBInfoMap.put(backupLocation_1, bInfo1);

    // Case 2: Partial restore
    BackupStorageInfo rInfo_1 = new BackupStorageInfo();
    rInfo_1.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_1.storageLocation = backupLocation_1;
    rInfo_1.keyspace = "foo_r";
    rInfo_1.selectiveTableRestore = true;
    rInfo_1.tableNameList = Arrays.asList("t3", "t4");
    BackupStorageInfo rInfo_2 = new BackupStorageInfo();
    rInfo_2.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_2.storageLocation = backupLocation_1;
    rInfo_2.keyspace = "foo_r";
    rInfo_2.selectiveTableRestore = true;
    rInfo_2.tableNameList = Arrays.asList("t1", "t2");
    List<BackupStorageInfo> bInfosList = Arrays.asList(rInfo_1, rInfo_2);
    Map<TableType, Map<String, Set<String>>> restoreMap =
        YbcBackupUtil.generateMapToRestoreNonRedisYBC(bInfosList, locationBInfoMap);
    Map<String, Set<String>> ycqlRestore = restoreMap.get(TableType.YQL_TABLE_TYPE);

    assertTrue(ycqlRestore.size() == 1);
    assertTrue(restoreMap.get(TableType.PGSQL_TABLE_TYPE).size() == 0);
    assertTrue(CollectionUtils.isEqualCollection(tL1, ycqlRestore.get("foo_r")));
  }

  @Test(expected = Test.None.class)
  public void testGenerateMapToRestoreYCQLDifferentLocationSameKeyspaceNoOverwrite() {
    String backupLocation_1 = "s3://foo/univ-000/backup-001";
    List<String> tL1 = Arrays.asList("t1", "t2");
    String keyspace1 = "foo_1";
    PerBackupLocationKeyspaceTables bL1_KeyspaceTables =
        PerBackupLocationKeyspaceTables.builder()
            .originalKeyspace(keyspace1)
            .tableNameList(tL1)
            .build();
    PerLocationBackupInfo bInfo1 =
        PerLocationBackupInfo.builder()
            .backupLocation(backupLocation_1)
            .perBackupLocationKeyspaceTables(bL1_KeyspaceTables)
            .build();
    String backupLocation_2 = "s3://foo/univ-000/backup-002";
    List<String> tL2 = Arrays.asList("t3", "t4");
    String keyspace2 = "foo_2";
    PerBackupLocationKeyspaceTables bL2_KeyspaceTables =
        PerBackupLocationKeyspaceTables.builder()
            .originalKeyspace(keyspace2)
            .tableNameList(tL2)
            .build();
    PerLocationBackupInfo bInfo2 =
        PerLocationBackupInfo.builder()
            .backupLocation(backupLocation_2)
            .perBackupLocationKeyspaceTables(bL2_KeyspaceTables)
            .build();

    Map<String, PerLocationBackupInfo> locationBInfoMap = new HashMap<>();
    locationBInfoMap.put(backupLocation_1, bInfo1);
    locationBInfoMap.put(backupLocation_2, bInfo2);

    // Case 2: Partial restore
    BackupStorageInfo rInfo_1 = new BackupStorageInfo();
    rInfo_1.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_1.storageLocation = backupLocation_1;
    rInfo_1.keyspace = "foo_r";
    rInfo_1.tableNameList = Arrays.asList("t1", "t2");
    BackupStorageInfo rInfo_2 = new BackupStorageInfo();
    rInfo_2.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_2.storageLocation = backupLocation_2;
    rInfo_2.keyspace = "foo_r";
    rInfo_2.tableNameList = Arrays.asList("t3", "t4");
    List<BackupStorageInfo> bInfosList = Arrays.asList(rInfo_1, rInfo_2);
    Map<TableType, Map<String, Set<String>>> restoreMap =
        YbcBackupUtil.generateMapToRestoreNonRedisYBC(bInfosList, locationBInfoMap);
    Map<String, Set<String>> ycqlRestore = restoreMap.get(TableType.YQL_TABLE_TYPE);

    assertTrue(ycqlRestore.size() == 1);
    assertTrue(restoreMap.get(TableType.PGSQL_TABLE_TYPE).size() == 0);
    // Should contain all tables
    assertTrue(ycqlRestore.get("foo_r").size() == 4);
  }

  @Test
  public void testGenerateMapToRestoreYCQLDifferentKeyspaceSameTablesOverwrite() {
    String backupLocation_1 = "s3://foo/univ-000/backup-001";
    List<String> tL1 = Arrays.asList("t1", "t2");
    String keyspace1 = "foo_1";
    PerBackupLocationKeyspaceTables bL1_KeyspaceTables =
        PerBackupLocationKeyspaceTables.builder()
            .originalKeyspace(keyspace1)
            .tableNameList(tL1)
            .build();
    PerLocationBackupInfo bInfo1 =
        PerLocationBackupInfo.builder()
            .backupLocation(backupLocation_1)
            .perBackupLocationKeyspaceTables(bL1_KeyspaceTables)
            .build();
    String backupLocation_2 = "s3://foo/univ-000/backup-002";
    List<String> tL2 = Arrays.asList("t1", "t2");
    String keyspace2 = "foo_2";
    PerBackupLocationKeyspaceTables bL2_KeyspaceTables =
        PerBackupLocationKeyspaceTables.builder()
            .originalKeyspace(keyspace2)
            .tableNameList(tL2)
            .build();
    PerLocationBackupInfo bInfo2 =
        PerLocationBackupInfo.builder()
            .backupLocation(backupLocation_2)
            .perBackupLocationKeyspaceTables(bL2_KeyspaceTables)
            .build();

    Map<String, PerLocationBackupInfo> locationBInfoMap = new HashMap<>();
    locationBInfoMap.put(backupLocation_1, bInfo1);
    locationBInfoMap.put(backupLocation_2, bInfo2);

    // Case 2: Partial restore
    BackupStorageInfo rInfo_1 = new BackupStorageInfo();
    rInfo_1.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_1.storageLocation = backupLocation_1;
    rInfo_1.keyspace = "foo_r";
    BackupStorageInfo rInfo_2 = new BackupStorageInfo();
    rInfo_2.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_2.storageLocation = backupLocation_2;
    rInfo_2.keyspace = "foo_r";
    List<BackupStorageInfo> bInfosList = Arrays.asList(rInfo_1, rInfo_2);
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> YbcBackupUtil.generateMapToRestoreNonRedisYBC(bInfosList, locationBInfoMap));
    assertTrue(ex.getMessage().equals("Overwrite of data attempted for YCQL keyspace foo_r"));
  }

  @Test
  public void testGenerateMapToRestoreYSQLRestoreNoOverwrite() {
    String backupLocation_1 = "s3://foo/univ-000/backup-001";
    String keyspace1 = "foo";
    PerBackupLocationKeyspaceTables bL1_KeyspaceTables =
        PerBackupLocationKeyspaceTables.builder().originalKeyspace(keyspace1).build();
    PerLocationBackupInfo bInfo1 =
        PerLocationBackupInfo.builder()
            .backupLocation(backupLocation_1)
            .isYSQLBackup(true)
            .perBackupLocationKeyspaceTables(bL1_KeyspaceTables)
            .build();
    Map<String, PerLocationBackupInfo> locationBInfoMap = new HashMap<>();
    locationBInfoMap.put(backupLocation_1, bInfo1);

    // Only case: Repeated DB name to restore.
    BackupStorageInfo rInfo_1 = new BackupStorageInfo();
    rInfo_1.backupType = TableType.PGSQL_TABLE_TYPE;
    rInfo_1.storageLocation = backupLocation_1;
    rInfo_1.keyspace = "foo_r";
    BackupStorageInfo rInfo_2 = new BackupStorageInfo();
    rInfo_2.backupType = TableType.PGSQL_TABLE_TYPE;
    rInfo_2.storageLocation = backupLocation_1;
    rInfo_2.keyspace = "bar_r";
    List<BackupStorageInfo> bInfosList = Arrays.asList(rInfo_1, rInfo_2);
    Map<TableType, Map<String, Set<String>>> restoreMap =
        YbcBackupUtil.generateMapToRestoreNonRedisYBC(bInfosList, locationBInfoMap);
    Map<String, Set<String>> ysqlRestore = restoreMap.get(TableType.PGSQL_TABLE_TYPE);
    assertTrue(ysqlRestore.size() == 2);
    assertTrue(restoreMap.get(TableType.YQL_TABLE_TYPE).size() == 0);
    assertTrue(ysqlRestore.keySet().containsAll(Arrays.asList(rInfo_1.keyspace, rInfo_2.keyspace)));
  }

  @Test
  public void testGenerateMapToRestoreYCQLPartialRestoreOverwrite() {
    String backupLocation_1 = "s3://foo/univ-000/backup-001";
    List<String> tL1 = Arrays.asList("t1", "t2", "t3", "t4");
    String keyspace1 = "foo";
    PerBackupLocationKeyspaceTables bL1_KeyspaceTables =
        PerBackupLocationKeyspaceTables.builder()
            .originalKeyspace(keyspace1)
            .tableNameList(tL1)
            .build();
    PerLocationBackupInfo bInfo1 =
        PerLocationBackupInfo.builder()
            .backupLocation(backupLocation_1)
            .perBackupLocationKeyspaceTables(bL1_KeyspaceTables)
            .build();
    Map<String, PerLocationBackupInfo> locationBInfoMap = new HashMap<>();
    locationBInfoMap.put(backupLocation_1, bInfo1);

    // Case 1: Same keyspace and repeating table "t3"
    BackupStorageInfo rInfo_1 = new BackupStorageInfo();
    rInfo_1.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_1.storageLocation = backupLocation_1;
    rInfo_1.keyspace = "foo_r";
    rInfo_1.tableNameList = Arrays.asList("t3", "t4");
    BackupStorageInfo rInfo_2 = new BackupStorageInfo();
    rInfo_2.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_2.storageLocation = backupLocation_1;
    rInfo_2.keyspace = "foo_r";
    rInfo_2.tableNameList = Arrays.asList("t1", "t2", "t3");
    List<BackupStorageInfo> bInfosList_1 = Arrays.asList(rInfo_1, rInfo_2);
    assertThrows(
        "Overwrite of data attempted for keyspace foo_r",
        PlatformServiceException.class,
        () -> YbcBackupUtil.generateMapToRestoreNonRedisYBC(bInfosList_1, locationBInfoMap));

    // Case 2: Same keyspace and no table provided in first sub-request
    BackupStorageInfo rInfo_3 = new BackupStorageInfo();
    rInfo_3.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_3.storageLocation = backupLocation_1;
    rInfo_3.keyspace = "foo_r";
    BackupStorageInfo rInfo_4 = new BackupStorageInfo();
    rInfo_4.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_4.storageLocation = backupLocation_1;
    rInfo_4.keyspace = "foo_r";
    rInfo_4.tableNameList = Arrays.asList("t1", "t2", "t3");
    List<BackupStorageInfo> bInfosList_2 = Arrays.asList(rInfo_3, rInfo_4);
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> YbcBackupUtil.generateMapToRestoreNonRedisYBC(bInfosList_2, locationBInfoMap));
    assertTrue(ex.getMessage().equals("Overwrite of data attempted for YCQL keyspace foo_r"));

    // Case 3: Same keyspace and no table provided in second sub-request
    BackupStorageInfo rInfo_5 = new BackupStorageInfo();
    rInfo_5.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_5.storageLocation = backupLocation_1;
    rInfo_5.keyspace = "foo_r";
    rInfo_5.tableNameList = Arrays.asList("t1", "t2", "t3");
    BackupStorageInfo rInfo_6 = new BackupStorageInfo();
    rInfo_6.backupType = TableType.YQL_TABLE_TYPE;
    rInfo_6.storageLocation = backupLocation_1;
    rInfo_6.keyspace = "foo_r";
    List<BackupStorageInfo> bInfosList_3 = Arrays.asList(rInfo_5, rInfo_6);
    ex =
        assertThrows(
            PlatformServiceException.class,
            () -> YbcBackupUtil.generateMapToRestoreNonRedisYBC(bInfosList_3, locationBInfoMap));
    assertTrue(ex.getMessage().equals("Overwrite of data attempted for YCQL keyspace foo_r"));
  }

  @Test
  public void testGenerateMapToRestoreYSQLRestoreOverwrite() {
    String backupLocation_1 = "s3://foo/univ-000/backup-001";
    String keyspace1 = "foo";
    PerBackupLocationKeyspaceTables bL1_KeyspaceTables =
        PerBackupLocationKeyspaceTables.builder().originalKeyspace(keyspace1).build();
    PerLocationBackupInfo bInfo1 =
        PerLocationBackupInfo.builder()
            .backupLocation(backupLocation_1)
            .isYSQLBackup(true)
            .perBackupLocationKeyspaceTables(bL1_KeyspaceTables)
            .build();
    Map<String, PerLocationBackupInfo> locationBInfoMap = new HashMap<>();
    locationBInfoMap.put(backupLocation_1, bInfo1);

    // Only case: Repeated DB name to restore.
    BackupStorageInfo rInfo_1 = new BackupStorageInfo();
    rInfo_1.backupType = TableType.PGSQL_TABLE_TYPE;
    rInfo_1.storageLocation = backupLocation_1;
    rInfo_1.keyspace = "foo_r";
    BackupStorageInfo rInfo_2 = new BackupStorageInfo();
    rInfo_2.backupType = TableType.PGSQL_TABLE_TYPE;
    rInfo_2.storageLocation = backupLocation_1;
    rInfo_2.keyspace = "foo_r";
    List<BackupStorageInfo> bInfosList_1 = Arrays.asList(rInfo_1, rInfo_2);
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> YbcBackupUtil.generateMapToRestoreNonRedisYBC(bInfosList_1, locationBInfoMap));
    assertTrue(ex.getMessage().equals("Overwrite of data attempted for YSQL keyspace foo_r"));
  }

  @Test
  @Parameters({"backup/ybc_success_file_with_index_tables.json"})
  public void testGetTableRestoreSpec(String filePath) {
    String successStr = TestUtils.readResource(filePath);
    YbcBackupResponse backupResponse = YbcBackupUtil.parseYbcBackupResponse(successStr);
    List<String> tablesInSuccessFile =
        backupResponse.snapshotObjectDetails.stream()
            .filter(sOD -> sOD.type.equals(SnapshotObjectType.TABLE))
            .map(sOD -> sOD.data.snapshotObjectName)
            .collect(Collectors.toList());
    List<String> nonIndexTablesInSuccessFile =
        backupResponse.snapshotObjectDetails.stream()
            .filter(
                sOD ->
                    sOD.type.equals(SnapshotObjectType.TABLE)
                        && ((TableData) sOD.data).indexedTableID == null)
            .map(sOD -> sOD.data.snapshotObjectName)
            .collect(Collectors.toList());

    BackupStorageInfo bSInfo = new BackupStorageInfo();
    bSInfo.tableNameList = new ArrayList<>(nonIndexTablesInSuccessFile);
    bSInfo.keyspace = "foo";
    bSInfo.backupType = TableType.YQL_TABLE_TYPE;
    TableRestoreSpec tRSpec = YbcBackupUtil.getTableRestoreSpec(backupResponse, bSInfo);
    assertTrue(tRSpec.getKeyspace().equals(bSInfo.keyspace));
    assertTrue(
        tRSpec.getTableList().parallelStream()
            .collect(Collectors.toList())
            .containsAll(tablesInSuccessFile));
  }
}
