/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers.handlers;

import static com.yugabyte.yw.common.Util.CONNECTION_POOLING_PREVIEW_VERSION;
import static com.yugabyte.yw.common.Util.CONNECTION_POOLING_STABLE_VERSION;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YcqlQueryExecutor;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.password.PasswordPolicyService;
import com.yugabyte.yw.forms.ConfigureDBApiParams;
import com.yugabyte.yw.forms.DatabaseSecurityFormData;
import com.yugabyte.yw.forms.DatabaseUserDropFormData;
import com.yugabyte.yw.forms.DatabaseUserFormData;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.net.URI;
import java.net.URISyntaxException;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Singleton;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Http;
import play.mvc.Http.Request;

@Singleton
public class UniverseYbDbAdminHandler {

  private static final Logger LOG = LoggerFactory.getLogger(UniverseYbDbAdminHandler.class);

  @VisibleForTesting public static final String LEARN_DOMAIN_NAME = "learn.yugabyte.com";

  @Inject Config appConfig;
  @Inject ConfigHelper configHelper;
  @Inject YsqlQueryExecutor ysqlQueryExecutor;
  @Inject YcqlQueryExecutor ycqlQueryExecutor;
  @Inject Commissioner commissioner;
  @Inject PasswordPolicyService policyService;
  @Inject RuntimeConfGetter confGetter;
  @Inject UniverseTableHandler tableHandler;
  @Inject GFlagsValidation gFlagsValidation;

  public UniverseYbDbAdminHandler() {}

  private static boolean isCorrectOrigin(Request request) {
    boolean correctOrigin = false;
    Optional<String> origin = request.header(Http.HeaderNames.ORIGIN);
    if (origin.isPresent()) {
      try {
        URI uri = new URI(origin.get());
        correctOrigin = LEARN_DOMAIN_NAME.equals(uri.getHost());
      } catch (URISyntaxException e) {
        LOG.debug("Ignored exception: " + e.getMessage());
      }
    }
    return correctOrigin;
  }

