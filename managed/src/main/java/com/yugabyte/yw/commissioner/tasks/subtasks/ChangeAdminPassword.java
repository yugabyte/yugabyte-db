// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YcqlQueryExecutor;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.forms.DatabaseSecurityFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.mvc.Http;

@Slf4j
public class ChangeAdminPassword extends UniverseTaskBase {
  @Inject
  protected ChangeAdminPassword(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Inject YsqlQueryExecutor ysqlQueryExecutor;
  @Inject YcqlQueryExecutor ycqlQueryExecutor;

  // Parameters for marking universe update as a success.
  public static class Params extends UniverseTaskParams {
    public Cluster primaryCluster;
    public String ysqlPassword;
    public String ycqlPassword;
    public String ysqlUserName;
    public String ysqlCurrentPassword;
    public String ysqlNewPassword;
    public String ycqlUserName;
    public String ycqlCurrentPassword;
    public String ycqlNewPassword;
    public String ysqlDbName;
    public boolean validateCurrentPassword;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().getUniverseUUID() + ")";
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());

      DatabaseSecurityFormData dbData = new DatabaseSecurityFormData();
      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      Cluster primaryCluster = taskParams().primaryCluster;
      if (primaryCluster == null) {
        primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
      }
      if (primaryCluster.userIntent.enableYCQL
          && primaryCluster.userIntent.enableYCQLAuth
          && !StringUtils.isEmpty(taskParams().ycqlNewPassword)) {
        dbData.ycqlCurrAdminPassword = taskParams().ycqlCurrentPassword;
        dbData.ycqlAdminUsername = taskParams().ycqlUserName;
        dbData.ycqlAdminPassword = taskParams().ycqlNewPassword;

        if (taskParams().validateCurrentPassword) {
          DatabaseSecurityFormData testDBCreds = new DatabaseSecurityFormData();
          testDBCreds.ycqlAdminPassword = dbData.ycqlCurrAdminPassword;
          testDBCreds.ycqlAdminUsername = dbData.ycqlAdminUsername;
          try {
            ycqlQueryExecutor.validateAdminPassword(universe, testDBCreds);
          } catch (PlatformServiceException e) {
            if (e.getHttpStatus() == Http.Status.UNAUTHORIZED) {
              testDBCreds.ycqlAdminPassword = Util.DEFAULT_YCQL_PASSWORD;
              ycqlQueryExecutor.validateAdminPassword(universe, testDBCreds);
            } else {
              throw e;
            }
          }
        }

        try {
          // Check if the password already works.
          ycqlQueryExecutor.validateAdminPassword(universe, dbData);
          log.info("YCQL password is already updated");
        } catch (PlatformServiceException e) {
          if (e.getHttpStatus() == Http.Status.UNAUTHORIZED) {
            log.info("Updating YCQL password");
            ycqlQueryExecutor.updateAdminPassword(universe, dbData);
          } else {
            throw e;
          }
        }
      }
      if (primaryCluster.userIntent.enableYSQL
          && primaryCluster.userIntent.enableYSQLAuth
          && !StringUtils.isEmpty(taskParams().ysqlNewPassword)) {
        dbData.dbName = taskParams().ysqlDbName;
        dbData.ysqlCurrAdminPassword = taskParams().ysqlCurrentPassword;
        dbData.ysqlAdminUsername = taskParams().ysqlUserName;
        dbData.ysqlAdminPassword = taskParams().ysqlNewPassword;

        if (taskParams().validateCurrentPassword) {
          DatabaseSecurityFormData testDBCreds = new DatabaseSecurityFormData();
          testDBCreds.ysqlAdminPassword = dbData.ysqlCurrAdminPassword;
          testDBCreds.dbName = dbData.dbName;
          testDBCreds.ysqlAdminUsername = dbData.ysqlAdminUsername;
          try {
            ysqlQueryExecutor.validateAdminPassword(universe, testDBCreds);
          } catch (PlatformServiceException e) {
            if (e.getHttpStatus() == Http.Status.UNAUTHORIZED) {
              testDBCreds.ysqlAdminPassword = Util.DEFAULT_YSQL_PASSWORD;
              ysqlQueryExecutor.validateAdminPassword(universe, testDBCreds);
            } else {
              throw e;
            }
          }
        }

        try {
          // Check if the password already works.
          ysqlQueryExecutor.validateAdminPassword(universe, dbData);
          log.info("YSQL password is already updated");
        } catch (PlatformServiceException e) {
          if (e.getHttpStatus() == Http.Status.UNAUTHORIZED) {
            log.info("Updating YSQL password");
            ysqlQueryExecutor.updateAdminPassword(universe, dbData);
          } else {
            throw e;
          }
        }
      }
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
