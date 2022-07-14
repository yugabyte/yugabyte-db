// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;

import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.InputStream;
import java.util.EnumSet;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Result;

@Api(
    value = "Support Bundle management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class SupportBundleController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(SupportBundleController.class);
  public static final String K8S_ENABLED = "yb.support_bundle.k8s_enabled";
  public static final String ONPREM_ENABLED = "yb.support_bundle.onprem_enabled";

  @Inject Commissioner commissioner;
  @Inject SupportBundleUtil supportBundleUtil;
  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  @Inject Config config;

  @ApiOperation(
      value = "Create support bundle for specific universe",
      nickname = "createSupportBundle",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "supportBundle",
          value = "post support bundle info",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.SupportBundleFormData",
          required = true))
  public Result create(UUID customerUUID, UUID universeUUID) {
    JsonNode requestBody = request().body().asJson();
    SupportBundleFormData bundleData =
        formFactory.getFormDataOrBadRequest(requestBody, SupportBundleFormData.class);

    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    // Do not create support bundle when either backup, update, or universe is paused
    if (universe.getUniverseDetails().backupInProgress
        || universe.getUniverseDetails().updateInProgress
        || universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Cannot create support bundle since the universe %s"
                  + "is currently in a locked/paused state or has backup running",
              universe.universeUUID));
    }

    // Temporarily cannot create for k8s properly. Will result in empty directories
    CloudType cloudType = universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType;
    Boolean k8s_enabled = runtimeConfigFactory.globalRuntimeConf().getBoolean(K8S_ENABLED);
    Boolean onprem_enabled = runtimeConfigFactory.globalRuntimeConf().getBoolean(ONPREM_ENABLED);
    if (cloudType == CloudType.onprem && !onprem_enabled) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Creating support bundle for on-prem universes is not enabled. "
              + "Please set onprem_enabled=true to create support bundle");
    }
    if (cloudType == CloudType.kubernetes && !k8s_enabled) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot currently create support bundle for k8s clusters");
    }

    SupportBundle supportBundle = SupportBundle.create(bundleData, universe);
    SupportBundleTaskParams taskParams =
        new SupportBundleTaskParams(supportBundle, bundleData, customer, universe);
    UUID taskUUID = commissioner.submit(TaskType.CreateSupportBundle, taskParams);

    // Add this task uuid to the user universe.
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.CreateSupportBundle,
        universe.name);
    log.info(
        "Saved task uuid "
            + taskUUID.toString()
            + " in customer tasks table for customer: "
            + customerUUID.toString()
            + " and universe: "
            + universeUUID.toString());

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.SupportBundle,
            Objects.toString(supportBundle.getBundleUUID(), null),
            Audit.ActionType.Create,
            requestBody,
            taskUUID);
    return new YBPTask(taskUUID, supportBundle.getBundleUUID()).asResult();
  }

  @ApiOperation(
      value = "Download support bundle",
      nickname = "downloadSupportBundle",
      response = String.class,
      produces = "application/x-compressed")
  public Result download(UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    SupportBundle bundle = SupportBundle.getOrBadRequest(bundleUUID);

    if (bundle.getStatus() != SupportBundleStatusType.Success) {
      throw new PlatformServiceException(
          NOT_FOUND, String.format("No bundle found for %s", bundleUUID.toString()));
    }
    InputStream is = SupportBundle.getAsInputStream(bundleUUID);
    response()
        .setHeader(
            "Content-Disposition",
            "attachment; filename=" + SupportBundle.get(bundleUUID).getFileName());
    return ok(is).as("application/x-compressed");
  }

  @ApiOperation(
      value = "List all support bundles from a universe",
      response = SupportBundle.class,
      responseContainer = "List",
      nickname = "listSupportBundle")
  public Result list(UUID customerUUID, UUID universeUUID) {
    int retentionDays = config.getInt("yb.support_bundle.retention_days");
    SupportBundle.setRetentionDays(retentionDays);
    List<SupportBundle> supportBundles = SupportBundle.getAll(universeUUID);
    return PlatformResults.withData(supportBundles);
  }

  @ApiOperation(
      value = "Get a support bundle from a universe",
      response = SupportBundle.class,
      nickname = "getSupportBundle")
  public Result get(UUID customerUUID, UUID universeUUID, UUID supportBundleUUID) {
    int retentionDays = config.getInt("yb.support_bundle.retention_days");
    SupportBundle.setRetentionDays(retentionDays);
    SupportBundle supportBundle = SupportBundle.getOrBadRequest(supportBundleUUID);
    return PlatformResults.withData(supportBundle);
  }

  @ApiOperation(
      value = "Delete a support bundle",
      response = YBPSuccess.class,
      nickname = "deleteSupportBundle")
  public Result delete(UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
    SupportBundle supportBundle = SupportBundle.getOrBadRequest(bundleUUID);

    // Deletes row from the support_bundle db table
    SupportBundle.delete(bundleUUID);

    // Delete the actual archive file
    supportBundleUtil.deleteFile(supportBundle.getPathObject());

    auditService()
        .createAuditEntryWithReqBody(
            ctx(), Audit.TargetType.SupportBundle, bundleUUID.toString(), Audit.ActionType.Delete);
    log.info("Successfully deleted the support bundle: " + bundleUUID.toString());
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "List all components available in support bundle",
      response = ComponentType.class,
      responseContainer = "List",
      nickname = "listSupportBundleComponents")
  public Result getComponents(UUID customerUUID) {
    EnumSet<ComponentType> components = EnumSet.allOf(ComponentType.class);
    return PlatformResults.withData(components);
  }
}
