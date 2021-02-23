// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.CertificateHelper;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.forms.CertificateParams;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import org.apache.commons.io.FileUtils;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;
import play.test.WithApplication;

import java.io.File;
import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Date;
import java.util.UUID;
import java.util.LinkedHashMap;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.test.Helpers.contentAsString;
import static org.mockito.Mockito.when;
import static com.yugabyte.yw.common.AssertHelper.*;
import static com.yugabyte.yw.common.TestHelper.createTempFile;

@RunWith(MockitoJUnitRunner.class)
public class CertificateControllerTest extends FakeDBApplication {
  @Mock
  play.Configuration mockAppConfig;

  private Customer customer;
  private Users user;
  private List<String> test_certs = Arrays.asList("test_cert1", "test_cert2", "test_cert3");
  private List<UUID> test_certs_uuids = new ArrayList<>();

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
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
    return FakeApiHelper.doRequestWithAuthToken("GET", uri, user.createAuthToken());
  }

  private Result uploadCertificate(UUID customerUUID, ObjectNode bodyJson) {
    String uri = "/api/customers/" + customerUUID + "/certificates";
    return FakeApiHelper.doRequestWithAuthTokenAndBody("POST", uri,
        user.createAuthToken(), bodyJson);
  }

  private Result updateCertificate(UUID customerUUID, UUID rootUUID, ObjectNode bodyJson) {
    String uri = "/api/customers/" + customerUUID + "/certificates/" + rootUUID +
                 "/update_empty_cert";
    return FakeApiHelper.doRequestWithAuthTokenAndBody("POST", uri, user.createAuthToken(),
                                                       bodyJson);
  }

  private Result getCertificate(UUID customerUUID, String label) {
    String uri = "/api/customers/" + customerUUID + "/certificates/" + label;
    return FakeApiHelper.doRequestWithAuthToken("GET", uri, user.createAuthToken());
  }
  
  private Result deleteCertificate(UUID customerUUID, UUID certUUID) {
    String uri = "/api/customers/" + customerUUID + "/certificates/" + certUUID.toString();
    return FakeApiHelper.doRequestWithAuthToken("DELETE", uri, user.createAuthToken());
  }

  private Result createClientCertificate(UUID customerUUID, UUID rootUUID,
                                         ObjectNode bodyJson) {
    String uri = "/api/customers/" + customerUUID + "/certificates/" + rootUUID;
    return FakeApiHelper.doRequestWithAuthTokenAndBody("POST", uri, user.createAuthToken(),
                                                       bodyJson);
  }

  private Result getRootCertificate(UUID customerUUID, UUID rootUUID) {
    String uri = "/api/customers/" + customerUUID + "/certificates/" + rootUUID + "/download";
    return FakeApiHelper.doRequestWithAuthToken("GET", uri, user.createAuthToken());
  }

  @Test
  public void testListCertificates() {
    ModelFactory.createUniverse(customer.getCustomerId(), test_certs_uuids.get(0));
    Result result = listCertificates(customer.uuid);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    List<LinkedHashMap> certs = Json.fromJson(json, List.class);
    List<UUID> result_uuids = new ArrayList<>();
    List<String> result_labels = new ArrayList<>();
    for (LinkedHashMap e : certs) {
      if (e.get("uuid").toString().equals(test_certs_uuids.get(0).toString())) {
          assertEquals(e.get("inUse"), true);
          assertNotEquals(e.get("universeDetails"), new ArrayList<>());
        }
        else {
          assertEquals(e.get("inUse"), false);
          assertEquals(e.get("universeDetails"), new ArrayList<>());
      }
      result_uuids.add(UUID.fromString(e.get("uuid").toString()));
      result_labels.add(e.get("label").toString());
      assertEquals(e.get("certType"), "SelfSigned");
    }
    assertEquals(test_certs, result_labels);
    assertEquals(test_certs_uuids, result_uuids);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testDeleteCertificate() {
    UUID cert_uuid = test_certs_uuids.get(0);
    Result result = deleteCertificate(customer.uuid, cert_uuid);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
  }
  
  @Test
  public void testDeleteInvalidCertificate() {
    UUID uuid=UUID.randomUUID();
    Result result = deleteCertificate(customer.uuid, uuid);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(BAD_REQUEST, result.status());
  }
  
  @Test
  public void testGetCertificate() {
    UUID cert_uuid = test_certs_uuids.get(0);
    Result result = getCertificate(customer.uuid, test_certs.get(0));
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(cert_uuid, UUID.fromString(json.asText()));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUploadCertificate() {
    String cert_content = "-----BEGIN CERTIFICATE-----\n" +
      "MIIDDjCCAfagAwIBAgIGAXVXb5y/MA0GCSqGSIb3DQEBCwUAMDQxHDAaBgNVBAMM\n" +
      "E3liLWFkbWluLXRlc3QtYXJuYXYxFDASBgNVBAoMC2V4YW1wbGUuY29tMB4XDTIw\n" +
      "MTAyMzIxNDg1M1oXDTIxMTAyMzIxNDg1M1owNDEcMBoGA1UEAwwTeWItYWRtaW4t\n" +
      "dGVzdC1hcm5hdjEUMBIGA1UECgwLZXhhbXBsZS5jb20wggEiMA0GCSqGSIb3DQEB\n" +
      "AQUAA4IBDwAwggEKAoIBAQCVWSZiQhr9e+L2MkSXP38dwXwF7RlZGhrYKrL7pp6l\n" +
      "aHkLZ0lsFgxI6h0Yyn5S+Hhi/jGWbBso6kXw7frUwVY5kX2Q6iv+E+rKqbYQgNV3\n" +
      "0vpCtOmNNolhNN3x4SKAIXyKOB5dXMGesjvba/qD6AstKS8bvRCUZcYDPjIUQGPu\n" +
      "cYLmywV/EdXgB+7WLhUOOY2eBRWBrnGxk60pcHJZeW44g1vas9cfiw81OWVp5/V5\n" +
      "apA631bE0MTgg283OCyYz+CV/YtnytUTg/+PUEqzM2cWsWdvpEz7TkKYXinRdN4d\n" +
      "SwgOQEIvb7A/GYYmVf3yk4moUxEh4NLoV9CBDljEBUjZAgMBAAGjJjAkMBIGA1Ud\n" +
      "EwEB/wQIMAYBAf8CAQEwDgYDVR0PAQH/BAQDAgLkMA0GCSqGSIb3DQEBCwUAA4IB\n" +
      "AQAFR0m7r1I3FyoatuLBIG+alaeGHqsgNqseAJTDGlEyajGDz4MT0c76ZIQkTSGM\n" +
      "vsM49Ad2D04sJR44/WlI2AVijubzHBr6Sj8ZdB909nPvGtB+Z8OnvKxJ0LUKyG1K\n" +
      "VUbcCnN3qSoVeY5PaPeFMmWF0Qv4S8lRTZqAvCnk34bckwkWoHkuuNGO49CsNb40\n" +
      "Z2NBy9Ckp0KkfeDpGcv9lHuUrl13iakCY09irvYRbfi0lVGF3+wXZtefV8ZAxfnN\n" +
      "Vt4faawkJ79oahlXDYs6WCKEd3zVM3mR3STnzwxvtB6WacjhqgP4ozXdt6PUbTfZ\n" +
      "jZPSP3OuL0IXk96wFHScay8S\n" +
      "-----END CERTIFICATE-----\n";
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("label", "test");
    bodyJson.put("certContent", cert_content);
    bodyJson.put("keyContent", "key_test");
    Date date = new Date();
    bodyJson.put("certStart", date.getTime());
    bodyJson.put("certExpiry", date.getTime());
    bodyJson.put("certType", "SelfSigned");
    Result result = uploadCertificate(customer.uuid, bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(BAD_REQUEST, result.status());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUploadCertificateNoKeyFail() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("label", "test");
    bodyJson.put("certContent", "cert_test");
    Date date = new Date();
    bodyJson.put("certStart", date.getTime());
    bodyJson.put("certExpiry", date.getTime());
    bodyJson.put("certType", "SelfSigned");
    Result result = uploadCertificate(customer.uuid, bodyJson);
    assertEquals(BAD_REQUEST, result.status());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUploadCustomCertificate() {
    String cert_content = "-----BEGIN CERTIFICATE-----\n" +
      "MIIDDjCCAfagAwIBAgIGAXVXb5y/MA0GCSqGSIb3DQEBCwUAMDQxHDAaBgNVBAMM\n" +
      "E3liLWFkbWluLXRlc3QtYXJuYXYxFDASBgNVBAoMC2V4YW1wbGUuY29tMB4XDTIw\n" +
      "MTAyMzIxNDg1M1oXDTIxMTAyMzIxNDg1M1owNDEcMBoGA1UEAwwTeWItYWRtaW4t\n" +
      "dGVzdC1hcm5hdjEUMBIGA1UECgwLZXhhbXBsZS5jb20wggEiMA0GCSqGSIb3DQEB\n" +
      "AQUAA4IBDwAwggEKAoIBAQCVWSZiQhr9e+L2MkSXP38dwXwF7RlZGhrYKrL7pp6l\n" +
      "aHkLZ0lsFgxI6h0Yyn5S+Hhi/jGWbBso6kXw7frUwVY5kX2Q6iv+E+rKqbYQgNV3\n" +
      "0vpCtOmNNolhNN3x4SKAIXyKOB5dXMGesjvba/qD6AstKS8bvRCUZcYDPjIUQGPu\n" +
      "cYLmywV/EdXgB+7WLhUOOY2eBRWBrnGxk60pcHJZeW44g1vas9cfiw81OWVp5/V5\n" +
      "apA631bE0MTgg283OCyYz+CV/YtnytUTg/+PUEqzM2cWsWdvpEz7TkKYXinRdN4d\n" +
      "SwgOQEIvb7A/GYYmVf3yk4moUxEh4NLoV9CBDljEBUjZAgMBAAGjJjAkMBIGA1Ud\n" +
      "EwEB/wQIMAYBAf8CAQEwDgYDVR0PAQH/BAQDAgLkMA0GCSqGSIb3DQEBCwUAA4IB\n" +
      "AQAFR0m7r1I3FyoatuLBIG+alaeGHqsgNqseAJTDGlEyajGDz4MT0c76ZIQkTSGM\n" +
      "vsM49Ad2D04sJR44/WlI2AVijubzHBr6Sj8ZdB909nPvGtB+Z8OnvKxJ0LUKyG1K\n" +
      "VUbcCnN3qSoVeY5PaPeFMmWF0Qv4S8lRTZqAvCnk34bckwkWoHkuuNGO49CsNb40\n" +
      "Z2NBy9Ckp0KkfeDpGcv9lHuUrl13iakCY09irvYRbfi0lVGF3+wXZtefV8ZAxfnN\n" +
      "Vt4faawkJ79oahlXDYs6WCKEd3zVM3mR3STnzwxvtB6WacjhqgP4ozXdt6PUbTfZ\n" +
      "jZPSP3OuL0IXk96wFHScay8S\n" +
      "-----END CERTIFICATE-----\n";
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("label", "test");
    bodyJson.put("certContent", cert_content);
    Date date = new Date();
    bodyJson.put("certStart", date.getTime());
    bodyJson.put("certExpiry", date.getTime());
    bodyJson.put("certType", "CustomCertHostPath");
    ObjectNode certJson = Json.newObject();
    certJson.put("rootCertPath", "/tmp/rootCertPath");
    certJson.put("nodeCertPath", "/tmp/nodeCertPath");
    certJson.put("nodeKeyPath", "/tmp/nodeKeyPath");
    bodyJson.put("customCertInfo", certJson);

    Result result = uploadCertificate(customer.uuid, bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    UUID certUUID = UUID.fromString(json.asText());
    CertificateInfo ci = CertificateInfo.get(certUUID);
    assertEquals(ci.label, "test");
    assertTrue(ci.certificate.contains("/tmp"));
    assertEquals(ci.certType, CertificateInfo.Type.CustomCertHostPath);
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUpdateCustomCertificate() throws IOException, NoSuchAlgorithmException {
    UUID certUUID = UUID.randomUUID();
    Date date = new Date();
    CertificateParams.CustomCertInfo customCertInfo = null;
    new File(TestHelper.TMP_PATH).mkdirs();
    createTempFile("ca.crt", "test-cert");
    CertificateInfo.create(certUUID, customer.uuid, "test", date, date,
                           TestHelper.TMP_PATH + "/ca.crt", customCertInfo, null, null);
    customCertInfo = CertificateInfo.get(certUUID).getCustomCertInfo();
    assertNull(customCertInfo);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("label", "test");
    bodyJson.put("certContent", "cert_test");
    bodyJson.put("certStart", date.getTime());
    bodyJson.put("certExpiry", date.getTime());
    bodyJson.put("certType", "CustomCertHostPath");
    ObjectNode certJson = Json.newObject();
    certJson.put("rootCertPath", "/tmp/rootCertPath");
    certJson.put("nodeCertPath", "/tmp/nodeCertPath");
    certJson.put("nodeKeyPath", "/tmp/nodeKeyPath");
    bodyJson.put("customCertInfo", certJson);
    Result result = updateCertificate(customer.uuid, certUUID, bodyJson);
    assertEquals(OK, result.status());
    customCertInfo = CertificateInfo.get(certUUID).getCustomCertInfo();
    assertNotNull(customCertInfo);
  }

  @Test
  public void testUpdateCustomCertificateFailure() throws IOException, NoSuchAlgorithmException {
    UUID certUUID = UUID.randomUUID();
    Date date = new Date();
    CertificateParams.CustomCertInfo customCertInfo = new CertificateParams.CustomCertInfo();
    customCertInfo.rootCertPath = "rootCertPath";
    customCertInfo.nodeCertPath = "nodeCertPath";
    customCertInfo.nodeKeyPath = "nodeKeyPath";
    new File(TestHelper.TMP_PATH).mkdirs();
    createTempFile("ca.crt", "test-cert");
    CertificateInfo.create(certUUID, customer.uuid, "test", date, date,
                           TestHelper.TMP_PATH + "/ca.crt", customCertInfo,
                           null, null);
    customCertInfo = CertificateInfo.get(certUUID).getCustomCertInfo();
    assertNotNull(customCertInfo);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("label", "test");
    bodyJson.put("certContent", "cert_test");
    bodyJson.put("certStart", date.getTime());
    bodyJson.put("certExpiry", date.getTime());
    bodyJson.put("certType", "CustomCertHostPath");
    ObjectNode certJson = Json.newObject();
    certJson.put("rootCertPath", "/tmp/rootCertPath");
    certJson.put("nodeCertPath", "/tmp/nodeCertPath");
    certJson.put("nodeKeyPath", "/tmp/nodeKeyPath");
    bodyJson.put("customCertInfo", certJson);
    Result result = updateCertificate(customer.uuid, certUUID, bodyJson);
    assertEquals(BAD_REQUEST, result.status());
  }

  @Test
  public void testCreateClientCertificate() {
    ObjectNode bodyJson = Json.newObject();
    Date date = new Date();
    bodyJson.put("username", "test");
    bodyJson.put("certStart", date.getTime());
    bodyJson.put("certExpiry", date.getTime());
    UUID rootCA = CertificateHelper.createRootCA("test-universe", customer.uuid,
                                                 "/tmp");
    Result result = createClientCertificate(customer.uuid, rootCA, bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    String clientCert = json.get("yugabytedb.crt").asText();
    String clientKey = json.get("yugabytedb.key").asText();
    assertNotNull(clientCert);
    assertNotNull(clientKey);
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testGetRootCertificate() {
    UUID rootCA = CertificateHelper.createRootCA("test-universe", customer.uuid,
                                                 "/tmp");
    Result result = getRootCertificate(customer.uuid, rootCA);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    String rootCert = json.get("root.crt").asText();
    assertNotNull(rootCert);
    assertAuditEntry(1, customer.uuid);
  }

}
