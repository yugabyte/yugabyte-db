// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.certmgmt.castore.CustomCAStoreManager;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.CustomCACertParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.CustomCaCertificateInfo;
import com.yugabyte.yw.models.Customer;
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
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Slf4j
@Api(
    value = "Custom CA Certificates",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CustomCAStoreController extends AuthenticatedController {

  private final CustomCAStoreManager customCAStoreManager;
  private final TokenAuthenticator tokenAuthenticator;

  @Inject
  public CustomCAStoreController(
      CustomCAStoreManager customCAStoreManager, TokenAuthenticator tokenAuthenticator) {
    this.customCAStoreManager = customCAStoreManager;
    this.tokenAuthenticator = tokenAuthenticator;
  }

  @ApiOperation(value = "Add a named custom CA certificate", response = UUID.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "X509CACertificate",
          value = "CA certificate contents in 'X509' format",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.CustomCACertParams",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.OTHER,
                action = Action.SUPER_ADMIN_ACTIONS),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result addCA(UUID customerId, Http.Request request) {
    if (!customCAStoreManager.isEnabled()) {
      throw new PlatformServiceException(
          UNAUTHORIZED, "Custom CA trust-store feature is not enabled on this YBA");
    }

    log.debug("Received call to add CA certificate");
    Customer.getOrBadRequest(customerId);
    CustomCACertParams certParams = parseJsonAndValidate(request, CustomCACertParams.class);

    // Register the cert.
    String name = certParams.getName();
    String contents = certParams.getContents();

    boolean isSuperAdmin = tokenAuthenticator.superAdminAuthentication(request);
    if (!isSuperAdmin) {
      throw new PlatformServiceException(
          UNAUTHORIZED, "Only super-admin users can add CA certificates");
    }

    UUID certId =
        customCAStoreManager.addCACert(
            customerId, name, contents, AppConfigHelper.getStoragePath());
    log.info("Certificate registered");

    // Successfully created and 'certId' is available now.
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CustomCACertificate,
            String.format("%s-%s", name, certId.toString()),
            Audit.ActionType.Upload,
            Json.toJson(certParams));
    return PlatformResults.withData(certId);
  }

  @ApiOperation(
      value = "List all custom CA certificates of a customer",
      responseContainer = "List",
      response = CustomCaCertificateInfo.class,
      nickname = "listAllCustomCaCertificates")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result listCAs(UUID customerId) {
    Customer.getOrBadRequest(customerId);
    return PlatformResults.withData(customCAStoreManager.getAll());
  }

  @ApiOperation(
      value = "Download a custom CA certificates of a customer",
      response = CustomCaCertificateInfo.class,
      nickname = "getAllCustomCaCertificates")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result downloadCA(UUID customerId, UUID certId, Http.Request request) {
    Customer.getOrBadRequest(customerId);
    CustomCaCertificateInfo cert = customCAStoreManager.get(customerId, certId);
    return PlatformResults.withData(cert);
  }

  @ApiOperation(value = "Update a named custom CA certificate", response = UUID.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "X509CACertificate",
          value = "CA certificate contents in 'X509' format",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.CustomCACertParams",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.OTHER,
                action = Action.SUPER_ADMIN_ACTIONS),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result updateCA(UUID customerId, UUID oldCertId, Http.Request request) {
    if (!customCAStoreManager.isEnabled()) {
      throw new PlatformServiceException(
          UNAUTHORIZED, "Custom CA trust-store feature is not enabled on this YBA");
    }

    log.debug("Received request to update CA certificate {}", oldCertId);
    Customer.getOrBadRequest(customerId);
    CustomCACertParams certParams = parseJsonAndValidate(request, CustomCACertParams.class);

    String name = certParams.getName();
    String contents = certParams.getContents();

    boolean isSuperAdmin = tokenAuthenticator.superAdminAuthentication(request);
    if (!isSuperAdmin) {
      throw new PlatformServiceException(
          UNAUTHORIZED, "Only super-admin users can update CA certificates");
    }

    UUID newCertId =
        customCAStoreManager.updateCA(
            customerId, oldCertId, name, contents, AppConfigHelper.getStoragePath());
    log.info("Certificate {} refreshed with {}", oldCertId, newCertId);

    // Successfully updated and 'newCertId' is available now.
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CustomCACertificate,
            String.format("%s-%s", name, newCertId.toString()),
            Audit.ActionType.Update,
            Json.toJson(certParams));
    return PlatformResults.withData(newCertId);
  }

  @ApiOperation(
      value = "Delete a named custom CA certificate",
      response = YBPSuccess.class,
      nickname = "Delete custom CA certificate")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.OTHER,
                action = Action.SUPER_ADMIN_ACTIONS),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result deleteCA(UUID customerId, UUID certId, Http.Request request) {
    if (!customCAStoreManager.isEnabled()) {
      throw new PlatformServiceException(
          UNAUTHORIZED, "Custom CA trust-store feature is not enabled on this YBA");
    }

    log.debug("Received request to delete cert {}", certId);
    Customer.getOrBadRequest(customerId);

    boolean isSuperAdmin = tokenAuthenticator.superAdminAuthentication(request);
    if (!isSuperAdmin) {
      throw new PlatformServiceException(
          UNAUTHORIZED, "Only super-admin users can delete CA certificates");
    }

    boolean deleted =
        customCAStoreManager.deleteCA(customerId, certId, AppConfigHelper.getStoragePath());
    if (!deleted) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, String.format("CA certificate %s could not be deleted", certId));
    }
    log.info("Certificate {} deleted", certId);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CustomCACertificate,
            certId.toString(),
            Audit.ActionType.Delete);
    return YBPSuccess.withMessage(String.format("Deleted CA certificate %s", certId));
  }
}
