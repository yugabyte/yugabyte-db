/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.BackupUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.forms.XClusterConfigGetResp;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.time.Duration;
import java.time.Instant;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Asynchronous Replication",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class XClusterConfigController extends AuthenticatedController {

  private final Commissioner commissioner;
  private final MetricQueryHelper metricQueryHelper;
  private final BackupUtil backupUtil;
  private final CustomerConfigService customerConfigService;

  @Inject
  public XClusterConfigController(
      Commissioner commissioner,
      MetricQueryHelper metricQueryHelper,
      BackupUtil backupUtil,
      CustomerConfigService customerConfigService) {
    this.commissioner = commissioner;
    this.metricQueryHelper = metricQueryHelper;
    this.backupUtil = backupUtil;
    this.customerConfigService = customerConfigService;
  }

  /**
   * API that creates an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "createXClusterConfig",
      value = "Create xcluster config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_replication_create_form_data",
          value = "XCluster Replication Create Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterConfigCreateFormData",
          paramType = "body",
          required = true))
  public Result create(UUID customerUUID) {
    log.info("Received create XClusterConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfigCreateFormData createFormData = parseCreateFormData();
    Universe.getValidUniverseOrBadRequest(createFormData.sourceUniverseUUID, customer);
    Universe targetUniverse =
        Universe.getValidUniverseOrBadRequest(createFormData.targetUniverseUUID, customer);
    checkConfigDoesNotAlreadyExist(
        createFormData.name, createFormData.sourceUniverseUUID, createFormData.targetUniverseUUID);
    validateBootstrapParams(createFormData, customerUUID);

    // Create xCluster config object.
    XClusterConfig xClusterConfig = XClusterConfig.create(createFormData);
    verifyTaskAllowed(xClusterConfig, TaskType.CreateXClusterConfig);

    // Submit task to set up xCluster config.
    XClusterConfigTaskParams taskParams =
        new XClusterConfigTaskParams(xClusterConfig, createFormData);
    UUID taskUUID = commissioner.submit(TaskType.CreateXClusterConfig, taskParams);
    CustomerTask.create(
        customer,
        targetUniverse.universeUUID,
        taskUUID,
        CustomerTask.TargetType.XClusterConfig,
        CustomerTask.TaskType.Create,
        xClusterConfig.name);

    log.info("Submitted create XClusterConfig({}), task {}", xClusterConfig.uuid, taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.XClusterConfig,
            Objects.toString(xClusterConfig.uuid, null),
            Audit.ActionType.Create,
            Json.toJson(createFormData),
            taskUUID);
    return new YBPTask(taskUUID, xClusterConfig.uuid).asResult();
  }

  /**
   * API that gets an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "getXClusterConfig",
      value = "Get xcluster config",
      response = XClusterConfigGetResp.class)
  public Result get(UUID customerUUID, UUID xclusterConfigUUID) {
    log.info("Received get XClusterConfig({}) request", xclusterConfigUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xclusterConfigUUID);

    JsonNode lagMetricData;

    try {
      Set<String> streamIds = xClusterConfig.getStreamIdsWithReplicationSetup();
      log.info(
          "Querying lag metrics for XClusterConfig({}) using CDC stream IDs: {}",
          xClusterConfig.uuid,
          streamIds);

      // Query for replication lag
      Map<String, String> metricParams = new HashMap<>();
      String metric = "tserver_async_replication_lag_micros";
      metricParams.put("metrics[0]", metric);
      String startTime = Long.toString(Instant.now().minus(Duration.ofMinutes(1)).getEpochSecond());
      metricParams.put("start", startTime);
      ObjectNode filterJson = Json.newObject();
      Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.sourceUniverseUUID);
      String nodePrefix = sourceUniverse.getUniverseDetails().nodePrefix;
      filterJson.put("node_prefix", nodePrefix);
      String streamIdFilter = String.join("|", streamIds);
      filterJson.put("stream_id", streamIdFilter);
      metricParams.put("filters", Json.stringify(filterJson));
      lagMetricData =
          metricQueryHelper.query(
              Collections.singletonList(metric), metricParams, Collections.emptyMap());
    } catch (Exception e) {
      String errorMsg =
          String.format(
              "Failed to get lag metric data for XClusterConfig(%s): %s",
              xClusterConfig.uuid, e.getMessage());
      log.error(errorMsg);
      lagMetricData = Json.newObject().put("error", errorMsg);
    }

    // Wrap XClusterConfig with lag metric data and return
    XClusterConfigGetResp resp = new XClusterConfigGetResp();
    resp.xclusterConfig = xClusterConfig;
    resp.lag = lagMetricData;
    return PlatformResults.withData(resp);
  }

  /**
   * API that edits an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "editXClusterConfig",
      value = "Edit xcluster config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_replication_edit_form_data",
          value = "XCluster Replication Edit Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterConfigEditFormData",
          paramType = "body",
          required = true))
  public Result edit(UUID customerUUID, UUID xclusterConfigUUID) {
    log.info("Received edit XClusterConfig({}) request", xclusterConfigUUID);

    // Parse and validate request
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfigEditFormData editFormData = parseEditFormData();
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xclusterConfigUUID);
    verifyTaskAllowed(xClusterConfig, TaskType.EditXClusterConfig);

    // Only one type of edit is allowed in one call.
    int editName = editFormData.name == null ? 0 : 1;
    int editStatus = editFormData.status == null ? 0 : 1;
    int editTables = editFormData.tables == null ? 0 : 1;
    if (editName + editStatus + editTables != 1) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Exactly one edit request (either editName, editStatus, editTables) is "
              + "allowed in one call.");
    }

    // If renaming, verify xcluster replication with same name (between same source/target)
    // does not already exist.
    if (editFormData.name != null) {
      if (XClusterConfig.getByNameSourceTarget(
              editFormData.name,
              xClusterConfig.sourceUniverseUUID,
              xClusterConfig.targetUniverseUUID)
          != null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "XClusterConfig with same name already exists");
      }
    }

    Universe targetUniverse =
        Universe.getValidUniverseOrBadRequest(xClusterConfig.targetUniverseUUID, customer);
    // Submit task to edit xCluster config
    XClusterConfigTaskParams params = new XClusterConfigTaskParams(xClusterConfig, editFormData);
    UUID taskUUID = commissioner.submit(TaskType.EditXClusterConfig, params);
    CustomerTask.create(
        customer,
        targetUniverse.universeUUID,
        taskUUID,
        CustomerTask.TargetType.XClusterConfig,
        CustomerTask.TaskType.Edit,
        xClusterConfig.name);

    log.info("Submitted edit XClusterConfig({}), task {}", xClusterConfig.uuid, taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.XClusterConfig,
            xclusterConfigUUID.toString(),
            Audit.ActionType.Edit,
            Json.toJson(editFormData),
            taskUUID);
    return new YBPTask(taskUUID, xClusterConfig.uuid).asResult();
  }

  /**
   * API that deletes an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "deleteXClusterConfig",
      value = "Delete xcluster config",
      response = YBPTask.class)
  public Result delete(UUID customerUUID, UUID xclusterConfigUUID) {
    log.info("Received delete XClusterConfig({}) request", xclusterConfigUUID);

    // Parse and validate request
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xclusterConfigUUID);
    verifyTaskAllowed(xClusterConfig, TaskType.DeleteXClusterConfig);

    Universe sourceUniverse = null;
    Universe targetUniverse = null;
    if (xClusterConfig.sourceUniverseUUID != null) {
      sourceUniverse =
          Universe.getValidUniverseOrBadRequest(xClusterConfig.sourceUniverseUUID, customer);
    }
    if (xClusterConfig.targetUniverseUUID != null) {
      targetUniverse =
          Universe.getValidUniverseOrBadRequest(xClusterConfig.targetUniverseUUID, customer);
    }

    // Submit task to delete xCluster config
    XClusterConfigTaskParams params = new XClusterConfigTaskParams(xClusterConfig);
    UUID taskUUID = commissioner.submit(TaskType.DeleteXClusterConfig, params);
    if (targetUniverse != null) {
      CustomerTask.create(
          customer,
          targetUniverse.universeUUID,
          taskUUID,
          CustomerTask.TargetType.XClusterConfig,
          CustomerTask.TaskType.Delete,
          xClusterConfig.name);
    } else if (sourceUniverse != null) {
      CustomerTask.create(
          customer,
          sourceUniverse.universeUUID,
          taskUUID,
          CustomerTask.TargetType.XClusterConfig,
          CustomerTask.TaskType.Delete,
          xClusterConfig.name);
    }
    log.info("Submitted delete XClusterConfig({}), task {}", xClusterConfig.uuid, taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.XClusterConfig,
            xclusterConfigUUID.toString(),
            Audit.ActionType.Delete,
            taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  /**
   * API that syncs target universe xCluster replication configuration with platform state.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "syncXClusterConfig",
      value = "Sync xcluster config",
      response = YBPTask.class)
  public Result sync(UUID customerUUID, UUID targetUniverseUUID) {
    log.info("Received sync XClusterConfig request for universe({})", targetUniverseUUID);

    // Parse and validate request
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe targetUniverse = Universe.getValidUniverseOrBadRequest(targetUniverseUUID, customer);

    // Submit task to sync xCluster config
    XClusterConfigTaskParams params = new XClusterConfigTaskParams(targetUniverseUUID);
    UUID taskUUID = commissioner.submit(TaskType.SyncXClusterConfig, params);
    CustomerTask.create(
        customer,
        targetUniverseUUID,
        taskUUID,
        TargetType.XClusterConfig,
        CustomerTask.TaskType.Sync,
        targetUniverse.name);

    log.info(
        "Submitted sync XClusterConfig for universe({}), task {}", targetUniverseUUID, taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe,
            targetUniverseUUID.toString(),
            Audit.ActionType.SyncXClusterConfig,
            taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  private XClusterConfigCreateFormData parseCreateFormData() {
    log.debug("Request body to create an xCluster config is {}", request().body().asJson());
    XClusterConfigCreateFormData formData =
        formFactory.getFormDataOrBadRequest(
            request().body().asJson(), XClusterConfigCreateFormData.class);

    validateTables(formData.tables);
    if (Objects.equals(formData.sourceUniverseUUID, formData.targetUniverseUUID)) {
      throw new IllegalArgumentException(
          String.format(
              "Source and target universe cannot be the same: both are %s",
              formData.sourceUniverseUUID));
    }
    return formData;
  }

  private XClusterConfigEditFormData parseEditFormData() {
    XClusterConfigEditFormData formData =
        formFactory.getFormDataOrBadRequest(
            request().body().asJson(), XClusterConfigEditFormData.class);

    // Ensure exactly one edit form field is specified
    int numEditOps = 0;
    numEditOps += (formData.name != null) ? 1 : 0;
    numEditOps += (formData.status != null) ? 1 : 0;
    numEditOps += (formData.tables != null) ? 1 : 0;
    if (numEditOps == 0) {
      throw new PlatformServiceException(BAD_REQUEST, "Must specify an edit operation");
    } else if (numEditOps > 1) {
      throw new PlatformServiceException(BAD_REQUEST, "Must perform one edit operation at a time");
    }

    validateTables(formData.tables);

    return formData;
  }

  private void checkConfigDoesNotAlreadyExist(
      String name, UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    XClusterConfig xClusterConfig =
        XClusterConfig.getByNameSourceTarget(name, sourceUniverseUUID, targetUniverseUUID);

    if (xClusterConfig != null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "xCluster config between source universe "
              + sourceUniverseUUID
              + " and target universe "
              + targetUniverseUUID
              + " with name '"
              + name
              + "' already exists.");
    }
  }

  private void validateTables(Set<String> tables) {
    // TODO: Make custom validation constraints for this.
    if (tables != null) {
      if (tables.size() == 0) {
        throw new PlatformServiceException(BAD_REQUEST, "Table set must be non-empty");
      }
    }
  }

  private void verifyTaskAllowed(XClusterConfig xClusterConfig, TaskType taskType) {
    if (!XClusterConfigTaskBase.isTaskAllowed(xClusterConfig, taskType)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "%s task is not allowed; with status `%s`, the allowed tasks are %s",
              taskType,
              xClusterConfig.status,
              XClusterConfigTaskBase.getAllowedTasks(xClusterConfig)));
    }
  }

  private void validateBootstrapParams(
      XClusterConfigCreateFormData createFormData, UUID customerUUID) {
    // Validate bootstrap parameters if there is any.
    if (createFormData.bootstrapParams != null) {
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams = createFormData.bootstrapParams;
      BackupRequestParams backupRequestParams = bootstrapParams.backupRequestParams;
      // Ensure tables in BootstrapParams is a subset of tables in the main body.
      if (!createFormData.tables.containsAll(bootstrapParams.tables)) {
        String errMsg =
            String.format(
                "The set of tables in bootstrapParams (%s) is not a subset of tables in the main "
                    + "body (%s)",
                bootstrapParams.tables, createFormData.tables);
        throw new IllegalArgumentException(errMsg);
      }

      // Fail early if parameters are invalid for bootstrapping. Support only keyspace.
      if (bootstrapParams.tables.size() > 0) {
        CustomerConfig customerConfig =
            customerConfigService.getOrBadRequest(
                customerUUID, backupRequestParams.storageConfigUUID);
        if (!customerConfig.getState().equals(CustomerConfig.ConfigState.Active)) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Cannot create backup as config is queued for deletion.");
        }
        backupUtil.validateStorageConfig(customerConfig);
        // Ensure the following parameters are not set by the user because they will be set by the
        // task based on other parameters automatically.
        if (backupRequestParams.keyspaceTableList != null) {
          throw new IllegalArgumentException(
              "backupRequestParams.keyspaceTableList must be null, table selection happens "
                  + "automatically");
        }
        if (backupRequestParams.backupType != null) {
          throw new IllegalArgumentException(
              "backupRequestParams.backupType must be null, backup type will be selected "
                  + "automatically based on tables");
        }
        if (backupRequestParams.customerUUID != null
            && !backupRequestParams.customerUUID.equals(customerUUID)) {
          String errMsg =
              String.format(
                  "backupRequestParams.customerUUID is set to a wrong customer UUID (%s). Please "
                      + "either set it to null, or use the right customer uuid (%s)",
                  backupRequestParams.customerUUID, customerUUID);
          throw new PlatformServiceException(Http.Status.BAD_REQUEST, errMsg);
        }
        if (backupRequestParams.customerUUID == null) {
          backupRequestParams.customerUUID = customerUUID;
        }
        if (backupRequestParams.universeUUID != null) {
          throw new PlatformServiceException(
              Http.Status.BAD_REQUEST, "backupRequestParams.universeUUID must be null");
        }
        if (backupRequestParams.timeBeforeDelete != 0L
            || backupRequestParams.expiryTimeUnit != null) {
          throw new PlatformServiceException(
              Http.Status.BAD_REQUEST,
              "backupRequestParams.timeBeforeDelete and "
                  + "backupRequestParams.expiryTimeUnit must be null");
        }
        // The following parameters are used for scheduled backups and should not be set for this
        // task.
        if (backupRequestParams.frequencyTimeUnit != null
            || backupRequestParams.schedulingFrequency != 0L
            || backupRequestParams.cronExpression != null
            || backupRequestParams.scheduleUUID != null
            || backupRequestParams.scheduleName != null
            || backupRequestParams.minNumBackupsToRetain != Util.MIN_NUM_BACKUPS_TO_RETAIN) {
          throw new PlatformServiceException(
              Http.Status.BAD_REQUEST,
              "Schedule backup related parameters cannot be set for this task");
        }
      }
    }
  }
}