  public void setDatabaseCredentials(
      Customer customer, Universe universe, DatabaseSecurityFormData dbCreds) {

    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    // Only yugbayte customer cloud can modify password for users other than default.
    boolean isCloudEnabled = confGetter.getConfForScope(customer, CustomerConfKeys.cloudEnabled);
    if (!StringUtils.isEmpty(dbCreds.ysqlAdminUsername)) {
      if (!userIntent.enableYSQLAuth && !isCloudEnabled) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot change password for ysql as its auth is already disabled.");
      } else if (!dbCreds.ysqlAdminUsername.equals(Util.DEFAULT_YSQL_USERNAME) && !isCloudEnabled) {
        throw new PlatformServiceException(BAD_REQUEST, "Invalid Customer type.");
      }
    }
    if (!StringUtils.isEmpty(dbCreds.ycqlAdminUsername)) {
      if (!userIntent.enableYCQLAuth && !isCloudEnabled) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot change password for ycql as its auth is already disabled.");
      } else if (!dbCreds.ycqlAdminUsername.equals(Util.DEFAULT_YCQL_USERNAME) && !isCloudEnabled) {
        throw new PlatformServiceException(BAD_REQUEST, "Invalid Customer type.");
      }
    }

    dbCreds.validation();
    if (!isCloudEnabled) {
      dbCreds.validatePassword(policyService);
    }

    if (!StringUtils.isEmpty(dbCreds.ysqlAdminUsername)) {
      // Test current password
      try {
        DatabaseSecurityFormData testDBCreds = new DatabaseSecurityFormData();
        testDBCreds.ysqlAdminPassword = dbCreds.ysqlCurrAdminPassword;
        testDBCreds.dbName = dbCreds.dbName;
        testDBCreds.ysqlAdminUsername = dbCreds.ysqlAdminUsername;
        ysqlQueryExecutor.validateAdminPassword(universe, testDBCreds);
      } catch (PlatformServiceException pe) {
        throw new PlatformServiceException(
            BAD_REQUEST, "provided username and password are incorrect.");
      }
      // No need to check the current password since we're already using it to log in.
      // Just update new password.
      ysqlQueryExecutor.updateAdminPassword(universe, dbCreds);
    }

    if (!StringUtils.isEmpty(dbCreds.ycqlAdminUsername)) {
      ycqlQueryExecutor.updateAdminPassword(universe, dbCreds);
    }
  }

  public void dropUser(Customer customer, Universe universe, DatabaseUserDropFormData data) {
    if (!confGetter.getConfForScope(customer, CustomerConfKeys.cloudEnabled)) {
      throw new PlatformServiceException(BAD_REQUEST, "Feature not allowed.");
    }

    ysqlQueryExecutor.dropUser(universe, data);
  }

  public void createRestrictedUser(
      Customer customer, Universe universe, DatabaseUserFormData data) {
    if (!confGetter.getConfForScope(customer, CustomerConfKeys.cloudEnabled)) {
      throw new PlatformServiceException(BAD_REQUEST, "Feature not allowed.");
    }
    data.validation();

    ysqlQueryExecutor.createRestrictedUser(universe, data);
  }

  public void createUserInDB(Customer customer, Universe universe, DatabaseUserFormData data) {
    if (!confGetter.getConfForScope(customer, CustomerConfKeys.cloudEnabled)) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Customer type.");
    }
    data.validation();

    if (!StringUtils.isEmpty(data.ysqlAdminUsername)) {
      ysqlQueryExecutor.createUser(universe, data);
    }
    if (!StringUtils.isEmpty(data.ycqlAdminUsername)) {
      ycqlQueryExecutor.createUser(universe, data);
    }
  }

  public JsonNode validateRequestAndExecuteQuery(
      Universe universe, RunQueryFormData runQueryFormData) {
    boolean queriesEnabled =
        confGetter.getConfForScope(universe, UniverseConfKeys.enableDbQueryApi);
    if (!queriesEnabled) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "API is disabled. Can be enabled via "
              + UniverseConfKeys.enableDbQueryApi.getKey()
              + " runtime conf flag");
    }

    NodeDetails node;
    if (StringUtils.isEmpty(runQueryFormData.getNodeName())) {
      node = CommonUtils.getARandomLiveTServer(universe);
    } else {
      node = universe.getNode(runQueryFormData.getNodeName());
      if (node == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Node " + runQueryFormData.getNodeName() + " not found");
      }
      if (!node.isActive()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Node "
                + runQueryFormData.getNodeName()
                + " is not active. Current state: "
                + node.state.name());
      }
    }

    return ysqlQueryExecutor.executeQueryInNodeShell(universe, runQueryFormData, node);
  }

  public UUID configureYSQL(
      ConfigureDBApiParams requestParams, Customer customer, Universe universe) {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    // Check runtime flag for connection pooling.
    if (requestParams.enableConnectionPooling) {
      boolean allowConnectionPooling =
          confGetter.getGlobalConf(GlobalConfKeys.allowConnectionPooling);
      if (!allowConnectionPooling) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Connection pooling is not allowed. Please set runtime flag"
                + " 'yb.universe.allow_connection_pooling' to true.");
      }

      String softwareVersion =
          universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
      if (universe
          .getUniverseDetails()
          .softwareUpgradeState
          .equals(SoftwareUpgradeState.PreFinalize)) {
        if (universe.getUniverseDetails().prevYBSoftwareConfig != null) {
          softwareVersion = universe.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion();
        }
      }

      if (Util.compareYBVersions(
              softwareVersion,
              CONNECTION_POOLING_STABLE_VERSION,
              CONNECTION_POOLING_PREVIEW_VERSION,
              true)
          < 0) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Connection pooling needs minimum stable version '%s' and preview version '%s'.",
                CONNECTION_POOLING_STABLE_VERSION, CONNECTION_POOLING_PREVIEW_VERSION));
      }

      if (universe
          .getUniverseDetails()
          .getPrimaryCluster()
          .userIntent
          .providerType
          .equals(Common.CloudType.kubernetes)) {
        if (requestParams.communicationPorts.ysqlServerRpcPort
            != KubernetesCommandExecutor.DEFAULT_YSQL_SERVER_RPC_PORT) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Custom YSQL RPC port is not yet supported for Kubernetes universes.");
        }
        if (requestParams.communicationPorts.internalYsqlServerRpcPort
            != KubernetesCommandExecutor.DEFAULT_INTERNAL_YSQL_SERVER_RPC_PORT) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "Custom Internal YSQL RPC port is not yet supported for Kubernetes universes.");
        }
      }
    }
    // Verify request params
    requestParams.verifyParams(universe, true);
    gFlagsValidation.validateConnectionPoolingGflags(
        universe, requestParams.connectionPoolingGflags);
    requestParams.validatePassword(policyService);
    requestParams.validateYSQLTables(universe, tableHandler);
    TaskType taskType =
        userIntent.providerType.equals(Common.CloudType.kubernetes)
            ? TaskType.ConfigureDBApisKubernetes
            : TaskType.ConfigureDBApis;
    UUID taskUUID = commissioner.submit(taskType, requestParams);
    LOG.info(
        "Submitted {} for {} : {}, task uuid = {}.",
        taskType,
        universe.getUniverseUUID(),
        universe.getName(),
        taskUUID);
    CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.ConfigureDBApis,
        universe.getName());
    LOG.info(
        "Saved task uuid {} in customer tasks table for universe {} : {}.",
        taskUUID,
        universe.getUniverseUUID(),
        universe.getName());
    return taskUUID;
  }

  public UUID configureYCQL(
      ConfigureDBApiParams requestParams, Customer customer, Universe universe) {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    // Verify request params
    requestParams.verifyParams(universe, true);
    requestParams.validatePassword(policyService);
    requestParams.validateYCQLTables(universe, tableHandler);
    TaskType taskType =
        userIntent.providerType.equals(Common.CloudType.kubernetes)
            ? TaskType.ConfigureDBApisKubernetes
            : TaskType.ConfigureDBApis;
    UUID taskUUID = commissioner.submit(taskType, requestParams);
    LOG.info(
        "Submitted {} for {} : {}, task uuid = {}.",
        taskType,
        universe.getUniverseUUID(),
        universe.getName(),
        taskUUID);
    CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.ConfigureDBApis,
        universe.getName());
    LOG.info(
        "Saved task uuid {} in customer tasks table for universe {} : {}.",
        taskUUID,
        universe.getUniverseUUID(),
        universe.getName());
    return taskUUID;
  }

  @VisibleForTesting
  public void setAppConfig(Config config) {
    appConfig = config;
  }
}
