// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.CustomerLicenseManager;
import com.yugabyte.yw.common.FileHelperService;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.CustomerLicenseFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerLicense;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Objects;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Files.TemporaryFile;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Http.MultipartFormData;
import play.mvc.Http.MultipartFormData.FilePart;
import play.mvc.Result;

@Api(
    value = "License management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CustomerLicenseController extends AuthenticatedController {

  @Inject CustomerLicenseManager cLicenseManager;

  @Inject FileHelperService fileHelperService;

  public static final Logger LOG = LoggerFactory.getLogger(CustomerLicenseController.class);

  @ApiOperation(
      nickname = "upload_license",
      value = "Uploads the license",
      response = CustomerLicense.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result upload(UUID customerUUID, Http.Request request) throws IOException {
    Customer.getOrBadRequest(customerUUID);
    CustomerLicenseFormData formData =
        formFactory.getFormDataOrBadRequest(request, CustomerLicenseFormData.class).get();

    String licenseType = formData.licenseType;
    String licenseContent = formData.licenseContent;
    CustomerLicense license;

    LOG.info("Uploading license {}, {} for customer.", licenseType, customerUUID);

    // Check if a license was uploaded as part of the request
    MultipartFormData<TemporaryFile> multiPartBody = request.body().asMultipartFormData();
    if (multiPartBody != null) {
      FilePart<TemporaryFile> filePart = multiPartBody.getFile("licenseFile");
      if (filePart == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "License file must contain valid file content.");
      }
      TemporaryFile uploadedFile = filePart.getRef();
      String fileName = filePart.getFilename();
      if (uploadedFile == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "liceneType and licenseFile params required.");
      }
      license =
          cLicenseManager.uploadLicenseFile(
              customerUUID, fileName, uploadedFile.path(), licenseType);
    } else if (licenseContent != null && !licenseContent.isEmpty()) {
      String fileName = licenseType + ".txt";
      // Create temp file and fill with content
      Path tempFile = fileHelperService.createTempFile(UUID.randomUUID().toString(), ".txt");
      Files.write(tempFile, licenseContent.getBytes());

      // Upload temp file to upload the license and return success/failure
      license =
          cLicenseManager.uploadLicenseFile(
              customerUUID, fileName, tempFile.toAbsolutePath(), licenseType);
    } else {
      throw new PlatformServiceException(
          BAD_REQUEST, "Either License Content/ License Key file is required");
    }

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CustomerLicense,
            Objects.toString(license.getLicenseUUID(), null),
            Audit.ActionType.Upload,
            Json.toJson(formData));
    return PlatformResults.withData(license);
  }

  @ApiOperation(value = "Delete a license", response = YBPSuccess.class, nickname = "deleteLicense")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result delete(UUID customerUUID, UUID licenseUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    cLicenseManager.delete(customerUUID, licenseUUID);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.CustomerLicense,
            licenseUUID.toString(),
            Audit.ActionType.Delete);
    LOG.info("Successfully deleted the license:" + licenseUUID);
    return YBPSuccess.empty();
  }
}
