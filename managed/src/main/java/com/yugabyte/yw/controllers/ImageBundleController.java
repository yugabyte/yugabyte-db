package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.handlers.ImageBundleHandler;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
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
import java.util.List;
import java.util.UUID;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Image Bundle Management",
    tags = "preview",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class ImageBundleController extends AuthenticatedController {

  @Inject ImageBundleHandler imageBundleHandler;

  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      value = "Create a image bundle",
      response = ImageBundle.class,
      nickname = "createImageBundle")
  @ApiImplicitParams(
      @ApiImplicitParam(
          value = "CreateImageBundleRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.ImageBundle",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
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
      notes = "WARNING: This is a preview API that could change.",
      value = "List image bundles",
      response = ImageBundle.class,
      responseContainer = "List",
      nickname = "getListOfImageBundles")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
  public Result list(UUID customerUUID, UUID providerUUID, @Nullable String arch) {
    Provider.getOrBadRequest(customerUUID, providerUUID);
    List<ImageBundle> imageBundles;
    if (arch == null) {
      imageBundles = ImageBundle.getAll(providerUUID);
    } else {
      try {
        Architecture.valueOf(arch);
      } catch (IllegalArgumentException e) {
        throw new PlatformServiceException(
            BAD_REQUEST, String.format("Specify a valid arch type: %s", arch));
      }
      imageBundles = ImageBundle.getBundlesForArchType(providerUUID, arch);
    }
    return PlatformResults.withData(imageBundles);
  }

  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      value = "Get a image bundle",
      response = ImageBundle.class,
      nickname = "getImageBundle")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
  public Result index(UUID customerUUID, UUID providerUUID, UUID imageBundleUUID) {
    Provider.getOrBadRequest(customerUUID, providerUUID);
    ImageBundle bundle = ImageBundle.getOrBadRequest(providerUUID, imageBundleUUID);
    return PlatformResults.withData(bundle);
  }

  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      value = "Update a image bundle",
      response = ImageBundle.class,
      nickname = "editImageBundle")
  @ApiImplicitParams(
      @ApiImplicitParam(
          value = "EditImageBundleRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.ImageBundle",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
  public Result edit(UUID customerUUID, UUID providerUUID, UUID iBUUID, Http.Request request) {
    final Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    ImageBundle bundle = parseJsonAndValidate(request, ImageBundle.class);
    checkImageBundleUsageInUniverses(providerUUID, iBUUID, bundle);

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

  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      value = "Delete a image bundle",
      response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
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
    checkImageBundleUsageInUniverses(providerUUID, imageBundleUUID, null);
  }

  private void checkImageBundleUsageInUniverses(
      UUID providerUUID, UUID imageBundleUUID, ImageBundle bundle) {
    ImageBundle iBundle = ImageBundle.getOrBadRequest(providerUUID, imageBundleUUID);
    long universeCount = iBundle.getUniverseCount();

    if (universeCount > 0 && bundle == null) {
      throw new PlatformServiceException(
          FORBIDDEN,
          String.format(
              "There %s %d universe%s using this imageBundle, cannot delete",
              universeCount > 1 ? "are" : "is", universeCount, universeCount > 1 ? "s" : ""));
    } else if (universeCount > 0 && !bundle.allowUpdateDuringUniverseAssociation(iBundle)) {
      throw new PlatformServiceException(
          FORBIDDEN,
          String.format(
              "There %s %d universe%s using this imageBundle, cannot modify",
              universeCount > 1 ? "are" : "is", universeCount, universeCount > 1 ? "s" : ""));
    }
  }
}
