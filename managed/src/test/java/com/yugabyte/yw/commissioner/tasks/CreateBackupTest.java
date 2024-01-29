// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.Util.SYSTEM_PLATFORM_DB;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.protobuf.ByteString;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.CommonTypes.TableType;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;
import org.yb.master.MasterTypes;

@RunWith(MockitoJUnitRunner.class)
public class CreateBackupTest extends CommissionerBaseTest {

  private Universe defaultUniverse;
  private CustomerConfig storageConfig;
  private Users defaultUser;
  private static final UUID TABLE_1_UUID = UUID.randomUUID();
  private static final UUID TABLE_2_UUID = UUID.randomUUID();
  private static final UUID TABLE_3_UUID = UUID.randomUUID();
  private static final UUID TABLE_4_UUID = UUID.randomUUID();
  private static final UUID TABLE_6_UUID = UUID.randomUUID();
  private static final UUID SYSTEM_TABLE_UUID = UUID.randomUUID();

  @Override
  @Before
  public void setUp() {
    super.setUp();
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse();
    defaultUser = ModelFactory.testUser(defaultCustomer);
    storageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "bar");
    List<TableInfo> tableInfoList = new ArrayList<>();
    List<TableInfo> tableInfoList1 = new ArrayList<>();
    List<TableInfo> tableInfoList2 = new ArrayList<>();
    TableInfo ti1 =
        TableInfo.newBuilder()
            .setName("Table1")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("$$$Default0"))
            .setId(ByteString.copyFromUtf8(TABLE_1_UUID.toString()))
            .setTableType(TableType.REDIS_TABLE_TYPE)
            .build();
    TableInfo ti2 =
        TableInfo.newBuilder()
            .setName("Table2")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("$$$Default1"))
            .setId(ByteString.copyFromUtf8(TABLE_2_UUID.toString()))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .build();
    TableInfo ti3 =
        TableInfo.newBuilder()
            .setName("Table3")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("$$$Default2"))
            .setId(ByteString.copyFromUtf8(TABLE_3_UUID.toString()))
            .setTableType(TableType.PGSQL_TABLE_TYPE)
            .build();
    TableInfo ti4 =
        TableInfo.newBuilder()
            .setName("Table4")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("$$$Default2"))
            .setId(ByteString.copyFromUtf8(TABLE_4_UUID.toString()))
            .setTableType(TableType.PGSQL_TABLE_TYPE)
            .build();
    TableInfo ti5 =
        TableInfo.newBuilder()
            .setName("write_read_test")
            .setNamespace(
                MasterTypes.NamespaceIdentifierPB.newBuilder().setName(SYSTEM_PLATFORM_DB))
            .setId(ByteString.copyFromUtf8(SYSTEM_TABLE_UUID.toString()))
            .setTableType(TableType.PGSQL_TABLE_TYPE)
            .build();
    TableInfo ti6 =
        TableInfo.newBuilder()
            .setName("Table6")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("$$$Default3"))
            .setId(ByteString.copyFromUtf8(TABLE_6_UUID.toString()))
            .setTableType(TableType.PGSQL_TABLE_TYPE)
            .build();
    tableInfoList.add(ti1);
    tableInfoList.add(ti2);
    tableInfoList.add(ti3);
    tableInfoList.add(ti4);
    tableInfoList.add(ti5);
    tableInfoList.add(ti6);
    tableInfoList1.add(ti1);
    tableInfoList2.add(ti3);
    tableInfoList2.add(ti4);
    YBClient mockClient = mock(YBClient.class);
    ListTablesResponse mockListTablesResponse = mock(ListTablesResponse.class);
    GetTableSchemaResponse mockSchemaResponse1 = mock(GetTableSchemaResponse.class);
    GetTableSchemaResponse mockSchemaResponse2 = mock(GetTableSchemaResponse.class);
    GetTableSchemaResponse mockSchemaResponse3 = mock(GetTableSchemaResponse.class);
    mockClient = mock(YBClient.class);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    try {
      when(mockClient.getTablesList(null, true, null)).thenReturn(mockListTablesResponse);
      when(mockClient.getTableSchemaByUUID(TABLE_1_UUID.toString().replace("-", "")))
          .thenReturn(mockSchemaResponse1);
      when(mockClient.getTableSchemaByUUID(TABLE_2_UUID.toString().replace("-", "")))
          .thenReturn(mockSchemaResponse2);
      when(mockClient.getTableSchemaByUUID(TABLE_3_UUID.toString().replace("-", "")))
          .thenReturn(mockSchemaResponse3);
    } catch (Exception e) {
      // Do nothing.
    }
    when(mockListTablesResponse.getTableInfoList()).thenReturn(tableInfoList);
    when(mockSchemaResponse1.getTableType()).thenReturn(TableType.REDIS_TABLE_TYPE);
    when(mockSchemaResponse2.getTableName()).thenReturn("Table2");
    when(mockSchemaResponse2.getNamespace()).thenReturn("$$$Default1");
    when(mockSchemaResponse2.getTableType()).thenReturn(TableType.YQL_TABLE_TYPE);
    when(mockSchemaResponse3.getTableType()).thenReturn(TableType.PGSQL_TABLE_TYPE);
  }

  private TaskInfo submitTask(ITaskParams backupTableParams) {
    try {
      UUID taskUUID = commissioner.submit(TaskType.CreateBackup, backupTableParams);
      // Set http context
      TestUtils.setFakeHttpContext(defaultUser);
      CustomerTask.create(
          defaultCustomer,
          defaultUniverse.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.Universe,
          CustomerTask.TaskType.Backup,
          "bar");
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private TaskInfo submitTask(String keyspace, List<UUID> tableUUIDs, TableType backupType) {
    BackupRequestParams backupTableParams = new BackupRequestParams();
    backupTableParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    backupTableParams.customerUUID = defaultCustomer.getUuid();
    BackupRequestParams.KeyspaceTable keyspaceTable = new BackupRequestParams.KeyspaceTable();
    keyspaceTable.keyspace = keyspace;
    keyspaceTable.tableUUIDList = tableUUIDs;
    backupTableParams.keyspaceTableList = Collections.singletonList(keyspaceTable);
    backupTableParams.backupType = backupType;
    backupTableParams.storageConfigUUID = storageConfig.getConfigUUID();
    return submitTask(backupTableParams);
  }

  private TaskInfo submitTask(TableType backupType) {
    BackupRequestParams backupTableParams = new BackupRequestParams();
    backupTableParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    backupTableParams.customerUUID = defaultCustomer.getUuid();
    backupTableParams.backupType = backupType;
    backupTableParams.storageConfigUUID = storageConfig.getConfigUUID();
    return submitTask(backupTableParams);
  }

  @Test
  public void testUniverseBackupYcql() {
    Map<String, String> config = new HashMap<>();
    config.put(Universe.TAKE_BACKUPS, "true");
    defaultUniverse.updateConfig(config);
    defaultUniverse.save();
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"snapshot_url\": \"/tmp/backup\", \"backup_size_in_bytes\": 340}";
    shellResponse.code = 0;
    when(mockTableManagerYb.createBackup(any())).thenReturn(shellResponse);

    // Entire universe backup, only YCQL tables
    TaskInfo taskInfo = submitTask(TableType.YQL_TABLE_TYPE);
    verify(mockTableManagerYb, times(1)).createBackup(any());
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testBackupKeyspaceListYsql() {
    Map<String, String> config = new HashMap<>();
    config.put(Universe.TAKE_BACKUPS, "true");
    defaultUniverse.updateConfig(config);
    defaultUniverse.save();
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"snapshot_url\": \"/tmp/backup\", \"backup_size_in_bytes\": 340}";
    shellResponse.code = 0;
    when(mockTableManagerYb.createBackup(any())).thenReturn(shellResponse);
    BackupRequestParams backupTableParams = new BackupRequestParams();
    backupTableParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    backupTableParams.customerUUID = defaultCustomer.getUuid();
    BackupRequestParams.KeyspaceTable keyspaceTable1 = new BackupRequestParams.KeyspaceTable();
    BackupRequestParams.KeyspaceTable keyspaceTable2 = new BackupRequestParams.KeyspaceTable();
    keyspaceTable1.keyspace = "$$$Default2";
    keyspaceTable2.keyspace = "$$$Default3";
    List<BackupRequestParams.KeyspaceTable> keyspaceTables = new ArrayList<>();
    keyspaceTables.add(keyspaceTable1);
    keyspaceTables.add(keyspaceTable2);
    backupTableParams.keyspaceTableList = keyspaceTables;
    backupTableParams.backupType = TableType.PGSQL_TABLE_TYPE;
    backupTableParams.storageConfigUUID = storageConfig.getConfigUUID();
    // Entire universe backup, only YCQL tables
    TaskInfo taskInfo = submitTask(backupTableParams);
    verify(mockTableManagerYb, times(2)).createBackup(any());
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testMultiTableBackupKeyspace() {
    Map<String, String> config = new HashMap<>();
    config.put(Universe.TAKE_BACKUPS, "true");
    defaultUniverse.updateConfig(config);
    defaultUniverse.save();
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"snapshot_url\": \"/tmp/backup\", \"backup_size_in_bytes\": 340}";
    shellResponse.code = 0;
    when(mockTableManagerYb.createBackup(any())).thenReturn(shellResponse);

    TaskInfo taskInfo = submitTask("$$$Default1", new ArrayList<>(), TableType.YQL_TABLE_TYPE);
    verify(mockTableManagerYb, times(1)).createBackup(any());
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testMultiTablebackupInvalidKeyspace() {
    Map<String, String> config = new HashMap<>();
    config.put(Universe.TAKE_BACKUPS, "true");
    defaultUniverse.updateConfig(config);
    defaultUniverse.save();
    TaskInfo taskInfo =
        submitTask("InvalidKeySpace", new ArrayList<>(), TableType.PGSQL_TABLE_TYPE);
    assertEquals(Failure, taskInfo.getTaskState());
    String errMsg = taskInfo.getDetails().get("errorString").asText();
    assertThat(errMsg, containsString("Invalid Keyspaces or no tables to backup"));
    verify(mockTableManagerYb, times(0)).createBackup(any());
  }

  @Test
  public void testUniverseBackupYsql() {
    Map<String, String> config = new HashMap<>();
    config.put(Universe.TAKE_BACKUPS, "true");
    defaultUniverse.updateConfig(config);
    defaultUniverse.save();
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"snapshot_url\": \"/tmp/backup\", \"backup_size_in_bytes\": 340}";
    shellResponse.code = 0;
    when(mockTableManagerYb.createBackup(any())).thenReturn(shellResponse);

    TaskInfo taskInfo = submitTask(TableType.PGSQL_TABLE_TYPE);
    verify(mockTableManagerYb, times(2)).createBackup(any());
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testMultiTableBackupList() {
    Map<String, String> config = new HashMap<>();
    config.put(Universe.TAKE_BACKUPS, "true");
    defaultUniverse.updateConfig(config);
    defaultUniverse.save();
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"snapshot_url\": \"/tmp/backup\", \"backup_size_in_bytes\": 340}";
    shellResponse.code = 0;
    when(mockTableManagerYb.createBackup(any())).thenReturn(shellResponse);
    List<UUID> tableUUIDs = new ArrayList<>();
    tableUUIDs.add(TABLE_1_UUID);
    tableUUIDs.add(TABLE_2_UUID);
    tableUUIDs.add(TABLE_3_UUID);
    TaskInfo taskInfo = submitTask("$$$Default1", tableUUIDs, TableType.YQL_TABLE_TYPE);
    verify(mockTableManagerYb, times(1)).createBackup(any());
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testMultiTableBackupIgnore() {
    Map<String, String> config = new HashMap<>();
    config.put(Universe.TAKE_BACKUPS, "false");
    defaultUniverse.updateConfig(config);
    defaultUniverse.save();
    TaskInfo taskInfo = submitTask(TableType.YQL_TABLE_TYPE);
    verify(mockTableManagerYb, times(0)).createBackup(any());
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testAbortBackup() {
    Map<String, String> config = new HashMap<>();
    config.put(Universe.TAKE_BACKUPS, "true");
    defaultUniverse.updateConfig(config);
    defaultUniverse.save();
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.code = ShellResponse.ERROR_CODE_EXECUTION_CANCELLED;
    when(mockTableManagerYb.createBackup(any())).thenReturn(shellResponse);
    TaskInfo taskInfo = submitTask(TableType.YQL_TABLE_TYPE);
    List<Backup> backupList = Backup.fetchAllBackupsByTaskUUID(taskInfo.getTaskUUID());
    assertNotEquals(0, backupList.size());
    backupList.forEach((backup -> assertEquals(BackupState.Stopped, backup.getState())));
  }
}
