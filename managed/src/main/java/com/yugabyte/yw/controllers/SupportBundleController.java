// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.controllers.handlers.SupportBundleHandler;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.Universe;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.IOException;
import java.io.InputStream;
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

  @Inject private SupportBundleHandler supportBundleHandler;

  @ApiOperation(
      value = "Create support bundle for specific universe",
      nickname = "createSupportBundle",
      response = YBPTask.class)
  public Result createSupportBundle(UUID customerUUID, UUID universeUUID) throws IOException {
    JsonNode requestBody = request().body().asJson();
    SupportBundleFormData bundleData =
        formFactory.getFormDataOrBadRequest(requestBody, SupportBundleFormData.class);

    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    UUID bundle_UUID = supportBundleHandler.createBundle(customer, universe, bundleData);
    return new YBPTask(bundle_UUID, bundle_UUID).asResult();
  }

  @ApiOperation(
      value = "Download support bundle",
      nickname = "getSupportBundle",
      produces = "application/x-compressed")
  public Result getSupportBundle(UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    InputStream is = SupportBundle.getAsInputStream(bundleUUID);
    response()
        .setHeader(
            "Content-Disposition",
            "attachment; filename=" + SupportBundle.get(bundleUUID).getFileName());
    return ok(is).as("application/x-compressed");
  }
}
