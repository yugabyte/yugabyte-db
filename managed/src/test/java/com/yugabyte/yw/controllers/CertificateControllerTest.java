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
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

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
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class CertificateControllerTest extends FakeDBApplication {
  @Mock play.Configuration mockAppConfig;

  private Customer customer;
  private Users user;
  private List<String> test_certs = Arrays.asList("test_cert1", "test_cert2", "test_cert3");
  private List<UUID> test_certs_uuids = new ArrayList<>();

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    for (String cert : test_certs) {
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
  public void testUploadCertificate() {
    String cert_content =
        "-----BEGIN CERTIFICATE-----\n"
            + "MIIDDjCCAfagAwIBAgIGAXVXb5y/MA0GCSqGSIb3DQEBCwUAMDQxHDAaBgNVBAMM\n"
            + "E3liLWFkbWluLXRlc3QtYXJuYXYxFDASBgNVBAoMC2V4YW1wbGUuY29tMB4XDTIw\n"
            + "MTAyMzIxNDg1M1oXDTIxMTAyMzIxNDg1M1owNDEcMBoGA1UEAwwTeWItYWRtaW4t\n"
            + "dGVzdC1hcm5hdjEUMBIGA1UECgwLZXhhbXBsZS5jb20wggEiMA0GCSqGSIb3DQEB\n"
            + "AQUAA4IBDwAwggEKAoIBAQCVWSZiQhr9e+L2MkSXP38dwXwF7RlZGhrYKrL7pp6l\n"
            + "aHkLZ0lsFgxI6h0Yyn5S+Hhi/jGWbBso6kXw7frUwVY5kX2Q6iv+E+rKqbYQgNV3\n"
            + "0vpCtOmNNolhNN3x4SKAIXyKOB5dXMGesjvba/qD6AstKS8bvRCUZcYDPjIUQGPu\n"
            + "cYLmywV/EdXgB+7WLhUOOY2eBRWBrnGxk60pcHJZeW44g1vas9cfiw81OWVp5/V5\n"
            + "apA631bE0MTgg283OCyYz+CV/YtnytUTg/+PUEqzM2cWsWdvpEz7TkKYXinRdN4d\n"
            + "SwgOQEIvb7A/GYYmVf3yk4moUxEh4NLoV9CBDljEBUjZAgMBAAGjJjAkMBIGA1Ud\n"
            + "EwEB/wQIMAYBAf8CAQEwDgYDVR0PAQH/BAQDAgLkMA0GCSqGSIb3DQEBCwUAA4IB\n"
            + "AQAFR0m7r1I3FyoatuLBIG+alaeGHqsgNqseAJTDGlEyajGDz4MT0c76ZIQkTSGM\n"
            + "vsM49Ad2D04sJR44/WlI2AVijubzHBr6Sj8ZdB909nPvGtB+Z8OnvKxJ0LUKyG1K\n"
            + "VUbcCnN3qSoVeY5PaPeFMmWF0Qv4S8lRTZqAvCnk34bckwkWoHkuuNGO49CsNb40\n"
            + "Z2NBy9Ckp0KkfeDpGcv9lHuUrl13iakCY09irvYRbfi0lVGF3+wXZtefV8ZAxfnN\n"
            + "Vt4faawkJ79oahlXDYs6WCKEd3zVM3mR3STnzwxvtB6WacjhqgP4ozXdt6PUbTfZ\n"
            + "jZPSP3OuL0IXk96wFHScay8S\n"
            + "-----END CERTIFICATE-----\n";
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
  public void testUploadCustomCertificate() {
    String cert_content =
        "-----BEGIN CERTIFICATE-----\n"
            + "MIIDDjCCAfagAwIBAgIGAXVXb5y/MA0GCSqGSIb3DQEBCwUAMDQxHDAaBgNVBAMM\n"
            + "E3liLWFkbWluLXRlc3QtYXJuYXYxFDASBgNVBAoMC2V4YW1wbGUuY29tMB4XDTIw\n"
            + "MTAyMzIxNDg1M1oXDTIxMTAyMzIxNDg1M1owNDEcMBoGA1UEAwwTeWItYWRtaW4t\n"
            + "dGVzdC1hcm5hdjEUMBIGA1UECgwLZXhhbXBsZS5jb20wggEiMA0GCSqGSIb3DQEB\n"
            + "AQUAA4IBDwAwggEKAoIBAQCVWSZiQhr9e+L2MkSXP38dwXwF7RlZGhrYKrL7pp6l\n"
            + "aHkLZ0lsFgxI6h0Yyn5S+Hhi/jGWbBso6kXw7frUwVY5kX2Q6iv+E+rKqbYQgNV3\n"
            + "0vpCtOmNNolhNN3x4SKAIXyKOB5dXMGesjvba/qD6AstKS8bvRCUZcYDPjIUQGPu\n"
            + "cYLmywV/EdXgB+7WLhUOOY2eBRWBrnGxk60pcHJZeW44g1vas9cfiw81OWVp5/V5\n"
            + "apA631bE0MTgg283OCyYz+CV/YtnytUTg/+PUEqzM2cWsWdvpEz7TkKYXinRdN4d\n"
            + "SwgOQEIvb7A/GYYmVf3yk4moUxEh4NLoV9CBDljEBUjZAgMBAAGjJjAkMBIGA1Ud\n"
            + "EwEB/wQIMAYBAf8CAQEwDgYDVR0PAQH/BAQDAgLkMA0GCSqGSIb3DQEBCwUAA4IB\n"
            + "AQAFR0m7r1I3FyoatuLBIG+alaeGHqsgNqseAJTDGlEyajGDz4MT0c76ZIQkTSGM\n"
            + "vsM49Ad2D04sJR44/WlI2AVijubzHBr6Sj8ZdB909nPvGtB+Z8OnvKxJ0LUKyG1K\n"
            + "VUbcCnN3qSoVeY5PaPeFMmWF0Qv4S8lRTZqAvCnk34bckwkWoHkuuNGO49CsNb40\n"
            + "Z2NBy9Ckp0KkfeDpGcv9lHuUrl13iakCY09irvYRbfi0lVGF3+wXZtefV8ZAxfnN\n"
            + "Vt4faawkJ79oahlXDYs6WCKEd3zVM3mR3STnzwxvtB6WacjhqgP4ozXdt6PUbTfZ\n"
            + "jZPSP3OuL0IXk96wFHScay8S\n"
            + "-----END CERTIFICATE-----\n";
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
  public void testUploadCustomServerCertificate() {
    String cert_content =
        "-----BEGIN CERTIFICATE-----\n"
            + "MIIDEDCCAfigAwIBAgIGAXoJELweMA0GCSqGSIb3DQEBCwUAMDUxHTAbBgNVBAMM\n"
            + "FHliLWFkbWluLXRlc3QtYXNpbmdoMRQwEgYDVQQKDAtleGFtcGxlLmNvbTAeFw0y\n"
            + "MTA2MTQwNTQ4NDlaFw0yMjA2MTQwNTQ4NDlaMDUxHTAbBgNVBAMMFHliLWFkbWlu\n"
            + "LXRlc3QtYXNpbmdoMRQwEgYDVQQKDAtleGFtcGxlLmNvbTCCASIwDQYJKoZIhvcN\n"
            + "AQEBBQADggEPADCCAQoCggEBAIFAb8DhUfou632m/c186Zs+X8okj8USS4nc3kJr\n"
            + "0V/sfY92Z0qoEBIPUaBb/MzIjFPWcT/UlTq2hkaCLNVytynFGiIAUtrGvwvW1n5p\n"
            + "mTHO6V53VunbSwAdRC1WoZqnMqpr4GeWHbdp8eyNoNOecqQ+z94gBVdXDtq3OsHa\n"
            + "7GuNz7Q/E7VtR0ETKYbYFQG6Os1+vSQSD8fuudWwCyRkR2CgXkcIZgE0xEnb3EBn\n"
            + "KUc6GD7Ye2CpHSVEpcBZPnT5oR/aODqFw+TAhmliezNrrrIO1gACeKVZmhglQusU\n"
            + "JCxyKOOJjB9JadQZRoJnf3p2a/UmkS7t+vzyKQX8cUgbAfUCAwEAAaMmMCQwEgYD\n"
            + "VR0TAQH/BAgwBgEB/wIBATAOBgNVHQ8BAf8EBAMCAuQwDQYJKoZIhvcNAQELBQAD\n"
            + "ggEBACQ0BxvWvtUh2f4gY4H2zy6jFh65X2dc1D4VXuEf7HGttVsApLX8Ny/7eMRr\n"
            + "5nEZPNdPEshxSrqtH0gPKCoCr3SRcUhS1VUIG4Yr9Hslui1D+Wk33EqPTVUCXWUb\n"
            + "Og14JKjAxgXSgxv4gIGO2sc4BglWX0CczxYK/CV0tcgrW7Pk5Gx4MPZF8JcttmUi\n"
            + "3NREOcmpslu2aEmV8FyTwwJdaZiGhEhBPObNrjsPs+JFLy2TUkHKcOKZcpTK3tdf\n"
            + "TnkdwZtNz6/4R7YJOATYZ9WoPUdUlemTgHaGlF8mNmiQynOZlgaBpqk4kJS54pmv\n"
            + "tHIdwRyTVMUFDOk7ZLKS5VB4/MM=\n"
            + "-----END CERTIFICATE-----";
    String server_cert_content =
        "-----BEGIN CERTIFICATE-----\n"
            + "MIIDADCCAeigAwIBAgIUBKN3X3k2+Z3mvx9vpCBWKGZ02DAwDQYJKoZIhvcNAQEL\n"
            + "BQAwNTEdMBsGA1UEAwwUeWItYWRtaW4tdGVzdC1hc2luZ2gxFDASBgNVBAoMC2V4\n"
            + "YW1wbGUuY29tMB4XDTIxMDYxNDA1NTIxNloXDTIyMDYxNDA1NTIxNlowLTEVMBMG\n"
            + "A1UEAwwMMTAuMTUwLjAuMTA3MRQwEgYDVQQKDAtleGFtcGxlLmNvbTCCASIwDQYJ\n"
            + "KoZIhvcNAQEBBQADggEPADCCAQoCggEBAMXq0mlwLRlOfHsngXobIC7E6zSdPUx3\n"
            + "9TMhhesH38e3kmAHkTjlew95yP8lM9+3D8uCZgnJiiMxtqhKLQZyNpUJ/uzn9E7M\n"
            + "VGUiiFIkbTquuY5SOawh7o3AymjU3Y+siMzMQYkxq8BNh/vd9ydfPuBjohQ4lve4\n"
            + "ZknNTp9DceefXD1Er5oYb2CiRB+FLt8xoI2fuNLwXbLWFn3BFcPwDONNbKNXz/jd\n"
            + "4VdErrQvd7t9OQYQXWqqJjt4L1JpMFC7DtZmLi3GkyQm/fPEbKVt6oL2IQcIfzku\n"
            + "M63Ik1ZfQ3PEeXrx4xHqtk6FlJL+YNJHmoLHjPWjveyY8atXKfY5if0CAwEAAaMQ\n"
            + "MA4wDAYDVR0TAQH/BAIwADANBgkqhkiG9w0BAQsFAAOCAQEAf1NSKX0QSnemh/1a\n"
            + "D1tXZJqsxsNHnJ1ub6H815yJkdM4X17B41qEVgsz2RUGyWeTOb4sfY+i/If2KFeS\n"
            + "3yoNhQ0/3hwy0ubUOkQeu/u4gCqcfynOthFU+AbTD4FsvStyRJfdKbsAEE2xRtan\n"
            + "6SwfS5NSyEersr0NH4jA3kD+D7m0nEml2rMpuyMisYXLDjoGpTMO3UACTctb1AMi\n"
            + "XgyOf5cNLUpgeKa6gHXG/zV+gaqzClbRzwiKwUc/euuIQIvoPqKSAmwgxEb4+Z1O\n"
            + "hTjo/tX+W4326YDWgO3g0ooLG1NIokTzfQVM7uuf/rw8C2G5zGORGXeGWambgJbD\n"
            + "pBJxoQ==\n"
            + "-----END CERTIFICATE-----";
    String server_key_content =
        "-----BEGIN RSA PRIVATE KEY-----\n"
            + "MIIEogIBAAKCAQEAxerSaXAtGU58eyeBehsgLsTrNJ09THf1MyGF6wffx7eSYAeR\n"
            + "OOV7D3nI/yUz37cPy4JmCcmKIzG2qEotBnI2lQn+7Of0TsxUZSKIUiRtOq65jlI5\n"
            + "rCHujcDKaNTdj6yIzMxBiTGrwE2H+933J18+4GOiFDiW97hmSc1On0Nx559cPUSv\n"
            + "mhhvYKJEH4Uu3zGgjZ+40vBdstYWfcEVw/AM401so1fP+N3hV0SutC93u305BhBd\n"
            + "aqomO3gvUmkwULsO1mYuLcaTJCb988RspW3qgvYhBwh/OS4zrciTVl9Dc8R5evHj\n"
            + "Eeq2ToWUkv5g0keagseM9aO97Jjxq1cp9jmJ/QIDAQABAoIBAB2ETeknr7IsgGgl\n"
            + "livNy9jtyV5JbRDwewMrJrvMqtUwTYZA2qmvn9DJCu7yb3AX7yUcx3cCNbXV/jXP\n"
            + "CjQB6J4FpZ1TYp413whORCJsCFZOJKJTJQLE9LzzWbyUso5w3t4cQFHjtIeziGpJ\n"
            + "ykh27futIEj/v5QmTisHkYgzGNPALwfnh3V/x4EO5A6Hn/OU01N7g6JRRJ+RCPuU\n"
            + "YHmm6g5zmWHZWe2mMBHIeRd6hrbJeHZYNGLLCo7kNbMr7+zK1wBytM+xR6yiVOMG\n"
            + "1A3gPpQSP53smEdV9EW1RBgdibGzHeJHrxggYBIkpujuuwkC7T0s+3WZVBhxPWdW\n"
            + "A/3MRYECgYEA+zRVbCww569lZQ+hmzw5D3jvtTCCU9nmF223ZUoWTQzQIR51S0BA\n"
            + "h19ukIMwMp1P0QlL6CjvY94hzgAJC1UWJNcgKlV0ie/huhYlTfldTj/6YY6ik2nP\n"
            + "1jzLpsZHS4fshj78IkkNG9nouhfIqjCHyNHV1KD5ntxhowrgEDfU6fECgYEAybIR\n"
            + "Rvbof8Kr1/P6CWqV53QpTlabU2zGEyl19yQdOjkjJY4jN+Zyj6mPiLBnnRahFubn\n"
            + "5xz4ltXaRGwNnHOVTx3fklGWNDauGQhSZslJii5SjmHQrJWirxB042lxp5WZNIU0\n"
            + "NgZTaNWb1KALi5Q8OWiYppwYcyU9SG4xIiFJdM0CgYAp3zNN8J/GPqo8Cjr50TQB\n"
            + "rDrojMlsiKmdxiAHti25ciVPH/CVNoSLDBE17WgfR7GCOnZ4oDom/2PLHp5jUS97\n"
            + "vJAT/mKKi32oswBM2v/+hxOJJ2laAQ0vvLqFdg90O5flWKJWZK7WsZ/lRQmhtK0t\n"
            + "gCyQYLS7EikEME/g5C2NQQKBgFl4tFFWliyWnsRdZj1nGrhhvzERGjYXuoYljj7j\n"
            + "tlNtpTmzo8vYXll8Tj/EgTIeJ7eRFq5fG6dNllVj2WXdoA5IojS2HHttBi30kxkl\n"
            + "kYnKorSmj3r/pfsiwbdfvxsoMZ4quM5+X+HRYB8iH/z69PxCefTuqanqixTmTMVn\n"
            + "Hr7BAoGAQbOxZCwv+PWU+T9W/Q846WizI+j52PCu5aXdba6dpa7RcR54G6VY8cxq\n"
            + "XAt1Lnt4lzRaEc4FHl5X4PfOKMmjIxhm2dr98w+ZIuOzbBi/wuYL4pSpT5Yqh7pB\n"
            + "jz+NbPTBYb/tdbJI+u/08aJTTfjWb79RP4t25A8RiQ7ZbsEUaN0=\n"
            + "-----END RSA PRIVATE KEY-----";
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
    assertEquals(ci.certType, CertificateInfo.Type.CustomServerCert);
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUpdateCustomCertificate() throws IOException, NoSuchAlgorithmException {
    UUID certUUID = UUID.randomUUID();
    Date date = new Date();
    new File(TestHelper.TMP_PATH).mkdirs();
    createTempFile("ca.crt", "test-cert");
    CertificateParams.CustomCertInfo emptyCustomCertInfo = null;
    CertificateInfo.create(
        certUUID,
        customer.uuid,
        "test",
        date,
        date,
        TestHelper.TMP_PATH + "/ca.crt",
        emptyCustomCertInfo);
    CertificateParams.CustomCertInfo customCertInfo =
        CertificateInfo.get(certUUID).getCustomCertInfo();
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
    CertificateInfo.create(
        certUUID,
        customer.uuid,
        "test",
        date,
        date,
        TestHelper.TMP_PATH + "/ca.crt",
        customCertInfo);
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
    Result result =
        assertPlatformException(() -> updateCertificate(customer.uuid, certUUID, bodyJson));
    assertEquals(BAD_REQUEST, result.status());
  }

  @Test
  public void testCreateClientCertificate() {
    String cert_content =
        "-----BEGIN CERTIFICATE-----\n"
            + "MIIDDjCCAfagAwIBAgIGAXVXb5y/MA0GCSqGSIb3DQEBCwUAMDQxHDAaBgNVBAMM\n"
            + "E3liLWFkbWluLXRlc3QtYXJuYXYxFDASBgNVBAoMC2V4YW1wbGUuY29tMB4XDTIw\n"
            + "MTAyMzIxNDg1M1oXDTIxMTAyMzIxNDg1M1owNDEcMBoGA1UEAwwTeWItYWRtaW4t\n"
            + "dGVzdC1hcm5hdjEUMBIGA1UECgwLZXhhbXBsZS5jb20wggEiMA0GCSqGSIb3DQEB\n"
            + "AQUAA4IBDwAwggEKAoIBAQCVWSZiQhr9e+L2MkSXP38dwXwF7RlZGhrYKrL7pp6l\n"
            + "aHkLZ0lsFgxI6h0Yyn5S+Hhi/jGWbBso6kXw7frUwVY5kX2Q6iv+E+rKqbYQgNV3\n"
            + "0vpCtOmNNolhNN3x4SKAIXyKOB5dXMGesjvba/qD6AstKS8bvRCUZcYDPjIUQGPu\n"
            + "cYLmywV/EdXgB+7WLhUOOY2eBRWBrnGxk60pcHJZeW44g1vas9cfiw81OWVp5/V5\n"
            + "apA631bE0MTgg283OCyYz+CV/YtnytUTg/+PUEqzM2cWsWdvpEz7TkKYXinRdN4d\n"
            + "SwgOQEIvb7A/GYYmVf3yk4moUxEh4NLoV9CBDljEBUjZAgMBAAGjJjAkMBIGA1Ud\n"
            + "EwEB/wQIMAYBAf8CAQEwDgYDVR0PAQH/BAQDAgLkMA0GCSqGSIb3DQEBCwUAA4IB\n"
            + "AQAFR0m7r1I3FyoatuLBIG+alaeGHqsgNqseAJTDGlEyajGDz4MT0c76ZIQkTSGM\n"
            + "vsM49Ad2D04sJR44/WlI2AVijubzHBr6Sj8ZdB909nPvGtB+Z8OnvKxJ0LUKyG1K\n"
            + "VUbcCnN3qSoVeY5PaPeFMmWF0Qv4S8lRTZqAvCnk34bckwkWoHkuuNGO49CsNb40\n"
            + "Z2NBy9Ckp0KkfeDpGcv9lHuUrl13iakCY09irvYRbfi0lVGF3+wXZtefV8ZAxfnN\n"
            + "Vt4faawkJ79oahlXDYs6WCKEd3zVM3mR3STnzwxvtB6WacjhqgP4ozXdt6PUbTfZ\n"
            + "jZPSP3OuL0IXk96wFHScay8S\n"
            + "-----END CERTIFICATE-----\n";
    ObjectNode bodyJson = Json.newObject();
    Date date = new Date();
    bodyJson.put("username", "test");
    bodyJson.put("certStart", date.getTime());
    bodyJson.put("certExpiry", date.getTime());
    bodyJson.put("certContent", cert_content);
    UUID rootCA = CertificateHelper.createRootCA("test-universe", customer.uuid, "/tmp");
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
    UUID rootCA = CertificateHelper.createRootCA("test-universe", customer.uuid, "/tmp");
    Result result = getRootCertificate(customer.uuid, rootCA);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    String rootCert = json.get("root.crt").asText();
    assertNotNull(rootCert);
    assertAuditEntry(1, customer.uuid);
  }
}
