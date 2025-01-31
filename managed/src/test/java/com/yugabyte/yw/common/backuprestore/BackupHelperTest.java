package com.yugabyte.yw.common.backuprestore;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.google.protobuf.ByteString;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.backuprestore.BackupUtil.PerBackupLocationKeyspaceTables;
import com.yugabyte.yw.common.backuprestore.BackupUtil.PerLocationBackupInfo;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.replication.ValidateReplicationInfo;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.BackupRequestParams.KeyspaceTable;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestorePreflightParams;
import com.yugabyte.yw.forms.RestorePreflightResponse;
import com.yugabyte.yw.forms.backuprestore.AdvancedRestorePreflightParams;
import com.yugabyte.yw.forms.backuprestore.KeyspaceTables;
import com.yugabyte.yw.forms.backuprestore.RestoreItemsValidationParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupVersion;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageData;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.MapUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;
import org.yb.CommonTypes.TableType;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;
import org.yb.master.MasterTypes.NamespaceIdentifierPB;
import org.yb.ybc.BackupServiceTaskEnabledFeaturesResponse;
import org.yb.ybc.CloudStoreSpec;

@Slf4j
@RunWith(JUnitParamsRunner.class)
public class BackupHelperTest extends FakeDBApplication {

  private Universe testUniverse;
  private Customer testCustomer;
  private BackupHelper spyBackupHelper;
  private CustomerConfigService mockConfigService;
  private RuntimeConfGetter mockRuntimeConfGetter;
  private ValidateReplicationInfo mockValidateReplicationInfo;
  private NodeUniverseManager mockNodeUniverseManager;
  private YbcBackupUtil mockYbcBackupUtil;

  @Before
  public void setup() {
    testCustomer = ModelFactory.testCustomer();
    testUniverse = ModelFactory.createUniverse(testCustomer.getId());
    mockConfigService = mock(CustomerConfigService.class);
    mockRuntimeConfGetter = mock(RuntimeConfGetter.class);
    mockCommissioner = mock(Commissioner.class);
    mockValidateReplicationInfo = mock(ValidateReplicationInfo.class);
    mockNodeUniverseManager = mock(NodeUniverseManager.class);
    mockYbcBackupUtil = mock(YbcBackupUtil.class);
    spyBackupHelper =
        Mockito.spy(
            new BackupHelper(
                mockYbcManager,
                mockService,
                mockConfigService,
                mockRuntimeConfGetter,
                mockStorageUtilFactory,
                mockCommissioner,
                mockValidateReplicationInfo,
                mockNodeUniverseManager,
                mockYbcBackupUtil));
    when(mockStorageUtilFactory.getStorageUtil(eq("S3"))).thenReturn(mockAWSUtil);
    when(mockStorageUtilFactory.getStorageUtil(eq("NFS"))).thenReturn(mockNfsUtil);
  }

  private TableInfo getTableInfoYCQL(UUID tableUUID, String tableName, String keyspace) {
    return TableInfo.newBuilder()
        .setName(tableName)
        .setTableType(TableType.YQL_TABLE_TYPE)
        .setId(ByteString.copyFromUtf8(tableUUID.toString()))
        .setNamespace(
            NamespaceIdentifierPB.newBuilder()
                .setId(ByteString.copyFromUtf8(tableUUID.toString()))
                .setDatabaseType(YQLDatabase.YQL_DATABASE_CQL)
                .setName(keyspace)
                .build())
        .build();
  }

  private TableInfo getTableInfoYSQL(UUID tableUUID, String tableName, String keyspace) {
    return TableInfo.newBuilder()
        .setName(tableName)
        .setTableType(TableType.PGSQL_TABLE_TYPE)
        .setId(ByteString.copyFromUtf8(tableUUID.toString()))
        .setNamespace(
            NamespaceIdentifierPB.newBuilder()
                .setId(ByteString.copyFromUtf8(tableUUID.toString()))
                .setDatabaseType(YQLDatabase.YQL_DATABASE_PGSQL)
                .setName(keyspace)
                .build())
        .build();
  }

  @Test(expected = Test.None.class)
  public void testValidateBackupRequestYCQLSameKeyspaceValid() {
    List<KeyspaceTable> kTList = new ArrayList<>();
    KeyspaceTable kT1 = new KeyspaceTable();
    kT1.keyspace = "foo";
    UUID tableUUID1 = UUID.randomUUID();
    kT1.tableUUIDList.add(tableUUID1);
    kTList.add(kT1);

    KeyspaceTable kT2 = new KeyspaceTable();
    kT2.keyspace = "foo";
    UUID tableUUID2 = UUID.randomUUID();
    kT2.tableUUIDList.add(tableUUID2);
    kTList.add(kT2);
    UUID tableUUID3 = UUID.randomUUID();
    kT2.tableUUIDList.add(tableUUID3);

    List<TableInfo> ybClientTableList = new ArrayList<>();
    ybClientTableList.add(getTableInfoYCQL(tableUUID1, "table_1", "foo"));
    ybClientTableList.add(getTableInfoYCQL(tableUUID2, "table_2", "foo"));
    ybClientTableList.add(getTableInfoYCQL(tableUUID3, "table_3", "foo"));
    doReturn(ybClientTableList).when(spyBackupHelper).getTableInfosOrEmpty(any());
    doNothing().when(spyBackupHelper).validateTables(any(), any(), anyString(), any());
    spyBackupHelper.validateBackupRequest(kTList, testUniverse, TableType.YQL_TABLE_TYPE);
  }

