// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.CertificateHelper;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;
import play.test.WithApplication;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import static org.junit.Assert.assertEquals;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

public class CertificateControllerTest extends WithApplication {

  private Customer customer;
  private List<String> test_certs = Arrays.asList("test_cert1", "test_cert2", "test_cert3");
  private List<UUID> test_certs_uuids = new ArrayList<>();

  @Override
  protected Application provideApplication() {

    return new GuiceApplicationBuilder()
      .configure((Map) Helpers.inMemoryDatabase())
      .build();
  }

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    for (String cert: test_certs) {
      test_certs_uuids.add(CertificateHelper.createRootCA(cert, customer.uuid, "/tmp/certs"));
    }
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File("/tmp/certs"));
  }

  private Result listCertificates(UUID customerUUID) {
    String uri = "/api/customers/" + customerUUID + "/certificates";
    return FakeApiHelper.doRequestWithAuthToken("GET", uri, customer.createAuthToken());
  }

  private Result getCertificate(UUID customerUUID, String label) {
    String uri = "/api/customers/" + customerUUID + "/certificates/" + label;
    return FakeApiHelper.doRequestWithAuthToken("GET", uri, customer.createAuthToken());
  }

  @Test
  public void testListCertificates() {
    Result result = listCertificates(customer.uuid);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    List<String> certs = Json.fromJson(json, List.class);
    assertEquals(test_certs, certs);
  }

  @Test
  public void testGetCertificate() {
    UUID cert_uuid = test_certs_uuids.get(0);
    Result result = getCertificate(customer.uuid, test_certs.get(0));
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(cert_uuid, UUID.fromString(json.asText()));
  }

}
