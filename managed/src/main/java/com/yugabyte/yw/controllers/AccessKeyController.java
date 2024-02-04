// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.forms.PlatformResults.YBPSuccess.withMessage;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.handlers.AccessKeyHandler;
import com.yugabyte.yw.forms.AccessKeyFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
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
import java.util.Objects;
import java.util.UUID;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Access Keys",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class AccessKeyController extends AuthenticatedController {

  @Inject AccessKeyHandler accessKeyHandler;

  public static final Logger LOG = LoggerFactory.getLogger(AccessKeyController.class);

  @ApiOperation(value = "Get an access key", response = AccessKey.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result index(UUID customerUUID, UUID providerUUID, String keyCode) {
    Customer.getOrBadRequest(customerUUID);
    Provider.getOrBadRequest(customerUUID, providerUUID);

    AccessKey accessKey = AccessKey.getOrBadRequest(providerUUID, keyCode);
    accessKey.mergeProviderDetails();
    return PlatformResults.withData(accessKey);
  }

  @ApiOperation(
      value = "List access keys for a specific provider",
      response = AccessKey.class,
      responseContainer = "List")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result list(UUID customerUUID, UUID providerUUID) {
    Customer.getOrBadRequest(customerUUID);
    Provider.getOrBadRequest(customerUUID, providerUUID);

    List<AccessKey> accessKeys;
    accessKeys = AccessKey.getAll(providerUUID);
    accessKeys.forEach(AccessKey::mergeProviderDetails);
    return PlatformResults.withData(accessKeys);
  }

  @ApiOperation(
      value = "List access keys for all providers of a customer",
      response = AccessKey.class,
      responseContainer = "List")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result listAllForCustomer(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    List<UUID> providerUUIDs =
        Provider.getAll(customerUUID).stream()
            .map(provider -> provider.getUuid())
            .collect(Collectors.toList());
    List<AccessKey> accessKeys = AccessKey.getByProviderUuids(providerUUIDs);
    accessKeys.forEach(AccessKey::mergeProviderDetails);
    return PlatformResults.withData(accessKeys);
  }

  // TODO: Move this endpoint under region since this api is per region
  @ApiOperation(
      nickname = "createAccesskey",
      value =
          "Deprecated since YBA version 2.20.0.0, "
              + "Use /api/v1/customers/{cUUID}/provider/{pUUID}/edit instead"
              + " for adding the key",
      notes = "UNSTABLE - This API will undergo changes in future.",
      response = AccessKey.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "AccessKeyFormData",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AccessKeyFormData",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @Deprecated
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.20.0.0")
  public Result create(UUID customerUUID, UUID providerUUID, Http.Request request) {
    final Provider provider = Provider.getOrBadRequest(providerUUID);
    AccessKeyFormData formData =
        formFactory
            .getFormDataOrBadRequest(request, AccessKeyFormData.class)
            .get()
            .setOrValidateRequestDataWithExistingKey(provider);
    AccessKey accessKey = accessKeyHandler.create(customerUUID, provider, formData, request.body());
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.AccessKey,
            Objects.toString(accessKey.getIdKey(), null),
            Audit.ActionType.Create);
    return PlatformResults.withData(accessKey);
  }

  @ApiOperation(
      nickname = "editAccesskey",
      value = "WARNING: This is a preview API that could change. Modify the existing access Key",
      response = AccessKey.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "accesskey",
          value = "access key edit form data",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.AccessKey",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
  public Result edit(UUID customerUUID, UUID providerUUID, String keyCode, Http.Request request) {
    // As part of access key edit we will be creating a new access key
    // so that if the old key is associated with some universes remains
    // functional by the time we shift completely to start using new generated keys.

    final Provider provider = Provider.getOrBadRequest(providerUUID);
    JsonNode requestBody = request.body().asJson();
    AccessKey accessKey = formFactory.getFormDataOrBadRequest(requestBody, AccessKey.class);

    AccessKey newAccessKey = accessKeyHandler.edit(customerUUID, provider, accessKey, keyCode);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.AccessKey,
            Objects.toString(newAccessKey.getIdKey(), null),
            Audit.ActionType.Edit);
    return PlatformResults.withData(newAccessKey);
  }

  @ApiOperation(
      nickname = "delete_accesskey",
      value = "Delete an access key",
      response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result delete(UUID customerUUID, UUID providerUUID, String keyCode, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    long universesCount = provider.getUniverseCount();
    if (universesCount > 0) {
      throw new PlatformServiceException(
          FORBIDDEN, "Cannot delete the access key for the provider in use!");
    }

    AccessKey accessKey = AccessKey.getOrBadRequest(providerUUID, keyCode);
    LOG.info(
        "Deleting access key {} for customer {}, provider {}", keyCode, customerUUID, providerUUID);

    accessKeyHandler.delete(accessKey);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.AccessKey,
            Objects.toString(accessKey.getIdKey(), null),
            Audit.ActionType.Delete);
    return withMessage("Deleted KeyCode: " + keyCode);
  }
}
