// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;
import static play.test.Helpers.contentAsString;

import akka.stream.javadsl.FileIO;
import akka.stream.javadsl.Source;
import akka.util.ByteString;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerLicense;
import com.yugabyte.yw.models.Users;
import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class CustomerLicenseControllerTest extends FakeDBApplication {
  Customer defaultCustomer;
  Users defaultUser;

  @Before
  public void before() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer);
  }

  private Result uploadLicenseFile(
      boolean uploadFile, String licenseType, File licenseFile, String licenseContent) {
    String uri = "/api/customers/" + defaultCustomer.getUuid() + "/licenses";

    if (uploadFile) {
      List<Http.MultipartFormData.Part<Source<ByteString, ?>>> bodyData = new ArrayList<>();
      bodyData.add(new Http.MultipartFormData.DataPart("licenseType", licenseType));
      if (licenseFile != null) {
        Source<ByteString, ?> uploadedFile = FileIO.fromFile(licenseFile);
        bodyData.add(
            new Http.MultipartFormData.FilePart<>(
                "licenseFile", "license.txt", "application/octet-stream", uploadedFile));
      }
      return doRequestWithAuthTokenAndMultipartData(
          "POST", uri, defaultUser.createAuthToken(), bodyData, mat);
    } else {
      ObjectNode bodyJson = Json.newObject();
      bodyJson.put("licenseType", licenseType);
      bodyJson.put("licenseContent", licenseContent);
      return doRequestWithAuthTokenAndBody("POST", uri, defaultUser.createAuthToken(), bodyJson);
    }
  }

  @Test
  public void testUploadLicenseWithLicenseFile() throws IOException {
    String tmpFile = createTempFile("TEMP LICENSE FILE");
    File licenseFile = new File(tmpFile);
    String licenseType = "test_license_type";
    CustomerLicense license =
        CustomerLicense.create(
            defaultCustomer.getUuid(), licenseFile.getAbsolutePath(), licenseType);
    when(mockCustomerLicenseManager.uploadLicenseFile(any(), any(), any(), any()))
        .thenReturn(license);
    Result result = uploadLicenseFile(true, licenseType, licenseFile, null);
    JsonNode node = Json.parse(contentAsString(result));
    assertValue(node, "licenseType", licenseType);
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testUploadLicenseWithoutLicenseFile() throws IOException {
    String licenseContent = "LICENSE FILE CONTENT";
    String licenseType = "test_license_type_2";
    String fileName = licenseType + ".txt";
    CustomerLicense license =
        CustomerLicense.create(defaultCustomer.getUuid(), fileName, licenseType);
    when(mockCustomerLicenseManager.uploadLicenseFile(any(), any(), any(), any()))
        .thenReturn(license);
    Result result = uploadLicenseFile(false, licenseType, null, licenseContent);
    JsonNode node = Json.parse(contentAsString(result));
    assertValue(node, "licenseType", licenseType);
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testUploadLicenseWithoutLicenseContent() throws IOException {
    String tmpFile = createTempFile("TEMP LICENSE FILE");
    File licenseFile = new File(tmpFile);
    String licenseType = "test_license_type";
    Result result = assertPlatformException(() -> uploadLicenseFile(true, licenseType, null, null));
    assertBadRequest(result, "License file must contain valid file content.");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }
}