  @Test(expected = Test.None.class)
  public void testValidateBackupRequestYCQLDifferentKeyspaceValid() {
    List<KeyspaceTable> kTList = new ArrayList<>();
    KeyspaceTable kT1 = new KeyspaceTable();
    kT1.keyspace = "foo";
    UUID tableUUID1 = UUID.randomUUID();
    kT1.tableUUIDList.add(tableUUID1);
    kTList.add(kT1);

    KeyspaceTable kT2 = new KeyspaceTable();
    kT2.keyspace = "bar";
    UUID tableUUID2 = UUID.randomUUID();
    kT2.tableUUIDList.add(tableUUID2);
    kTList.add(kT2);

    List<TableInfo> ybClientTableList = new ArrayList<>();
    ybClientTableList.add(getTableInfoYCQL(tableUUID1, "table_1", "foo"));
    ybClientTableList.add(getTableInfoYCQL(tableUUID2, "table_1", "bar"));
    doReturn(ybClientTableList).when(spyBackupHelper).getTableInfosOrEmpty(any());
    doNothing().when(spyBackupHelper).validateTables(any(), any(), anyString(), any());
    spyBackupHelper.validateBackupRequest(kTList, testUniverse, TableType.YQL_TABLE_TYPE);
  }

  @Test
  public void testValidateBackupRequestYCQLSameKeyspaceInvalid() {
    List<KeyspaceTable> kTList = new ArrayList<>();
    KeyspaceTable kT1 = new KeyspaceTable();
    kT1.keyspace = "foo";
    UUID tableUUID1 = UUID.randomUUID();
    kT1.tableUUIDList = Arrays.asList(tableUUID1);
    kTList.add(kT1);

    KeyspaceTable kT2 = new KeyspaceTable();
    kT2.keyspace = "foo";
    UUID tableUUID2 = UUID.randomUUID();
    kT2.tableUUIDList.add(tableUUID2);
    kTList.add(kT2);
    kT2.tableUUIDList.add(tableUUID1);

    List<TableInfo> ybClientTableList = new ArrayList<>();
    ybClientTableList.add(getTableInfoYCQL(tableUUID1, "table_1", "foo"));
    ybClientTableList.add(getTableInfoYCQL(tableUUID2, "table_2", "foo"));
    doReturn(ybClientTableList).when(spyBackupHelper).getTableInfosOrEmpty(any());
    doNothing().when(spyBackupHelper).validateTables(any(), any(), anyString(), any());

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () ->
                spyBackupHelper.validateBackupRequest(
                    kTList, testUniverse, TableType.YQL_TABLE_TYPE));
    assertTrue(ex.getMessage().contains("Repeated tables in backup request for keyspace"));
  }

  @Test
  @Parameters({
    "true, YB_CONTROLLER",
    "false, YB_CONTROLLER",
    "true, YB_BACKUP_SCRIPT",
    "false, YB_BACKUP_SCRIPT"
  })
  public void testRestorePreflightResponseWithYCQLBackupObjectExists(
      boolean isKMS, BackupCategory category) {
    Universe testYbcUniverse =
        ModelFactory.createUniverse(
            "test4", UUID.randomUUID(), testCustomer.getId(), CloudType.aws, null, null, true);
    CustomerConfig testStorageConfigS3 =
        ModelFactory.createS3StorageConfig(testCustomer, "test_S3");
    BackupTableParams parentBTableParams = new BackupTableParams();
    parentBTableParams.setUniverseUUID(testYbcUniverse.getUniverseUUID());
    parentBTableParams.customerUuid = testCustomer.getUuid();
    parentBTableParams.storageConfigUUID = testStorageConfigS3.getConfigUUID();

    List<UUID> tableUUIDList = Arrays.asList(UUID.randomUUID(), UUID.randomUUID());
    List<String> tableNameList = Arrays.asList("table_1", "table_2");
    BackupTableParams childBTableParams = new BackupTableParams();
    childBTableParams.tableNameList = tableNameList;
    childBTableParams.tableUUIDList = tableUUIDList;
    childBTableParams.setKeyspace("foo");
    childBTableParams.backupType = TableType.YQL_TABLE_TYPE;
    childBTableParams.storageConfigUUID = testStorageConfigS3.getConfigUUID();

    parentBTableParams.backupList = Arrays.asList(childBTableParams);
    Backup backup =
        Backup.create(testCustomer.getUuid(), parentBTableParams, category, BackupVersion.V2);
    if (isKMS) {
      backup.setHasKMSHistory(true);
      backup.save();
    }

    BackupServiceTaskEnabledFeaturesResponse.Builder eFResponseBuilder =
        BackupServiceTaskEnabledFeaturesResponse.newBuilder();
    eFResponseBuilder.setSelectiveTableRestore(true);
    when(mockYbcManager.getEnabledBackupFeatures(any(UUID.class)))
        .thenReturn(eFResponseBuilder.build());

    RestorePreflightParams preflightParams = new RestorePreflightParams();
    preflightParams.setBackupUUID(backup.getBackupUUID());
    // preflightParams.setBackupLocations(
    //     new HashSet<>(Arrays.asList(backup.getBackupInfo().backupList.get(0).storageLocation)));
    // preflightParams.setStorageConfigUUID(testStorageConfigS3.getConfigUUID());
    preflightParams.setUniverseUUID(testYbcUniverse.getUniverseUUID());

    RestorePreflightResponse preflightResponse =
        spyBackupHelper.restorePreflightWithBackupObject(
            testCustomer.getUuid(), backup, preflightParams);

    PerLocationBackupInfo bInfo =
        preflightResponse.getPerLocationBackupInfoMap().values().iterator().next();
    if (category.equals(BackupCategory.YB_BACKUP_SCRIPT)) {
      assertFalse(bInfo.getIsSelectiveRestoreSupported());
    } else {
      assertTrue(bInfo.getIsSelectiveRestoreSupported());
    }
    assertEquals(backup.getCategory(), preflightResponse.getBackupCategory());
    assertTrue(preflightResponse.getHasKMSHistory().equals(isKMS));
    assertFalse(bInfo.getIsYSQLBackup());
    bInfo.getPerBackupLocationKeyspaceTables().getTableNameList().parallelStream()
        .forEach(t -> assertTrue(tableNameList.contains(t)));
    assertTrue(bInfo.getPerBackupLocationKeyspaceTables().getOriginalKeyspace().equals("foo"));
  }

  @Test
  @Parameters({
    "true, YB_CONTROLLER",
    "false, YB_CONTROLLER",
    "true, YB_BACKUP_SCRIPT",
    "false, YB_BACKUP_SCRIPT"
  })
  public void testRestorePreflightResponseWithYSQLBackupObjectExists(
      boolean isKMS, BackupCategory category) {
    Universe testYbcUniverse =
        ModelFactory.createUniverse(
            "test5", UUID.randomUUID(), testCustomer.getId(), CloudType.aws, null, null, true);
    CustomerConfig testStorageConfigS3 =
        ModelFactory.createS3StorageConfig(testCustomer, "test_S3");
    BackupTableParams parentBTableParams = new BackupTableParams();
    parentBTableParams.setUniverseUUID(testYbcUniverse.getUniverseUUID());
    parentBTableParams.customerUuid = testCustomer.getUuid();
    parentBTableParams.storageConfigUUID = testStorageConfigS3.getConfigUUID();

    BackupTableParams childBTableParams = new BackupTableParams();
    childBTableParams.setKeyspace("foo");
    childBTableParams.backupType = TableType.PGSQL_TABLE_TYPE;
    childBTableParams.storageConfigUUID = testStorageConfigS3.getConfigUUID();

    parentBTableParams.backupList = Arrays.asList(childBTableParams);
    Backup backup =
        Backup.create(testCustomer.getUuid(), parentBTableParams, category, BackupVersion.V2);
    if (isKMS) {
      backup.setHasKMSHistory(true);
      backup.save();
    }

    BackupServiceTaskEnabledFeaturesResponse.Builder eFResponseBuilder =
        BackupServiceTaskEnabledFeaturesResponse.newBuilder();
    eFResponseBuilder.setSelectiveTableRestore(true);
    lenient()
        .when(mockYbcManager.getEnabledBackupFeatures(any(UUID.class)))
        .thenReturn(eFResponseBuilder.build());

    RestorePreflightParams preflightParams = new RestorePreflightParams();
    preflightParams.setBackupUUID(backup.getBackupUUID());
    preflightParams.setUniverseUUID(testYbcUniverse.getUniverseUUID());

    RestorePreflightResponse preflightResponse =
        spyBackupHelper.restorePreflightWithBackupObject(
            testCustomer.getUuid(), backup, preflightParams);

    PerLocationBackupInfo bInfo =
        preflightResponse.getPerLocationBackupInfoMap().values().iterator().next();
    assertFalse(bInfo.getIsSelectiveRestoreSupported());
    assertEquals(backup.getCategory(), preflightResponse.getBackupCategory());
    assertTrue(preflightResponse.getHasKMSHistory().equals(isKMS));
    assertTrue(bInfo.getIsYSQLBackup());
    assertTrue(bInfo.getPerBackupLocationKeyspaceTables().getOriginalKeyspace().equals("foo"));
  }

  @Test
  @Parameters({"false, true", "true, false", "true, true", "false, false"})
  public void testRestorePreflightWithoutBackupObjectS3NonYBC(boolean isYSQL, boolean isKMS) {
    CustomerConfig testStorageConfigS3 =
        ModelFactory.createS3StorageConfig(testCustomer, "test_S3");
    String parentDir = "univ-000";
    String suffixDir = "backup-000";
    String backupLocationParent =
        BackupUtil.getPathWithPrefixSuffixJoin(
            ((CustomerConfigStorageData) testStorageConfigS3.getDataObject()).backupLocation,
            parentDir);
    String backupLocationAbsolute =
        BackupUtil.getPathWithPrefixSuffixJoin(backupLocationParent, suffixDir);

    when(mockConfigService.getOrBadRequest(
            eq(testCustomer.getUuid()), eq(testStorageConfigS3.getConfigUUID())))
        .thenCallRealMethod();
    doNothing()
        .when(mockAWSUtil)
        .validateStorageConfigOnDefaultLocationsList(any(), any(), anyBoolean());
    doReturn(false)
        .when(mockAWSUtil)
        .checkFileExists(
            any(),
            argThat(locationSet -> locationSet.contains(backupLocationAbsolute)),
            eq(YbcBackupUtil.YBC_SUCCESS_MARKER_FILE_NAME),
            any(UUID.class),
            anyBoolean());
    doReturn(true)
        .when(mockAWSUtil)
        .checkFileExists(
            any(),
            argThat(locationSet -> locationSet.contains(backupLocationAbsolute)),
            eq(BackupUtil.SNAPSHOT_PB),
            eq(true));

    doReturn(isYSQL ? true : false)
        .when(mockAWSUtil)
        .checkFileExists(
            any(),
            argThat(locationSet -> locationSet.contains(backupLocationAbsolute)),
            eq(BackupUtil.YSQL_DUMP),
            anyBoolean());

    doReturn(isKMS ? true : false)
        .when(mockAWSUtil)
        .checkFileExists(
            any(),
            argThat(locationSet -> locationSet.contains(backupLocationParent)),
            eq(BackupUtil.BACKUP_KEYS_JSON),
            anyBoolean());

    AdvancedRestorePreflightParams preflightParams = new AdvancedRestorePreflightParams();
    preflightParams.setUniverseUUID(testUniverse.getUniverseUUID());
    preflightParams.setStorageConfigUUID(testStorageConfigS3.getConfigUUID());
    preflightParams.setBackupLocations(new HashSet<String>(Arrays.asList(backupLocationAbsolute)));
    when(mockAWSUtil.generateYBBackupRestorePreflightResponseWithoutBackupObject(
            any(AdvancedRestorePreflightParams.class), any(CustomerConfigData.class)))
        .thenCallRealMethod();
    RestorePreflightResponse preflightResponse =
        spyBackupHelper.generateAdvancedRestorePreflightAPIResponse(
            preflightParams, testCustomer.getUuid());

    assertEquals(preflightResponse.getHasKMSHistory(), isKMS);
    PerLocationBackupInfo bInfo =
        preflightResponse.getPerLocationBackupInfoMap().values().iterator().next();
    assertEquals(bInfo.getIsYSQLBackup(), isYSQL);
    assertFalse(bInfo.getIsSelectiveRestoreSupported());
    assertTrue(preflightResponse.getBackupCategory().equals(BackupCategory.YB_BACKUP_SCRIPT));
  }

  @Test
  @Parameters({"false, true", "true, false", "true, true", "false, false"})
  public void testRestorePreflightWithoutBackupObjectNFSNonYBC(boolean isYSQL, boolean isKMS) {
    CustomerConfig testStorageConfigNFS =
        ModelFactory.createNfsStorageConfig(testCustomer, "test_NFS");
    String parentDir = "univ-000";
    String suffixDir = "backup-000";
    String backupLocationParent =
        BackupUtil.getPathWithPrefixSuffixJoin(
            ((CustomerConfigStorageData) testStorageConfigNFS.getDataObject()).backupLocation,
            parentDir);
    String backupLocationAbsolute =
        BackupUtil.getPathWithPrefixSuffixJoin(backupLocationParent, suffixDir);

    when(mockConfigService.getOrBadRequest(
            eq(testCustomer.getUuid()), eq(testStorageConfigNFS.getConfigUUID())))
        .thenCallRealMethod();
    doNothing()
        .when(mockNfsUtil)
        .validateStorageConfigOnDefaultLocationsList(any(), any(), anyBoolean());
    doReturn(false)
        .when(mockNfsUtil)
        .checkFileExists(
            any(),
            argThat(locationSet -> locationSet.contains(backupLocationAbsolute)),
            eq(YbcBackupUtil.YBC_SUCCESS_MARKER_FILE_NAME),
            any(UUID.class),
            anyBoolean());

    Map<String, Boolean> bulkCheckResultMap = new HashMap<>();
    bulkCheckResultMap.put(
        BackupUtil.getPathWithPrefixSuffixJoin(backupLocationAbsolute, BackupUtil.SNAPSHOT_PB),
        true);
    bulkCheckResultMap.put(
        BackupUtil.getPathWithPrefixSuffixJoin(backupLocationAbsolute, BackupUtil.YSQL_DUMP),
        isYSQL ? true : false);
    bulkCheckResultMap.put(
        BackupUtil.getPathWithPrefixSuffixJoin(backupLocationParent, BackupUtil.BACKUP_KEYS_JSON),
        isKMS ? true : false);
    doReturn(bulkCheckResultMap)
        .when(mockNfsUtil)
        .bulkCheckFilesExistWithAbsoluteLocations(
            any(Universe.class),
            argThat(locationList -> locationList.containsAll(bulkCheckResultMap.keySet())));

    AdvancedRestorePreflightParams preflightParams = new AdvancedRestorePreflightParams();
    preflightParams.setUniverseUUID(testUniverse.getUniverseUUID());
    preflightParams.setStorageConfigUUID(testStorageConfigNFS.getConfigUUID());
    preflightParams.setBackupLocations(new HashSet<String>(Arrays.asList(backupLocationAbsolute)));
    when(mockNfsUtil.generateYBBackupRestorePreflightResponseWithoutBackupObject(
            any(AdvancedRestorePreflightParams.class), any(CustomerConfigData.class)))
        .thenCallRealMethod();
    when(mockNfsUtil.generateYBBackupRestorePreflightResponseWithoutBackupObject(
            any(AdvancedRestorePreflightParams.class)))
        .thenCallRealMethod();
    RestorePreflightResponse preflightResponse =
        spyBackupHelper.generateAdvancedRestorePreflightAPIResponse(
            preflightParams, testCustomer.getUuid());

    assertEquals(preflightResponse.getHasKMSHistory(), isKMS);
    PerLocationBackupInfo bInfo =
        preflightResponse.getPerLocationBackupInfoMap().values().iterator().next();
    assertEquals(bInfo.getIsYSQLBackup(), isYSQL);
    assertFalse(bInfo.getIsSelectiveRestoreSupported());
    assertTrue(preflightResponse.getBackupCategory().equals(BackupCategory.YB_BACKUP_SCRIPT));
  }

  @Test
  @Parameters({
    "backup/ybc_success_file_with_index_tables.json, true, false",
    "backup/ybc_success_file_with_index_tables.json, true, true",
    "backup/ybc_success_file_with_index_tables.json, false, true",
    "backup/ybc_success_file_with_index_tables.json, false, false"
  })
  public void testRestorePreflightWithoutBackupObjectYBCWithYCQLIndexTables(
      String successFilePath, boolean selectiveRestoreYbcResponse, boolean filterIndexes) {
    CustomerConfig testStorageConfigS3 =
        ModelFactory.createS3StorageConfig(testCustomer, "test_S3");
    String suffixDir = "univ-000/backup-000";
    String backupLocation =
        BackupUtil.getPathWithPrefixSuffixJoin(
            ((CustomerConfigStorageData) testStorageConfigS3.getDataObject()).backupLocation,
            suffixDir);
    AdvancedRestorePreflightParams preflightParams = new AdvancedRestorePreflightParams();
    preflightParams.setBackupLocations(new HashSet<>(Arrays.asList(backupLocation)));
    preflightParams.setUniverseUUID(testUniverse.getUniverseUUID());
    preflightParams.setStorageConfigUUID(testStorageConfigS3.getConfigUUID());
    String successStr = TestUtils.readResource(successFilePath);

    when(mockAWSUtil.createDsmCloudStoreSpec(anyString(), any(CustomerConfigData.class), any()))
        .thenReturn(CloudStoreSpec.getDefaultInstance());
    when(mockYbcManager.downloadSuccessMarker(any(), any(UUID.class), anyString()))
        .thenReturn(successStr);
    when(mockYbcManager.getEnabledBackupFeatures(any(UUID.class)))
        .thenReturn(
            BackupServiceTaskEnabledFeaturesResponse.newBuilder()
                .setSelectiveTableRestore(selectiveRestoreYbcResponse)
                .build());
    doNothing()
        .when(spyBackupHelper)
        .validateStorageConfigForSuccessMarkerDownloadOnUniverse(any(), any(), any());
    RestorePreflightResponse preflightResponse =
        spyBackupHelper.generateYBCRestorePreflightResponseWithoutBackupObject(
            preflightParams, testStorageConfigS3, filterIndexes, testUniverse);

    List<String> expectedNonIndexTables =
        Arrays.asList("batch_ts_metrics_raw", "cassandrasecondaryindex");
    String indexTable = "cassandrasecondaryindexbyvalue";
    String parentTable = expectedNonIndexTables.get(1);
    PerLocationBackupInfo bInfo =
        preflightResponse.getPerLocationBackupInfoMap().values().iterator().next();
    assertTrue(preflightResponse.getBackupCategory().equals(BackupCategory.YB_CONTROLLER));
    // This ybc success file resource has KMS history.
    assertTrue(preflightResponse.getHasKMSHistory());
    assertTrue(
        bInfo
            .getPerBackupLocationKeyspaceTables()
            .getTableNameList()
            .containsAll(expectedNonIndexTables));
    if (!filterIndexes) {
      assertTrue(
          MapUtils.isNotEmpty(
              bInfo.getPerBackupLocationKeyspaceTables().getTablesWithIndexesMap()));
      assertTrue(
          bInfo
              .getPerBackupLocationKeyspaceTables()
              .getTablesWithIndexesMap()
              .get(parentTable)
              .contains(indexTable));
    } else {
      assertFalse(
          MapUtils.isNotEmpty(
              bInfo.getPerBackupLocationKeyspaceTables().getTablesWithIndexesMap()));
    }
    assertEquals(bInfo.getIsSelectiveRestoreSupported(), selectiveRestoreYbcResponse);
  }

  @Test(expected = Test.None.class)
  public void testValidateMapToRestoreWithUniverseNonRedisYBC_NoOverwriteYCQL() {
    Map<TableType, Map<String, Set<String>>> restoreMap = new HashMap<>();
    restoreMap.put(TableType.YQL_TABLE_TYPE, new HashMap<String, Set<String>>());
    String keyspace_1 = "foo";
    Set<String> tableNameSet_1 = new HashSet<>(Arrays.asList("t1", "t2", "t3", "t4"));
    restoreMap.get(TableType.YQL_TABLE_TYPE).put(keyspace_1, tableNameSet_1);

    // Case 1: YCQL table with same name exists but in different keyspace
    TableInfo t1 = getTableInfoYCQL(UUID.randomUUID(), "t1", "bar");
    List<TableInfo> ybClientTableList = new ArrayList<>();
    ybClientTableList.add(t1);
    doReturn(ybClientTableList).when(spyBackupHelper).getTableInfosOrEmpty(any());
    spyBackupHelper.validateMapToRestoreWithUniverseNonRedisYBC(
        testUniverse.getUniverseUUID(), restoreMap);

    // Case 2: YSQL table with same keyspace and table name exists
    TableInfo t2 = getTableInfoYSQL(UUID.randomUUID(), "t1", "foo");
    ybClientTableList.clear();
    ybClientTableList.add(t2);
    doReturn(ybClientTableList).when(spyBackupHelper).getTableInfosOrEmpty(any());
    spyBackupHelper.validateMapToRestoreWithUniverseNonRedisYBC(
        testUniverse.getUniverseUUID(), restoreMap);

    // Case 3: YCQL table with different name in the same keyspace
    TableInfo t3 = getTableInfoYCQL(UUID.randomUUID(), "t1", "bar");
    ybClientTableList.clear();
    ybClientTableList.add(t3);
    doReturn(ybClientTableList).when(spyBackupHelper).getTableInfosOrEmpty(any());
    spyBackupHelper.validateMapToRestoreWithUniverseNonRedisYBC(
        testUniverse.getUniverseUUID(), restoreMap);
  }

  @Test(expected = Test.None.class)
  public void testValidateMapToRestoreWithUniverseNonRedisYBC_NoOverwriteYSQL() {
    Map<TableType, Map<String, Set<String>>> restoreMap = new HashMap<>();
    restoreMap.put(TableType.PGSQL_TABLE_TYPE, new HashMap<String, Set<String>>());
    String keyspace_1 = "foo";
    restoreMap.get(TableType.PGSQL_TABLE_TYPE).put(keyspace_1, null);

    // Case 1: YSQL table with same name exists but in different keyspace
    TableInfo t1 = getTableInfoYSQL(UUID.randomUUID(), "t1", "bar");
    List<TableInfo> ybClientTableList = new ArrayList<>();
    ybClientTableList.add(t1);
    doReturn(ybClientTableList).when(spyBackupHelper).getTableInfosOrEmpty(any());
    spyBackupHelper.validateMapToRestoreWithUniverseNonRedisYBC(
        testUniverse.getUniverseUUID(), restoreMap);
  }

  @Test
  public void testValidateMapToRestoreWithUniverseNonRedisYBC_OverwriteYCQL() {
    Map<TableType, Map<String, Set<String>>> restoreMap = new HashMap<>();
    restoreMap.put(TableType.YQL_TABLE_TYPE, new HashMap<String, Set<String>>());
    String keyspace_1 = "foo";
    Set<String> tableNameSet_1 = new HashSet<>(Arrays.asList("t1", "t2", "t3", "t4"));
    restoreMap.get(TableType.YQL_TABLE_TYPE).put(keyspace_1, tableNameSet_1);

    // Case 1: YCQL table with same name exists but in same keyspace
    TableInfo t1 = getTableInfoYCQL(UUID.randomUUID(), "t1", "foo");
    List<TableInfo> ybClientTableList = new ArrayList<>();
    ybClientTableList.add(t1);
    doReturn(ybClientTableList).when(spyBackupHelper).getTableInfosOrEmpty(any());
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () ->
                spyBackupHelper.validateMapToRestoreWithUniverseNonRedisYBC(
                    testUniverse.getUniverseUUID(), restoreMap));
    assertTrue(
        ex.getMessage().equals("Restore attempting overwrite for table t1 on keyspace foo."));
  }

  @Test
  public void testValidateMapToRestoreWithUniverseNonRedisYBC_OverwriteYSQL() {
    Map<TableType, Map<String, Set<String>>> restoreMap = new HashMap<>();
    restoreMap.put(TableType.PGSQL_TABLE_TYPE, new HashMap<String, Set<String>>());
    String keyspace_1 = "foo";
    restoreMap.get(TableType.PGSQL_TABLE_TYPE).put(keyspace_1, null);

    // Case 1: YSQL same name keyspace with tables exists.
    TableInfo t1 = getTableInfoYSQL(UUID.randomUUID(), "t1", "foo");
    List<TableInfo> ybClientTableList = new ArrayList<>();
    ybClientTableList.add(t1);
    doReturn(ybClientTableList).when(spyBackupHelper).getTableInfosOrEmpty(any());
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () ->
                spyBackupHelper.validateMapToRestoreWithUniverseNonRedisYBC(
                    testUniverse.getUniverseUUID(), restoreMap));
    assertTrue(ex.getMessage().equals("Restore attempting overwrite on YSQL keyspace foo."));
  }

  @Test(expected = Test.None.class)
  @Parameters({"backup/ybc_success_file_without_regions.json"})
  public void testValidateStorageConfigForYbcRestoreTaskSuccess(String successFilePath) {
    String successStr = TestUtils.readResource(successFilePath);
    YbcBackupResponse successMarker = YbcBackupUtil.parseYbcBackupResponse(successStr);
    when(mockRuntimeConfGetter.getConfForScope(
            eq(testUniverse), eq(UniverseConfKeys.skipConfigBasedPreflightValidation)))
        .thenReturn(false);
    CustomerConfig storageConfig_S3 = ModelFactory.createS3StorageConfig(testCustomer, "TEST-1");
    when(mockConfigService.getOrBadRequest(any(), any())).thenCallRealMethod();
    when(mockAWSUtil.getRegionLocationsMap(any())).thenCallRealMethod();
    spyBackupHelper.validateStorageConfigForYbcRestoreTask(
        storageConfig_S3.getConfigUUID(),
        testCustomer.getUuid(),
        testUniverse.getUniverseUUID(),
        List.of(successMarker));
  }

  @Test
  @Parameters({"backup/ybc_success_file_with_regions.json"})
  // The success file contains a multi-region backup which should fail with default config.
  public void testValidateStorageConfigForYbcRestoreTaskFail(String successFilePath) {
    String successStr = TestUtils.readResource(successFilePath);
    YbcBackupResponse successMarker = YbcBackupUtil.parseYbcBackupResponse(successStr);
    when(mockRuntimeConfGetter.getConfForScope(
            eq(testUniverse), eq(UniverseConfKeys.skipConfigBasedPreflightValidation)))
        .thenReturn(false);
    CustomerConfig storageConfig_S3 = ModelFactory.createS3StorageConfig(testCustomer, "TEST-1");
    when(mockConfigService.getOrBadRequest(any(), any())).thenCallRealMethod();
    when(mockAWSUtil.getRegionLocationsMap(any())).thenCallRealMethod();
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () ->
                spyBackupHelper.validateStorageConfigForYbcRestoreTask(
                    storageConfig_S3.getConfigUUID(),
                    testCustomer.getUuid(),
                    testUniverse.getUniverseUUID(),
                    List.of(successMarker)));
    assertTrue(ex.getMessage().contains("Storage config does not contain region"));
  }

  @Test
  @Parameters({"true", "false"})
  public void testGetSkipPreflightValidationRuntimeValue(boolean value) {
    when(mockRuntimeConfGetter.getConfForScope(
            eq(testUniverse), eq(UniverseConfKeys.skipConfigBasedPreflightValidation)))
        .thenReturn(value);
    assertEquals(value, spyBackupHelper.isSkipConfigBasedPreflightValidation(testUniverse));
  }

  private Backup createYCQLMultiKeyspaceBackup() {
    CustomerConfig testStorageConfigS3 =
        ModelFactory.createS3StorageConfig(testCustomer, "test_S3");
    BackupTableParams parentBTableParams = new BackupTableParams();
    parentBTableParams.setUniverseUUID(testUniverse.getUniverseUUID());
    parentBTableParams.customerUuid = testCustomer.getUuid();
    parentBTableParams.storageConfigUUID = testStorageConfigS3.getConfigUUID();

    List<UUID> tableUUIDList =
        Arrays.asList(UUID.randomUUID(), UUID.randomUUID(), UUID.randomUUID(), UUID.randomUUID());
    List<String> tableNameList = Arrays.asList("table_1", "table_2", "table_3", "table_4");

    BackupTableParams childBTableParams_1 = new BackupTableParams();
    childBTableParams_1.tableNameList = tableNameList.subList(0, 2);
    childBTableParams_1.tableUUIDList = tableUUIDList.subList(0, 2);
    childBTableParams_1.setKeyspace("foo");
    childBTableParams_1.backupType = TableType.YQL_TABLE_TYPE;
    childBTableParams_1.storageConfigUUID = testStorageConfigS3.getConfigUUID();

    BackupTableParams childBTableParams_2 = new BackupTableParams();
    childBTableParams_2.tableNameList = tableNameList.subList(2, 3);
    childBTableParams_2.tableUUIDList = tableUUIDList.subList(2, 3);
    childBTableParams_2.setKeyspace("foo");
    childBTableParams_2.backupType = TableType.YQL_TABLE_TYPE;
    childBTableParams_2.storageConfigUUID = testStorageConfigS3.getConfigUUID();

    BackupTableParams childBTableParams_3 = new BackupTableParams();
    childBTableParams_3.tableNameList = new ArrayList<>(tableNameList);
    childBTableParams_3.tableUUIDList = new ArrayList<>(tableUUIDList);
    childBTableParams_3.setKeyspace("bar");
    childBTableParams_3.backupType = TableType.YQL_TABLE_TYPE;
    childBTableParams_3.storageConfigUUID = testStorageConfigS3.getConfigUUID();

    parentBTableParams.backupList =
        Arrays.asList(childBTableParams_1, childBTableParams_2, childBTableParams_3);
    return Backup.create(
        testCustomer.getUuid(), parentBTableParams, BackupCategory.YB_CONTROLLER, BackupVersion.V2);
  }

  @Test
  public void testGetBackupLocationKeyspaceTablesMapRestoreEverything() {
    Backup backup = createYCQLMultiKeyspaceBackup();
    Map<String, PerBackupLocationKeyspaceTables> response =
        spyBackupHelper.getBackupLocationKeyspaceTablesMap(null, 0L, backup);
    // assert we get back everything
    assertEquals(3, response.size());
  }

  @Test
  public void testGetBackupLocationKeyspaceTablesMapRestorePartialBothKeyspace() {
    Backup backup = createYCQLMultiKeyspaceBackup();
    KeyspaceTables kT1 = new KeyspaceTables(Set.of("table_1", "table_2", "table_3"), "foo");
    KeyspaceTables kT2 = new KeyspaceTables(null, "bar");
    List<KeyspaceTables> keyspaceTablesList = Arrays.asList(kT1, kT2);
    Map<String, PerBackupLocationKeyspaceTables> response =
        spyBackupHelper.getBackupLocationKeyspaceTablesMap(keyspaceTablesList, 0L, backup);
    // assert we get back what is required
    assertEquals(3, response.size());
    AtomicInteger fooKeyspaceCount = new AtomicInteger(0);
    AtomicInteger barKeyspaceCount = new AtomicInteger(0);
    response.forEach(
        (location, keyspaceTables) -> {
          if (keyspaceTables.getOriginalKeyspace().equals("foo")) {
            fooKeyspaceCount.incrementAndGet();
            if (keyspaceTables.getTableNameList().contains("table_1")) {
              assertEquals(2, keyspaceTables.getTableNameList().size());
              assertTrue(keyspaceTables.getTableNameList().contains("table_1"));
              assertTrue(keyspaceTables.getTableNameList().contains("table_2"));
            } else if (keyspaceTables.getTableNameList().contains("table_3")) {
              assertEquals(1, keyspaceTables.getTableNameList().size());
              assertTrue(keyspaceTables.getTableNameList().contains("table_3"));
            }
          } else if (keyspaceTables.getOriginalKeyspace().equals("bar")) {
            barKeyspaceCount.incrementAndGet();
            assertEquals(4, keyspaceTables.getTableNameList().size());
          }
        });
    assertEquals(2, fooKeyspaceCount.get());
    assertEquals(1, barKeyspaceCount.get());
  }

  @Test
  public void testGetBackupLocationKeyspaceTablesMapRestorePartialOneKeyspace() {
    Backup backup = createYCQLMultiKeyspaceBackup();
    KeyspaceTables kT1 = new KeyspaceTables(Set.of("table_1", "table_2", "table_3"), "foo");
    List<KeyspaceTables> keyspaceTablesList = Arrays.asList(kT1);
    Map<String, PerBackupLocationKeyspaceTables> response =
        spyBackupHelper.getBackupLocationKeyspaceTablesMap(keyspaceTablesList, 0L, backup);
    // assert we get back what is required
    assertEquals(2, response.size());
    AtomicInteger fooKeyspaceCount = new AtomicInteger(0);
    AtomicInteger barKeyspaceCount = new AtomicInteger(0);
    response.forEach(
        (location, keyspaceTables) -> {
          if (keyspaceTables.getOriginalKeyspace().equals("foo")) {
            fooKeyspaceCount.incrementAndGet();
            if (keyspaceTables.getTableNameList().contains("table_1")) {
              assertEquals(2, keyspaceTables.getTableNameList().size());
              assertTrue(keyspaceTables.getTableNameList().contains("table_1"));
              assertTrue(keyspaceTables.getTableNameList().contains("table_2"));
            } else if (keyspaceTables.getTableNameList().contains("table_3")) {
              assertEquals(1, keyspaceTables.getTableNameList().size());
              assertTrue(keyspaceTables.getTableNameList().contains("table_3"));
            }
          } else if (keyspaceTables.getOriginalKeyspace().equals("bar")) {
            barKeyspaceCount.incrementAndGet();
          }
        });
    assertEquals(2, fooKeyspaceCount.get());
    assertEquals(0, barKeyspaceCount.get());
  }

  @Test
  public void testGetBackupLocationKeyspaceTablesMapRestoreKeyspaceDoesNotExistFail() {
    Backup backup = createYCQLMultiKeyspaceBackup();
    KeyspaceTables kT1 = new KeyspaceTables(Set.of("table_1", "table_2", "table_3"), "foo");
    KeyspaceTables kT2 = new KeyspaceTables(null, "fail");
    List<KeyspaceTables> keyspaceTablesList = Arrays.asList(kT1, kT2);
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () ->
                spyBackupHelper.getBackupLocationKeyspaceTablesMap(keyspaceTablesList, 0L, backup));
    assertEquals("Keyspace fail not found in backup", ex.getMessage());
  }

  @Test
  public void testGetBackupLocationKeyspaceTablesMapRestoreTableDoesNotExistFail() {
    Backup backup = createYCQLMultiKeyspaceBackup();
    KeyspaceTables kT1 = new KeyspaceTables(Set.of("table_1", "table_2", "table_3"), "foo");
    KeyspaceTables kT2 = new KeyspaceTables(Set.of("table_2", "table_5"), "bar");
    List<KeyspaceTables> keyspaceTablesList = Arrays.asList(kT1, kT2);
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () ->
                spyBackupHelper.getBackupLocationKeyspaceTablesMap(keyspaceTablesList, 0L, backup));
    assertEquals("Keyspace bar all tables cannot be restored", ex.getMessage());
  }

  @Test
  public void testValidateRestorableKeyspaceTablesAgainstBackupTablesNotExist() {
    Backup backup = createYCQLMultiKeyspaceBackup();
    KeyspaceTables kT1 = new KeyspaceTables(Set.of("table_1", "table_2", "table_3"), "foo");
    KeyspaceTables kT2 = new KeyspaceTables(Set.of("table_2", "table_5"), "bar");
    List<KeyspaceTables> keyspaceTablesList = Arrays.asList(kT1, kT2);
    RestoreItemsValidationParams params =
        new RestoreItemsValidationParams(backup.getBackupUUID(), 0L, keyspaceTablesList);
    Pair<Boolean, List<KeyspaceTables>> nonRestorableItems =
        spyBackupHelper.validateRestorableKeyspaceTablesAgainstBackup(
            testCustomer.getUuid(), params);
    assertFalse(nonRestorableItems.getFirst());
    assertEquals(1, nonRestorableItems.getSecond().size());
    assertEquals(nonRestorableItems.getSecond().get(0).getKeyspace(), "bar");
    assertEquals(nonRestorableItems.getSecond().get(0).getTableNames(), Set.of("table_5"));
  }

  @Test
  public void testValidateRestorableKeyspaceTablesAgainstBackupKeyspaceNotExist_WithTables() {
    Backup backup = createYCQLMultiKeyspaceBackup();
    KeyspaceTables kT1 = new KeyspaceTables(Set.of("table_1", "table_2", "table_3"), "foo");
    KeyspaceTables kT2 = new KeyspaceTables(Set.of("table_2", "table_5"), "fail");
    List<KeyspaceTables> keyspaceTablesList = Arrays.asList(kT1, kT2);
    RestoreItemsValidationParams params =
        new RestoreItemsValidationParams(backup.getBackupUUID(), 0L, keyspaceTablesList);
    Pair<Boolean, List<KeyspaceTables>> nonRestorableItems =
        spyBackupHelper.validateRestorableKeyspaceTablesAgainstBackup(
            testCustomer.getUuid(), params);
    assertFalse(nonRestorableItems.getFirst());
    assertEquals(1, nonRestorableItems.getSecond().size());
    assertEquals(nonRestorableItems.getSecond().get(0).getKeyspace(), "fail");
    assertEquals(
        nonRestorableItems.getSecond().get(0).getTableNames(), Set.of("table_2", "table_5"));
  }

  @Test
  public void testValidateRestorableKeyspaceTablesAgainstBackupKeyspaceNotExist_WithoutTables() {
    Backup backup = createYCQLMultiKeyspaceBackup();
    KeyspaceTables kT1 = new KeyspaceTables(Set.of("table_1", "table_2", "table_3"), "foo");
    KeyspaceTables kT2 = new KeyspaceTables(null, "fail");
    List<KeyspaceTables> keyspaceTablesList = Arrays.asList(kT1, kT2);
    RestoreItemsValidationParams params =
        new RestoreItemsValidationParams(backup.getBackupUUID(), 0L, keyspaceTablesList);
    Pair<Boolean, List<KeyspaceTables>> nonRestorableItems =
        spyBackupHelper.validateRestorableKeyspaceTablesAgainstBackup(
            testCustomer.getUuid(), params);
    assertFalse(nonRestorableItems.getFirst());
    assertEquals(1, nonRestorableItems.getSecond().size());
    assertEquals(nonRestorableItems.getSecond().get(0).getKeyspace(), "fail");
    assertEquals(0, nonRestorableItems.getSecond().get(0).getTableNames().size());
  }

  @Test
  public void testValidateRestorableKeyspaceTablesAgainstBackupKeyspacePartialSuccess() {
    Backup backup = createYCQLMultiKeyspaceBackup();
    KeyspaceTables kT1 = new KeyspaceTables(Set.of("table_1", "table_2", "table_3"), "foo");
    KeyspaceTables kT2 = new KeyspaceTables(Set.of("table_2", "table_3"), "bar");
    List<KeyspaceTables> keyspaceTablesList = Arrays.asList(kT1, kT2);
    RestoreItemsValidationParams params =
        new RestoreItemsValidationParams(backup.getBackupUUID(), 0L, keyspaceTablesList);
    Pair<Boolean, List<KeyspaceTables>> nonRestorableItems =
        spyBackupHelper.validateRestorableKeyspaceTablesAgainstBackup(
            testCustomer.getUuid(), params);
    assertTrue(nonRestorableItems.getFirst());
    assertEquals(0, nonRestorableItems.getSecond().size());
  }

  @Test
  public void testValidateRestorableKeyspaceTablesAgainstBackupKeyspaceAllTablesSuccess() {
    Backup backup = createYCQLMultiKeyspaceBackup();
    KeyspaceTables kT1 = new KeyspaceTables(Set.of("table_1", "table_2", "table_3"), "foo");
    KeyspaceTables kT2 = new KeyspaceTables(null, "bar");
    List<KeyspaceTables> keyspaceTablesList = Arrays.asList(kT1, kT2);
    RestoreItemsValidationParams params =
        new RestoreItemsValidationParams(backup.getBackupUUID(), 0L, keyspaceTablesList);
    Pair<Boolean, List<KeyspaceTables>> nonRestorableItems =
        spyBackupHelper.validateRestorableKeyspaceTablesAgainstBackup(
            testCustomer.getUuid(), params);
    assertTrue(nonRestorableItems.getFirst());
    assertEquals(0, nonRestorableItems.getSecond().size());
  }
}
