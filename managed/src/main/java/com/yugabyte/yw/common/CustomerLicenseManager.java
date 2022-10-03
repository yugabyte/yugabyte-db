// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.forms.CustomerLicenseFormData;
import com.yugabyte.yw.models.CustomerLicense;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.UUID;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
@Slf4j
public class CustomerLicenseManager {

  public static final Logger LOG = LoggerFactory.getLogger(CustomerLicenseManager.class);

  @Inject play.Configuration appConfig;

  private String getOrCreateLicenseFilePath(UUID customerUUID) {
    String customerLicensePath = "/licenses/" + customerUUID.toString();
    File keyBasePathName = new File(appConfig.getString("yb.storage.path"), customerLicensePath);
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
      UUID customerUUID, String fileName, File uploadedFile, String licenseType)
      throws IOException {
    String licenseParentPath = getOrCreateLicenseFilePath(customerUUID);
    Path source = Paths.get(uploadedFile.getAbsolutePath());
    Path destination = Paths.get(licenseParentPath, fileName);
    if (!Files.exists(source)) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "License file " + source.getFileName() + " not found.");
    }
    if (Files.exists(destination)) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "File " + destination.getFileName() + " already exists.");
    }

    Files.move(source, destination);
    return CustomerLicense.create(customerUUID, destination.toString(), licenseType);
  }

  public void delete(UUID customerUUID, UUID licenseUUID) {
    CustomerLicense license = CustomerLicense.getOrBadRequest(licenseUUID);
    if (FileUtils.deleteQuietly(new File(license.license))) {
      log.info("Successfully deleted file with path: " + license.license);
      if (license.delete()) {
        log.info("Successfully deleted the license: " + licenseUUID);
      } else {
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Unable to delete the license");
      }
    } else {
      log.info("Failed to delete file with path: " + license.license);
    }
  }
}
