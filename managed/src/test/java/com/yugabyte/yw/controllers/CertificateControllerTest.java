// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.spy;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.forms.CertificateParams;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import java.io.File;
import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.UUID;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.runners.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class CertificateControllerTest extends FakeDBApplication {
  private Customer customer;
  private Users user;
  private final List<String> test_certs = Arrays.asList("test_cert1", "test_cert2", "test_cert3");
  private final List<UUID> test_certs_uuids = new ArrayList<>();
  private Config spyConf;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    spyConf = spy(app.config());
    doReturn("/tmp/" + getClass().getSimpleName() + "/certs")
        .when(spyConf)
        .getString("yb.storage.path");
    for (String cert : test_certs) {
      test_certs_uuids.add(CertificateHelper.createRootCA(spyConf, cert, customer.uuid));
    }
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File("/tmp/" + getClass().getSimpleName() + "/certs"));
  }

  private Result listCertificates(UUID customerUUID) {
    String uri = "/api/customers/" + customerUUID + "/certificates";
    return FakeApiHelper.doRequestWithAuthToken("GET", uri, user.createAuthToken());
  }

  private Result uploadCertificate(UUID customerUUID, ObjectNode bodyJson) {
    String uri = "/api/customers/" + customerUUID + "/certificates";
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        "POST", uri, user.createAuthToken(), bodyJson);
  }

  private Result updateCertificate(UUID customerUUID, UUID rootUUID, ObjectNode bodyJson) {
    String uri =
        "/api/customers/" + customerUUID + "/certificates/" + rootUUID + "/update_empty_cert";
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        "POST", uri, user.createAuthToken(), bodyJson);
  }

  private Result getCertificate(UUID customerUUID, String label) {
    String uri = "/api/customers/" + customerUUID + "/certificates/" + label;
    return FakeApiHelper.doRequestWithAuthToken("GET", uri, user.createAuthToken());
  }

  private Result deleteCertificate(UUID customerUUID, UUID certUUID) {
    String uri = "/api/customers/" + customerUUID + "/certificates/" + certUUID.toString();
    return FakeApiHelper.doRequestWithAuthToken("DELETE", uri, user.createAuthToken());
  }

  private Result createClientCertificate(UUID customerUUID, UUID rootUUID, ObjectNode bodyJson) {
    String uri = "/api/customers/" + customerUUID + "/certificates/" + rootUUID;
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        "POST", uri, user.createAuthToken(), bodyJson);
  }

  private Result getRootCertificate(UUID customerUUID, UUID rootUUID) {
    String uri = "/api/customers/" + customerUUID + "/certificates/" + rootUUID + "/download";
    return FakeApiHelper.doRequestWithAuthToken("GET", uri, user.createAuthToken());
  }

  private Result createSelfSignedCertificate(UUID customerUUID, ObjectNode bodyJson) {
    String uri = "/api/customers/" + customerUUID + "/certificates/create_self_signed_cert";
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        "POST", uri, user.createAuthToken(), bodyJson);
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
      } else {
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
    UUID uuid = UUID.randomUUID();
    Result result = assertPlatformException(() -> deleteCertificate(customer.uuid, uuid));
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
  public void testUploadCertificate() throws IOException {
    String cert_content = TestUtils.readResource("platform.dev.crt");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("label", "test");
    bodyJson.put("certContent", cert_content);
    bodyJson.put("keyContent", "key_test");
    Date date = new Date();
    bodyJson.put("certStart", date.getTime());
    bodyJson.put("certExpiry", date.getTime());
    bodyJson.put("certType", "SelfSigned");
    Result result = assertPlatformException(() -> uploadCertificate(customer.uuid, bodyJson));
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
    Result result = assertPlatformException(() -> uploadCertificate(customer.uuid, bodyJson));
    assertEquals(BAD_REQUEST, result.status());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUploadCustomCertificate() throws IOException {
    String cert_content = TestUtils.readResource("platform.dev.crt");
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
    bodyJson.set("customCertInfo", certJson);

    Result result = uploadCertificate(customer.uuid, bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    UUID certUUID = UUID.fromString(json.asText());
    CertificateInfo ci = CertificateInfo.get(certUUID);
    assertEquals(ci.label, "test");
    assertTrue(ci.certificate.contains("/tmp"));
    assertEquals(ci.certType, CertConfigType.CustomCertHostPath);
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUploadCustomServerCertificate() throws IOException {
    String cert_content = TestUtils.readResource("platform.dev.crt");
    String server_cert_content = TestUtils.readResource("server.dev.crt");
    String server_key_content = TestUtils.readResource("server.dev.key");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("label", "test");
    bodyJson.put("certContent", cert_content);
    Date date = new Date();
    bodyJson.put("certStart", date.getTime());
    bodyJson.put("certExpiry", date.getTime());
    bodyJson.put("certType", "CustomServerCert");
    ObjectNode certJson = Json.newObject();
    certJson.put("serverCertContent", server_cert_content);
    certJson.put("serverKeyContent", server_key_content);
    bodyJson.put("customServerCertData", certJson);

    Result result = uploadCertificate(customer.uuid, bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    UUID certUUID = UUID.fromString(json.asText());
    CertificateInfo ci = CertificateInfo.get(certUUID);
    CertificateInfo.CustomServerCertInfo customServerCertInfo = ci.getCustomServerCertInfo();
    assertEquals(ci.label, "test");
    assertTrue(ci.certificate.contains("/tmp"));
    assertTrue(customServerCertInfo.serverCert.contains("/tmp"));
    assertTrue(customServerCertInfo.serverKey.contains("/tmp"));
    assertEquals(ci.certType, CertConfigType.CustomServerCert);
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUpdateCustomCertificate() throws IOException, NoSuchAlgorithmException {
    UUID certUUID = UUID.randomUUID();
    Date date = new Date();
    createTempFile("certificate_controller_test_ca.crt", "test-cert");
    CertificateParams.CustomCertInfo emptyCustomCertPathParams = null;
    CertificateInfo.create(
        certUUID,
        customer.uuid,
        "test",
        date,
        date,
        TestHelper.TMP_PATH + "/certificate_controller_test_ca.crt",
        emptyCustomCertPathParams);
    CertificateParams.CustomCertInfo customCertInfo =
        CertificateInfo.get(certUUID).getCustomCertPathParams();
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
    bodyJson.set("customCertInfo", certJson);

    Result result = updateCertificate(customer.uuid, certUUID, bodyJson);
    assertEquals(OK, result.status());
    customCertInfo = CertificateInfo.get(certUUID).getCustomCertPathParams();
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
    createTempFile("certificate_controller_test_ca.crt", "test-cert");
    CertificateInfo.create(
        certUUID,
        customer.uuid,
        "test",
        date,
        date,
        TestHelper.TMP_PATH + "/certificate_controller_test_ca.crt",
        customCertInfo);
    customCertInfo = CertificateInfo.get(certUUID).getCustomCertPathParams();
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

    Result result =
        assertPlatformException(() -> updateCertificate(customer.uuid, certUUID, bodyJson));
    assertEquals(BAD_REQUEST, result.status());
  }

  @Test
  public void testCreateClientCertificate() {
    String cert_content = TestUtils.readResource("platform.dev.crt");
    ObjectNode bodyJson = Json.newObject();
    Date date = new Date();
    bodyJson.put("username", "test");
    bodyJson.put("certStart", date.getTime());
    bodyJson.put("certExpiry", date.getTime());
    bodyJson.put("certContent", cert_content);
    UUID rootCA = CertificateHelper.createRootCA(spyConf, "test-universe", customer.uuid);
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
    UUID rootCA = CertificateHelper.createRootCA(spyConf, "test-universe", customer.uuid);
    Result result = getRootCertificate(customer.uuid, rootCA);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    String rootCert = json.get("root.crt").asText();
    assertNotNull(rootCert);
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateSelfSignedCertificate() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("label", "create_self_signed_cert_test");
    Result result = createSelfSignedCertificate(customer.uuid, bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    UUID certUUID = UUID.fromString(json.asText());
    CertificateInfo ci = CertificateInfo.get(certUUID);
    System.out.println(ci.label);
    assertEquals(ci.label, "create_self_signed_cert_test");
    assertEquals(ci.certType, CertConfigType.SelfSigned);
    assertAuditEntry(1, customer.uuid);
  }
}
