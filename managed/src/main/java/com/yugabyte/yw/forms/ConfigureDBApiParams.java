// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.password.PasswordPolicyService;
import com.yugabyte.yw.controllers.handlers.UniverseTableHandler;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.List;
import org.apache.commons.lang3.StringUtils;
import org.yb.CommonTypes.TableType;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = ConfigureDBApiParams.Converter.class)
public class ConfigureDBApiParams extends UpgradeTaskParams {

  public boolean enableYSQL;

  public boolean enableYSQLAuth;

  public String ysqlPassword;

  public boolean enableYCQL;

  public boolean enableYCQLAuth;

  public String ycqlPassword;

  public CommunicationPorts communicationPorts = new CommunicationPorts();

  public ServerType configureServer;

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    CommunicationPorts universePorts = universe.getUniverseDetails().communicationPorts;
    boolean changeInYsql =
        (enableYSQL != userIntent.enableYSQL)
            || (enableYSQLAuth != userIntent.enableYSQLAuth)
            || (!StringUtils.isEmpty(ysqlPassword))
            || (communicationPorts.ysqlServerHttpPort != universePorts.ysqlServerHttpPort)
            || (communicationPorts.ysqlServerRpcPort != universePorts.ysqlServerRpcPort);
    boolean changeInYcql =
        (enableYCQL != userIntent.enableYCQL)
            || (enableYCQLAuth != userIntent.enableYCQLAuth)
            || (!StringUtils.isEmpty(ycqlPassword))
            || (communicationPorts.yqlServerHttpPort != universePorts.yqlServerHttpPort)
            || (communicationPorts.yqlServerRpcPort != universePorts.yqlServerRpcPort);

