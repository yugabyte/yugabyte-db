// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.yugabyte.yw.models.CustomerLicense;
import com.yugabyte.yw.models.FileData;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.UUID;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
@Slf4j
public class CustomerLicenseManager {

  public static final Logger LOG = LoggerFactory.getLogger(CustomerLicenseManager.class);

  private String getOrCreateLicenseFilePath(UUID customerUUID) {
    String customerLicensePath = "/licenses/" + customerUUID.toString();
    File keyBasePathName = new File(AppConfigHelper.getStoragePath(), customerLicensePath);
    // Protect against multi-threaded access and validate that we only error out if mkdirs fails
    // correctly, by NOT creating the final dir path.
    synchronized (this) {
      if (!keyBasePathName.exists() && !keyBasePathName.mkdirs() && !keyBasePathName.exists()) {
        throw new RuntimeException(
            "License path " + keyBasePathName.getAbsolutePath() + " doesn't exist.");
      }
    }

    return keyBasePathName.getAbsolutePath();
  }

  public CustomerLicense uploadLicenseFile(
      UUID customerUUID, String fileName, Path uploadedFile, String licenseType)
      throws IOException {
    String licenseParentPath = getOrCreateLicenseFilePath(customerUUID);
    Path destination = Paths.get(licenseParentPath, fileName);
    if (!Files.exists(uploadedFile)) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "License file " + uploadedFile.getFileName() + " not found.");
    }
    if (Files.exists(destination)) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "File " + destination.getFileName() + " already exists.");
    }

    Files.move(uploadedFile, destination);
    return CustomerLicense.create(customerUUID, destination.toString(), licenseType);
  }

  public void delete(UUID customerUUID, UUID licenseUUID) {
    CustomerLicense license = CustomerLicense.getOrBadRequest(customerUUID, licenseUUID);
    FileData.deleteFiles(license.getLicense(), true);
    if (license.delete()) {
      log.info("Successfully deleted the license: " + licenseUUID);
    }
  }
}
