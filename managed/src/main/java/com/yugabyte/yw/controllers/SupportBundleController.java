// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import com.yugabyte.yw.models.helpers.TaskType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Path;
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

  @Inject Commissioner commissioner;

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
    SupportBundle supportBundle = SupportBundle.create(bundleData, universe);
    SupportBundleTaskParams taskParams =
        new SupportBundleTaskParams(supportBundle, bundleData, customer, universe);
    UUID taskUUID = commissioner.submit(TaskType.CreateSupportBundle, taskParams);
    return new YBPTask(taskUUID, supportBundle.getBundleUUID()).asResult();
  }

  @ApiOperation(
      value = "Download support bundle",
      nickname = "getSupportBundle",
      produces = "application/x-compressed")
  public Result get(UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
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
}
