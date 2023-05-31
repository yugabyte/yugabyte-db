package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.controllers.handlers.ImageBundleHandler;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.Provider;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.List;
import java.util.UUID;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Image Bundle Management",
    tags = "preview",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class ImageBundleController extends AuthenticatedController {

  @Inject ImageBundleHandler imageBundleHandler;

  @ApiOperation(
      value = "Create a image bundle",
      response = ImageBundle.class,
      nickname = "createImageBundle")
  @ApiImplicitParams(
      @ApiImplicitParam(
          value = "CreateImageBundleRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.ImageBundle",
          required = true))
  public Result create(UUID customerUUID, UUID providerUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    final Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    ImageBundle bundle = parseJsonAndValidate(request, ImageBundle.class);
    UUID taskUUID = imageBundleHandler.create(customer, provider, bundle);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CloudProvider,
            providerUUID.toString(),
            Audit.ActionType.CreateImageBundle,
            Json.toJson(bundle));
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(
      value = "List image bundles",
      response = ImageBundle.class,
      responseContainer = "List",
      nickname = "getListOfImageBundles")
  public Result list(UUID customerUUID, UUID providerUUID) {
    Provider.getOrBadRequest(customerUUID, providerUUID);
    List<ImageBundle> imageBundles = ImageBundle.getAll(providerUUID);
    return PlatformResults.withData(imageBundles);
  }

  @ApiOperation(
      value = "Get a image bundle",
      response = ImageBundle.class,
      nickname = "getImageBundle")
  public Result index(UUID customerUUID, UUID providerUUID, UUID imageBundleUUID) {
    Provider.getOrBadRequest(customerUUID, providerUUID);
    ImageBundle bundle = ImageBundle.getOrBadRequest(providerUUID, imageBundleUUID);
    return PlatformResults.withData(bundle);
  }

  @ApiOperation(
      value = "Update a image bundle",
      response = ImageBundle.class,
      nickname = "editImageBundle")
  @ApiImplicitParams(
      @ApiImplicitParam(
          value = "EditImageBundleRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.ImageBundle",
          required = true))
  public Result edit(UUID customerUUID, UUID providerUUID, UUID iBUUID, Http.Request request) {
    final Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    checkImageBundleUsageInUniverses(providerUUID, iBUUID);

    ImageBundle bundle = parseJsonAndValidate(request, ImageBundle.class);
    ImageBundle cBundle = imageBundleHandler.edit(provider, iBUUID, bundle);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CloudProvider,
            providerUUID.toString(),
            Audit.ActionType.EditImageBundle,
            Json.toJson(bundle));
    return PlatformResults.withData(cBundle);
  }

  @ApiOperation(value = "Delete a image bundle", response = YBPSuccess.class)
  public Result delete(UUID customerUUID, UUID providerUUID, UUID iBUUID, Http.Request request) {
    checkImageBundleUsageInUniverses(providerUUID, iBUUID);
    imageBundleHandler.delete(providerUUID, iBUUID);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CloudProvider,
            providerUUID.toString(),
            Audit.ActionType.DeleteImageBundle);
    return YBPSuccess.empty();
  }

  private void checkImageBundleUsageInUniverses(UUID providerUUID, UUID imageBundleUUID) {
    ImageBundle iBundle = ImageBundle.getOrBadRequest(providerUUID, imageBundleUUID);
    long universeCount = iBundle.getUniverseCount();

    if (universeCount > 0) {
      throw new PlatformServiceException(
          FORBIDDEN,
          String.format(
              "There %s %d universe%s using this imageBundle, cannot modify",
              universeCount > 1 ? "are" : "is", universeCount, universeCount > 1 ? "s" : ""));
    }
  }
}