    if (!enableYCQL && !enableYSQL) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Need to enable at least one endpoint among YSQL and YCQL");
    }

    if (configureServer.equals(ServerType.YSQLSERVER)) {
      if (changeInYcql) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot configure YCQL along with YSQL at a time.");
      } else if (enableYSQL && !userIntent.enableYSQL) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot enable YSQL if it was disabled earlier.");
      } else if ((communicationPorts.ysqlServerHttpPort != universePorts.ysqlServerHttpPort
              || communicationPorts.ysqlServerRpcPort != universePorts.ysqlServerRpcPort)
          && userIntent.providerType.equals(CloudType.kubernetes)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot change YSQL ports on k8s universe.");
      } else if ((enableYSQLAuth != userIntent.enableYSQLAuth)
          && StringUtils.isEmpty(ysqlPassword)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Required password to configure YSQL auth.");
      } else if (enableYSQL
          && (enableYSQLAuth == userIntent.enableYSQLAuth && !enableYSQLAuth)
          && !StringUtils.isEmpty(ysqlPassword)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot set password while YSQL auth is disabled.");
      } else if (!enableYSQL) {
        // Ensure that user deletes all backup schedules, xcluster configs
        // and pitr configs before disabling YSQL.
        if (PitrConfig.getByUniverseUUID(universe.getUniverseUUID()).stream()
            .anyMatch(p -> p.getTableType().equals(TableType.PGSQL_TABLE_TYPE))) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Cannot disable YSQL if pitr config exists");
        }
        if (Schedule.getAllSchedulesByOwnerUUID(universe.getUniverseUUID()).stream()
            .anyMatch(
                s ->
                    s.getTaskParams().has("backupType")
                        && s.getTaskParams()
                            .get("backupType")
                            .asText()
                            .equals(TableType.PGSQL_TABLE_TYPE.toString()))) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Cannot disable YSQL if backup schedules are active");
        }
        if (XClusterConfig.getByUniverseUuid(universe.getUniverseUUID()).stream()
            .anyMatch(
                config -> config.getTableTypeAsCommonType().equals(TableType.PGSQL_TABLE_TYPE))) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Cannot disable YSQL if xcluster config exists");
        }
      }
    } else if (configureServer.equals(ServerType.YQLSERVER)) {
      if (changeInYsql) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot configure YSQL along with YCQL at a time.");
      } else if (!enableYCQL && enableYCQLAuth) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot enable YCQL auth when API is disabled.");
      } else if ((communicationPorts.yqlServerHttpPort != universePorts.yqlServerHttpPort
          || communicationPorts.yqlServerRpcPort != universePorts.yqlServerRpcPort)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot change YCQL ports on k8s universe.");
      } else if ((enableYCQLAuth != userIntent.enableYCQLAuth)
          && StringUtils.isEmpty(ycqlPassword)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Required password to configure YCQL auth.");
      } else if (enableYCQL
          && (enableYCQLAuth == userIntent.enableYCQLAuth && !enableYCQLAuth)
          && !StringUtils.isEmpty(ycqlPassword)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot set password while YCQL auth is disabled.");
      } else if (!enableYCQL) {
        // Ensure that all backup schedules, xcluster configs
        // and pitr configs are deleted before disabling YCQL.
        if (PitrConfig.getByUniverseUUID(universe.getUniverseUUID()).stream()
            .anyMatch(p -> p.getTableType().equals(TableType.YQL_TABLE_TYPE))) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Cannot disable YCQL if pitr config exists");
        }
        if (Schedule.getAllSchedulesByOwnerUUID(universe.getUniverseUUID()).stream()
            .anyMatch(
                s ->
                    s.getTaskParams().has("backupType")
                        && s.getTaskParams()
                            .get("backupType")
                            .asText()
                            .equals(TableType.YQL_TABLE_TYPE.toString()))) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Cannot disable YCQL if backup schedules are active");
        }
        if (XClusterConfig.getByUniverseUuid(universe.getUniverseUUID()).stream()
            .anyMatch(
                config -> config.getTableTypeAsCommonType().equals(TableType.YQL_TABLE_TYPE))) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Cannot disable YCQL if xcluster config exists");
        }
      }
    } else {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot configure server type " + configureServer);
    }
  }

  public void validatePassword(PasswordPolicyService policyService) {
    if (enableYSQLAuth && !StringUtils.isEmpty(ysqlPassword)) {
      policyService.checkPasswordPolicy(null, ysqlPassword);
    }
    if (enableYCQLAuth && !StringUtils.isEmpty(ycqlPassword)) {
      policyService.checkPasswordPolicy(null, ycqlPassword);
    }
  }

  public void validateYSQLTables(Universe universe, UniverseTableHandler tableHandler) {
    if (enableYSQL) {
      return;
    }
    // Validate ysql tables exists only while disabling YSQL.
    Customer customer = Customer.get(universe.getCustomerId());
    List<TableInfoForm.TableInfoResp> tables =
        tableHandler.listTables(
            customer.getUuid(),
            universe.getUniverseUUID(),
            false /*includeParentTableInfo */,
            false /* excludeColocatedTables */,
            false /* includeColocatedParentTables */,
            false /* xClusterSupportedOnly */);
    if (tables.stream().anyMatch(t -> t.tableType.equals(TableType.PGSQL_TABLE_TYPE))) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot disable YSQL if any tables exists");
    }
  }

  public void validateYCQLTables(Universe universe, UniverseTableHandler tableHandler) {
    if (enableYCQL) {
      return;
    }
    // Validate ycql tables exists only while disabling YCQL.
    Customer customer = Customer.get(universe.getCustomerId());
    List<TableInfoForm.TableInfoResp> tables =
        tableHandler.listTables(
            customer.getUuid(),
            universe.getUniverseUUID(),
            false /*includeParentTableInfo */,
            false /* excludeColocatedTables */,
            false /* includeColocatedParentTables */,
            false /* xClusterSupportedOnly */);
    if (tables.stream().anyMatch(t -> t.tableType.equals(TableType.YQL_TABLE_TYPE))) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot disable YCQL if any tables exists");
    }
  }

  public static class Converter extends BaseConverter<ConfigureDBApiParams> {}
}
