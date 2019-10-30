/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.Master.ListTablesResponsePB.TableInfo;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import play.api.Play;

import static com.yugabyte.yw.common.Util.getUUIDRepresentation;

public class MultiTableBackup extends UniverseTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(MultiTableBackup.class);

  public YBClientService ybService;

  public static class Params extends UniverseTaskParams {
    public UUID customerUUID;
    public UUID storageConfigUUID;
    public long schedulingFrequency = 0L;
    public String cronExpression = null;
    public boolean sse = false;
  }

  public Params params() {
    return (Params)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public void run() {
    try {
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      lockUniverse(-1 /* expectedUniverseVersion */);

      Universe universe = Universe.get(params().universeUUID);
      String masterAddresses = universe.getMasterAddresses(true);
      String certificate = universe.getCertificate();
      YBClient client = null;
      List<BackupTableParams> tableBackupParams = new ArrayList<>();
      try {
        client = ybService.getClient(masterAddresses, certificate);
        ListTablesResponse response = client.getTablesList(null, true, null);
        List<TableInfo> tableInfoList = response.getTableInfoList();
        for (TableInfo table : tableInfoList) {
          String tableKeySpace = table.getNamespace().getName().toString();
          BackupTableParams backupParams = new BackupTableParams();
          backupParams.keyspace = tableKeySpace;
          backupParams.tableName = table.getName();
          String tableUUID = table.getId().toStringUtf8();
          backupParams.tableUUID = getUUIDRepresentation(tableUUID);
          backupParams.actionType = BackupTableParams.ActionType.CREATE;
          backupParams.storageConfigUUID = params().storageConfigUUID;
          backupParams.universeUUID = params().universeUUID;
          backupParams.sse = params().sse;
          tableBackupParams.add(backupParams);
          LOG.info("Queuing backup for table {}:{}", tableKeySpace, table.getName());

        }
        ybService.closeClient(client, masterAddresses);
      } catch (Exception e) {
        LOG.error("Failed to get list of tables in universe " + params().universeUUID, e);
        ybService.closeClient(client, masterAddresses);
        unlockUniverseForUpdate();
        throw new RuntimeException(e);
      }
      updateBackupState(true);

      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      LOG.info("Successfully started scheduled backup of tables.");
      for (BackupTableParams tableParams : tableBackupParams) {
        Backup backup = Backup.create(params().customerUUID, tableParams);
        createTableBackupTask(tableParams, backup).setSubTaskGroupType(
            UserTaskDetails.SubTaskGroupType.CreatingTableBackup);
      }

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      unlockUniverseForUpdate();

      subTaskGroupQueue.run();

    } catch (Throwable t) {
      LOG.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      // Run an unlock in case the task failed before getting to the unlock. It is okay if it
      // errors out.
      unlockUniverseForUpdate();
      throw t;
    } finally {
      updateBackupState(false);
    }
    LOG.info("Finished {} task.", getName());
  }
}
