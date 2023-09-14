/*
* Copyright 2022 YugaByte, Inc. and Contributors
*
* Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
* may not use this file except in compliance with the License. You
* may obtain a copy of the License at
*
https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
*/

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.SERVICE_UNAVAILABLE;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.util.ArrayList;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes.TableType;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;

@Slf4j
public class DeleteKeyspace extends UniverseTaskBase {

  @Inject private YsqlQueryExecutor ysqlQueryExecutor;

  private static final String MASTERS_UNAVAILABLE_ERR_MSG =
      "Expected error. Masters are not currently queryable.";

  // template1 db is always available, it is ok to use this as we will drop a DB using
  // this and do nothing more with it.
  private static final String DB_NAME = "template1";

  @Inject
  protected DeleteKeyspace(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends BackupTableParams {}

  @Override
  protected DeleteKeyspace.Params taskParams() {
    return (DeleteKeyspace.Params) taskParams;
  }

  @Override
  public void run() {
    TableType tableType = taskParams().backupType;
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    final String keyspaceName = taskParams().getKeyspace();
    YBClient client = null;
    final String masterAddresses = universe.getMasterAddresses();
    if (tableType == TableType.PGSQL_TABLE_TYPE) {
      try {
        // Build the query to run.
        String query = String.format("DROP DATABASE IF EXISTS \"%s\"", keyspaceName);

        // Execute the query.
        log.info("Executing query {} to drop existing DB {}", query, keyspaceName);
        ysqlQueryExecutor.runUserDbCommands(query, DB_NAME, universe);
        log.info("Finished running query to drop any existing database {}", keyspaceName);
      } catch (Exception ex) {
        log.error("Exception {} occurred while cleaning up DB {}", ex, keyspaceName);
        String exMsg =
            String.format(
                "Error %s occurred while cleaning up DB %s ", ex.getMessage(), keyspaceName);
        throw new RuntimeException(exMsg);
      }
    } else if (tableType == TableType.YQL_TABLE_TYPE) {
      String certificate = universe.getCertificateNodetoNode();

      if (masterAddresses.isEmpty()) {
        throw new PlatformServiceException(SERVICE_UNAVAILABLE, MASTERS_UNAVAILABLE_ERR_MSG);
      }
      log.info(
          "Preparing to make a call on {} to delete keyspace {} if it exists",
          masterAddresses,
          keyspaceName);
      try {
        client = ybService.getClient(masterAddresses, certificate);

        // Get all tables in the keyspace name.
        ListTablesResponse response = client.getTablesList(null, false, keyspaceName);
        // Filter by table type YCQL.
        List<TableInfo> tableInfoList = response.getTableInfoList();
        List<String> ycqlTableList = new ArrayList<>(tableInfoList.size());
        for (TableInfo table : tableInfoList) {
          if (isTableYCQL(table)) {
            ycqlTableList.add(table.getName());
          }
        }

        // Delete YCQL tables in the keyspace.
        YBClient finalClient = client;
        ycqlTableList.forEach(
            tableName -> {
              try {
                finalClient.deleteTable(keyspaceName, tableName);
                log.info(
                    "Dropped table {} from keyspace {}",
                    CommonUtils.logTableName(tableName),
                    keyspaceName);
              } catch (Exception e) {
                throw new RuntimeException(e);
              }
            });

        // The keyspace is empty and ready to be deleted.
        client.deleteNamespace(keyspaceName);
        log.info("Deleted YCQL keyspace {}", keyspaceName);
      } catch (Exception e) {
        String msg = "Error " + e.getMessage() + " while deleting keyspace " + keyspaceName;
        log.error(msg, e);
        throw new RuntimeException(msg);
      } finally {
        ybService.closeClient(client, masterAddresses);
      }
    } else {
      String errMsg =
          String.format(
              "Invalid table type: %s. Cannot delete keyspace %s", tableType, keyspaceName);
      throw new RuntimeException(errMsg);
    }
  }

  private boolean isTableYCQL(TableInfo tableInfo) {
    return (tableInfo.getTableType() == TableType.YQL_TABLE_TYPE);
  }
}
