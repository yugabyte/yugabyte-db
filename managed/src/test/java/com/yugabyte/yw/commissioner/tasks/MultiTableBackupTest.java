// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.MultiTableBackup;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.runners.MockitoJUnitRunner;
import com.google.protobuf.ByteString;

import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.Master;
import org.yb.master.Master.ListTablesResponsePB.TableInfo;
import org.yb.Common.TableType;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;

import static com.yugabyte.yw.models.Backup.BackupState.Completed;
import static com.yugabyte.yw.models.Backup.BackupState.Failed;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;


@RunWith(MockitoJUnitRunner.class)
public class MultiTableBackupTest extends CommissionerBaseTest {

  @InjectMocks
  Commissioner commissioner;

  Universe defaultUniverse;
  YBClient mockClient;
  ListTablesResponse mockListTablesResponse;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse();
    List<TableInfo> tableInfoList = new ArrayList<TableInfo>();
    Set<String> tableNames = new HashSet<String>();
    tableNames.add("Table1");
    tableNames.add("Table2");
    TableInfo ti1 = TableInfo.newBuilder()
        .setName("Table1")
        .setNamespace(Master.NamespaceIdentifierPB.newBuilder().setName("$$$Default"))
        .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString()))
        .setTableType(TableType.REDIS_TABLE_TYPE)
        .build();
    TableInfo ti2 = TableInfo.newBuilder()
        .setName("Table2")
        .setNamespace(Master.NamespaceIdentifierPB.newBuilder().setName("$$$Default"))
        .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString()))
        .setTableType(TableType.YQL_TABLE_TYPE)
        .build();
    tableInfoList.add(ti1);
    tableInfoList.add(ti2);
    mockClient = mock(YBClient.class);
    mockListTablesResponse = mock(ListTablesResponse.class);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    try {
      when(mockClient.getTablesList(null, true, null)).thenReturn(mockListTablesResponse);
    } catch (Exception e) {
      // Do nothing.
    }
    when(mockListTablesResponse.getTableInfoList()).thenReturn(tableInfoList);
  }

  private TaskInfo submitTask() {
    MultiTableBackup.Params backupTableParams = new MultiTableBackup.Params();
    backupTableParams.universeUUID = defaultUniverse.universeUUID;
    backupTableParams.customerUUID = defaultCustomer.uuid;
    backupTableParams.storageConfigUUID = UUID.randomUUID();
    try {
      UUID taskUUID = commissioner.submit(TaskType.MultiTableBackup, backupTableParams);
      CustomerTask.create(defaultCustomer, defaultUniverse.universeUUID, taskUUID,
          CustomerTask.TargetType.Universe, CustomerTask.TaskType.Backup,
          "bar");
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testMultiTableBackup() {
    Map<String, String> config = new HashMap<>();
    config.put("takeBackups", "true");
    defaultUniverse.setConfig(config);
    ShellProcessHandler.ShellResponse shellResponse =  new ShellProcessHandler.ShellResponse();
    shellResponse.message = "{\"success\": true}";
    shellResponse.code = 0;
    when(mockTableManager.createBackup(any())).thenReturn(shellResponse);

    TaskInfo taskInfo = submitTask();
    verify(mockTableManager, times(2)).createBackup(any());
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
  }

  @Test
  public void testMultiTableBackupIgnore() {
    Map<String, String> config = new HashMap<>();
    config.put("takeBackups", "false");
    defaultUniverse.setConfig(config);
    TaskInfo taskInfo = submitTask();
    verify(mockTableManager, times(0)).createBackup(any());
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
  }
}
