// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.password.PasswordPolicyService;
import com.yugabyte.yw.models.Universe;
import org.apache.commons.lang3.StringUtils;

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
  public void verifyParams(Universe universe) {
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
      }
    } else {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot configure server type " + configureServer.toString());
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

  public static class Converter extends BaseConverter<ConfigureDBApiParams> {}
}
