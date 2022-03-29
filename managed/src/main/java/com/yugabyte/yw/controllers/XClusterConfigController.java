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
import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.services.YBClientService;
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
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
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
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.yb.cdc.CdcConsumer;
import org.yb.cdc.CdcConsumer.ProducerEntryPB;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import play.libs.Json;
import play.mvc.Result;

@Api(
    value = "Asynchronous Replication",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class XClusterConfigController extends AuthenticatedController {

  private final Commissioner commissioner;
  private final YBClientService ybClientService;
  private final MetricQueryHelper metricQueryHelper;

  @Inject
  public XClusterConfigController(
      Commissioner commissioner,
      YBClientService ybClientService,
      MetricQueryHelper metricQueryHelper) {
    this.commissioner = commissioner;
    this.ybClientService = ybClientService;
    this.metricQueryHelper = metricQueryHelper;
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

    // Parse and validate request
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfigCreateFormData createFormData = parseCreateFormData();
    Universe.getValidUniverseOrBadRequest(createFormData.sourceUniverseUUID, customer);
    Universe.getValidUniverseOrBadRequest(createFormData.targetUniverseUUID, customer);
    checkConfigDoesNotAlreadyExist(
        createFormData.name, createFormData.sourceUniverseUUID, createFormData.targetUniverseUUID);

    // Create xCluster config object
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Init);

    // Submit task to set up xCluster config
    XClusterConfigTaskParams taskParams =
        new XClusterConfigTaskParams(xClusterConfig, createFormData);
    UUID taskUUID = commissioner.submit(TaskType.CreateXClusterConfig, taskParams);
    CustomerTask.create(
        customer,
        xClusterConfig.uuid,
        taskUUID,
        CustomerTask.TargetType.XClusterConfig,
        CustomerTask.TaskType.CreateXClusterConfig,
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
      // If necessary, update cached CDC stream IDs
      Map<String, String> cachedStreams = xClusterConfig.getStreams();
      if (cachedStreams.containsValue("")) {
        cachedStreams = refreshStreamIdsCache(xClusterConfig);
      }

      log.info(
          "Querying lag metrics for XClusterConfig({}) using CDC stream IDs: {}",
          xClusterConfig.uuid,
          cachedStreams.values());

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
      String streamIdFilter = String.join("|", cachedStreams.values());
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
    resp.xClusterConfig = xClusterConfig;
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

    // Submit task to edit xCluster config
    XClusterConfigTaskParams params = new XClusterConfigTaskParams(xClusterConfig, editFormData);
    UUID taskUUID = commissioner.submit(TaskType.EditXClusterConfig, params);
    CustomerTask.create(
        customer,
        xClusterConfig.uuid,
        taskUUID,
        CustomerTask.TargetType.XClusterConfig,
        CustomerTask.TaskType.EditXClusterConfig,
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

    // Submit task to delete xCluster config
    XClusterConfigTaskParams params = new XClusterConfigTaskParams(xClusterConfig);
    UUID taskUUID = commissioner.submit(TaskType.DeleteXClusterConfig, params);
    CustomerTask.create(
        customer,
        xclusterConfigUUID,
        taskUUID,
        CustomerTask.TargetType.XClusterConfig,
        CustomerTask.TaskType.DeleteXClusterConfig,
        xClusterConfig.name);

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
        TargetType.Universe,
        CustomerTask.TaskType.SyncXClusterConfig,
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
    XClusterConfigCreateFormData formData =
        formFactory.getFormDataOrBadRequest(
            request().body().asJson(), XClusterConfigCreateFormData.class);

    validateTables(formData.tables);

    return formData;
  }

  private XClusterConfigEditFormData parseEditFormData() {
    XClusterConfigEditFormData formData =
        formFactory.getFormDataOrBadRequest(
            request().body().asJson(), XClusterConfigEditFormData.class);

    // Ensure exactly one edit form field is specified
    // TODO: There must be a better way to do this.
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

  private Map<String, String> refreshStreamIdsCache(XClusterConfig xClusterConfig) {
    log.info("Updating CDC stream ID cache for XClusterConfig({})", xClusterConfig.uuid);

    Map<String, String> cachedStreams = xClusterConfig.getStreams();

    // Get Universe config
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID);
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    YBClient client =
        ybClientService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate);
    CatalogEntityInfo.SysClusterConfigEntryPB config;
    try {
      config = client.getMasterClusterConfig().getConfig();
    } catch (Exception e) {
      String errorMsg =
          String.format("Failed to get universe config, skipping cache update: %s", e.getMessage());
      log.error(errorMsg);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, errorMsg);
    }

    // Parse replication group metadata
    Map<String, ProducerEntryPB> replicationGroups =
        config.getConsumerRegistry().getProducerMapMap();
    ProducerEntryPB replicationGroup =
        replicationGroups.get(xClusterConfig.getReplicationGroupName());
    if (replicationGroup == null) {
      String errorMsg = "No replication group found, skipping cache update";
      log.error(errorMsg);
      throw new PlatformServiceException(NOT_FOUND, errorMsg);
    }

    // Parse CDC stream IDs
    Map<String, CdcConsumer.StreamEntryPB> replicationStreams = replicationGroup.getStreamMapMap();
    Map<String, String> streamMap =
        replicationStreams
            .entrySet()
            .stream()
            .collect(Collectors.toMap(e -> e.getValue().getProducerTableId(), Map.Entry::getKey));

    // If Platform's table set is outdated, log error and throw exception
    if (!streamMap.keySet().equals(cachedStreams.keySet())) {
      Set<String> cachedMissing = Sets.difference(streamMap.keySet(), cachedStreams.keySet());
      Set<String> actualMissing = Sets.difference(cachedStreams.keySet(), streamMap.keySet());
      String errorMsg =
          String.format(
              "Detected table set mismatch (cached missing=%s, actual missing=%s).",
              cachedMissing, actualMissing);
      log.error(errorMsg);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, errorMsg);
    }

    // Update cached CDC stream IDs and return
    xClusterConfig.setTables(streamMap);
    return xClusterConfig.getStreams();
  }
}
