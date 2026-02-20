// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.handlers.SupportBundleHandler;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.forms.SupportBundleSizeEstimateResponse;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
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
import play.mvc.Http;
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
  @Inject SupportBundleHandler sbHandler;
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
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.DEBUG),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result create(UUID customerUUID, UUID universeUUID, Http.Request request) {
    JsonNode requestBody = request.body().asJson();
    SupportBundleFormData bundleData =
        formFactory.getFormDataOrBadRequest(requestBody, SupportBundleFormData.class);

    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    if (universe.getUniverseDetails().updateInProgress
        || universe.getUniverseDetails().universePaused) {
      log.info(
          "Trying to create support bundle while universe {} is "
              + "in a locked/paused state or has backup running.",
          universe.getName());
    }

    sbHandler.bundleDataValidation(bundleData, universe);

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
        universe.getName());
    log.info(
        "Saved task uuid "
            + taskUUID.toString()
            + " in customer tasks table for customer: "
            + customerUUID.toString()
            + " and universe: "
            + universeUUID.toString());

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.SupportBundle,
            Objects.toString(supportBundle.getBundleUUID(), null),
            Audit.ActionType.Create,
            taskUUID);
    return new YBPTask(taskUUID, supportBundle.getBundleUUID()).asResult();
  }

  @ApiOperation(
      value = "Download support bundle",
      nickname = "downloadSupportBundle",
      response = String.class,
      produces = "application/x-compressed")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT)),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result download(UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe.getOrBadRequest(universeUUID, customer);
    SupportBundle bundle = SupportBundle.getOrBadRequest(bundleUUID);

    if (bundle.getStatus() != SupportBundleStatusType.Success) {
      throw new PlatformServiceException(
          NOT_FOUND, String.format("No bundle found for %s", bundleUUID.toString()));
    }
    InputStream is = SupportBundle.getAsInputStream(bundleUUID);
    return ok(is)
        .as("application/x-compressed")
        .withHeader(
            "Content-Disposition",
            "attachment; filename=" + SupportBundle.get(bundleUUID).getFileName());
  }

  @ApiOperation(
      value = "List all support bundles from a universe",
      response = SupportBundle.class,
      responseContainer = "List",
      nickname = "listSupportBundle")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT)),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result list(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe.getOrBadRequest(universeUUID, customer);
    int retentionDays = config.getInt("yb.support_bundle.retention_days");
    SupportBundle.setRetentionDays(retentionDays);
    List<SupportBundle> supportBundles = SupportBundle.getAll(universeUUID);
    return PlatformResults.withData(supportBundles);
  }

  @ApiOperation(
      value = "Get a support bundle from a universe",
      response = SupportBundle.class,
      nickname = "getSupportBundle")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT)),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result get(UUID customerUUID, UUID universeUUID, UUID supportBundleUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe.getOrBadRequest(universeUUID, customer);
    int retentionDays = config.getInt("yb.support_bundle.retention_days");
    SupportBundle.setRetentionDays(retentionDays);
    SupportBundle supportBundle = SupportBundle.getOrBadRequest(supportBundleUUID);
    return PlatformResults.withData(supportBundle);
  }

  @ApiOperation(
      value = "Delete a support bundle",
      response = YBPSuccess.class,
      nickname = "deleteSupportBundle")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT)),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result delete(
      UUID customerUUID, UUID universeUUID, UUID bundleUUID, Http.Request request) {
    SupportBundle supportBundle = SupportBundle.getOrBadRequest(bundleUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe.getOrBadRequest(universeUUID, customer);

    // Throw error if support bundle is running.
    if (SupportBundleStatusType.Running.equals(supportBundle.getStatus())) {
      throw new PlatformServiceException(BAD_REQUEST, "The support bundle is in running state.");
    }

    // Delete file from disk and record from DB.
    supportBundleUtil.deleteSupportBundle(supportBundle);

    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.SupportBundle,
            bundleUUID.toString(),
            Audit.ActionType.Delete);
    log.info("Successfully deleted the support bundle: " + bundleUUID.toString());
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "List all components available in support bundle",
      response = ComponentType.class,
      responseContainer = "List",
      nickname = "listSupportBundleComponents")
  @AuthzPath
  public Result getComponents(UUID customerUUID) {
    EnumSet<ComponentType> components = EnumSet.allOf(ComponentType.class);
    return PlatformResults.withData(components);
  }

  @ApiOperation(
      value = "Estimate support bundle size for specific universe",
      nickname = "estimateSupportBundleSize",
      notes = "YbaApi Internal.",
      response = SupportBundleSizeEstimateResponse.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "supportBundle",
          value = "support bundle info",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.SupportBundleFormData",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT)),
  })
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2025.1.0.0")
  public Result estimateSize(UUID customerUUID, UUID universeUUID, Http.Request request)
      throws Exception {
    JsonNode requestBody = request.body().asJson();
    SupportBundleFormData bundleData =
        formFactory.getFormDataOrBadRequest(requestBody, SupportBundleFormData.class);

    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    sbHandler.bundleDataValidation(bundleData, universe);

    return PlatformResults.withData(sbHandler.estimateBundleSize(customer, bundleData, universe));
  }
}
