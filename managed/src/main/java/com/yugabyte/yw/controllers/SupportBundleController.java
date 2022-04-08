// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.typesafe.config.Config;
import com.google.inject.Inject;
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
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.InputStream;
import java.text.SimpleDateFormat;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.Date;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Result;
import play.libs.Json;

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

    // Temporarily cannot create for onprem or k8s properly. Will result in empty directories
    CloudType cloudType = universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType;
    Boolean k8s_enabled = runtimeConfigFactory.globalRuntimeConf().getBoolean(K8S_ENABLED);
    Boolean onprem_enabled = runtimeConfigFactory.globalRuntimeConf().getBoolean(ONPREM_ENABLED);
    if ((cloudType == CloudType.onprem && !onprem_enabled)
        || (cloudType == CloudType.kubernetes && !k8s_enabled)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot currently create support bundle for onprem or k8s clusters");
    }

    SupportBundle supportBundle = SupportBundle.create(bundleData, universe);
    SupportBundleTaskParams taskParams =
        new SupportBundleTaskParams(supportBundle, bundleData, customer, universe);
    UUID taskUUID = commissioner.submit(TaskType.CreateSupportBundle, taskParams);
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
      response = SupportBundle.class,
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
      response = Object.class,
      responseContainer = "List",
      nickname = "listSupportBundle")
  public Result list(UUID customerUUID, UUID universeUUID) {
    List<SupportBundle> supportBundles = SupportBundle.getAll(universeUUID);
    List<ObjectNode> supportBundlesResponse =
        supportBundles
            .stream()
            .map(
                supportBundle -> {
                  return getSupportBundleResponse(supportBundle);
                })
            .filter(Objects::nonNull)
            .collect(Collectors.toList());
    return PlatformResults.withData(supportBundlesResponse);
  }

  @ApiOperation(
      value = "Get a support bundle from a universe",
      response = Object.class,
      nickname = "getSupportBundle")
  public Result get(UUID customerUUID, UUID universeUUID, UUID supportBundleUUID) {
    SupportBundle supportBundle = SupportBundle.getOrBadRequest(supportBundleUUID);
    ObjectNode supportBundleResponse = getSupportBundleResponse(supportBundle);
    return PlatformResults.withData(supportBundleResponse);
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

  public ObjectNode getSupportBundleResponse(SupportBundle supportBundle) {

    ObjectMapper mapper = new ObjectMapper();
    ObjectNode supportBundleResponse = mapper.valueToTree(supportBundle);
    if (supportBundle.getStatus() == SupportBundleStatusType.Success) {
      Date creationDate, expirationDate;
      try {
        creationDate = supportBundleUtil.getDateFromBundleFileName(supportBundle.getFileName());
      } catch (Exception e) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Failed to parse supportBundle filename %s for creation date",
                supportBundle.getFileName()));
      }
      int defaultRetentionDays = config.getInt("yb.support_bundle.default_retention_days");
      expirationDate = supportBundleUtil.getDateNDaysAfter(creationDate, defaultRetentionDays);
      String datePattern = "yyyy-MM-dd";
      SimpleDateFormat dateFormat = new SimpleDateFormat(datePattern);
      supportBundleResponse.put("creationDate", dateFormat.format(creationDate));
      supportBundleResponse.put("expirationDate", dateFormat.format(expirationDate));
    }
    return supportBundleResponse;
  }
}
