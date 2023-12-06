// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.junit.Assert.assertNotNull;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class CustomerLicenseTest extends FakeDBApplication {
  Customer customer;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
  }

  @Test
  public void testValidUpload() {
    String licenseType = "test_license_type";
    String license = "/opt/yugaware/licenses/license1.txt";
    CustomerLicense cLicense = CustomerLicense.create(customer.getUuid(), license, licenseType);

    assertNotNull(cLicense);
    JsonNode licenseInfoJson = Json.toJson(cLicense);
    assertValue(licenseInfoJson, "licenseType", "test_license_type");
    assertValue(licenseInfoJson, "license", "/opt/yugaware/licenses/license1.txt");
  }

  @Test
  public void testValidUploadAndRetrieval() {
    String licenseType = "test_license_type";
    String license = "/opt/yugaware/licenses/license1.txt";
    CustomerLicense cLicense = CustomerLicense.create(customer.getUuid(), license, licenseType);

    assertNotNull(cLicense);
    JsonNode licenseInfoJson = Json.toJson(cLicense);
    assertValue(licenseInfoJson, "licenseType", "test_license_type");
    assertValue(licenseInfoJson, "license", "/opt/yugaware/licenses/license1.txt");

    CustomerLicense cl = CustomerLicense.get(cLicense.getLicenseUUID());
    assertNotNull(cl);
    JsonNode cLInfoJson = Json.toJson(cl);
    assertValue(cLInfoJson, "licenseType", licenseType);
    assertValue(cLInfoJson, "license", license);
  }
}
