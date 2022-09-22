// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.CustomerLicenseManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.CustomerLicenseFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.CustomerLicense;
import com.yugabyte.yw.models.Audit;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Objects;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Http.MultipartFormData;
import play.mvc.Http.MultipartFormData.FilePart;
import play.mvc.Result;

@Api(
    value = "License management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CustomerLicenseController extends AuthenticatedController {

  @Inject CustomerLicenseManager cLicenseManager;

  public static final Logger LOG = LoggerFactory.getLogger(CustomerLicenseController.class);

  @ApiOperation(
      nickname = "upload_license",
      value = "Uploads the license",
      response = CustomerLicense.class)
  public Result upload(UUID customerUUID) throws IOException {
    CustomerLicenseFormData formData =
        formFactory.getFormDataOrBadRequest(CustomerLicenseFormData.class).get();

    String licenseType = formData.licenseType;
    String licenseContent = formData.licenseContent;
    CustomerLicense license;

    LOG.info("Uploading license {}, {} for customer.", licenseType, customerUUID);

    // Check if a license was uploaded as part of the request
    MultipartFormData<File> multiPartBody = request().body().asMultipartFormData();
    if (multiPartBody != null) {
      FilePart<File> filePart = multiPartBody.getFile("licenseFile");
      if (filePart == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "License file must contain valid file content.");
      }
      File uploadedFile = filePart.getFile();
      String fileName = filePart.getFilename();
      if (uploadedFile == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "liceneType and licenseFile params required.");
      }
      license =
          cLicenseManager.uploadLicenseFile(customerUUID, fileName, uploadedFile, licenseType);
    } else if (licenseContent != null && !licenseContent.isEmpty()) {
      String fileName = licenseType + ".txt";
      // Create temp file and fill with content
      Path tempFile = Files.createTempFile(UUID.randomUUID().toString(), ".txt");
      Files.write(tempFile, licenseContent.getBytes());

      // Upload temp file to upload the license and return success/failure
      license =
          cLicenseManager.uploadLicenseFile(customerUUID, fileName, tempFile.toFile(), licenseType);
    } else {
      throw new PlatformServiceException(
          BAD_REQUEST, "Either License Content/ License Key file is required");
    }

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.CustomerLicense,
            Objects.toString(license.licenseUUID, null),
            Audit.ActionType.Upload,
            request().body().asJson());
    return PlatformResults.withData(license);
  }

  @ApiOperation(value = "Delete a license", response = YBPSuccess.class, nickname = "deleteLicense")
  public Result delete(UUID customerUUID, UUID licenseUUID) {
    cLicenseManager.delete(customerUUID, licenseUUID);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.CustomerLicense,
            licenseUUID.toString(),
            Audit.ActionType.Delete);
    LOG.info("Successfully deleted the license:" + licenseUUID);
    return YBPSuccess.empty();
  }
}
