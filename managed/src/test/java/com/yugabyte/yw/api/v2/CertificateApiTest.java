// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registerOk;
import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registrationSpec;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertConflict;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.UNAUTHORIZED;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.security.KeyPair;
import java.security.cert.X509Certificate;
import java.time.Instant;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

public class CertificateApiTest extends FakeDBApplication {

  private UUID customerUuid;
  private String apiToken;
  private String certPem;
  private String keyPem;
  private X509Certificate caCert;

  @Before
  public void setUp() throws Exception {
    JsonNode registration = registerOk(app, registrationSpec("admin@example.com"), null);
    customerUuid = UUID.fromString(registration.get("customer_uuid").asText());
    apiToken = registration.get("api_token").asText();

    // A real CA and its key, so that the default validation (yb.tls.enable_config_validation)
    // passes.
    CertificateHelper certificateHelper = app.injector().instanceOf(CertificateHelper.class);
    KeyPair keyPair = CertificateHelper.getKeyPairObject();
    caCert = certificateHelper.generateCACertificate("v2-test-ca", keyPair, 4);
    certPem = CertificateHelper.getAsPemString(caCert);
    keyPem = CertificateHelper.getAsPemString(keyPair.getPrivate());
  }

  private static ObjectNode spec(String label, String cert, String key) {
    ObjectNode spec = Json.newObject().put("label", label).put("cert_type", "SelfSigned");
    if (cert != null) {
      spec.put("cert_content", cert);
    }
    if (key != null) {
      spec.put("key_content", key);
    }
    return spec;
  }

  private Result create(UUID cUUID, String token, JsonNode body) {
    Http.RequestBuilder request =
        Helpers.fakeRequest("POST", "/api/v2/customers/" + cUUID + "/certificates").bodyJson(body);
    if (token != null) {
      request.header(TokenAuthenticator.API_TOKEN_HEADER, token);
    }
    return Helpers.route(app, request);
  }

  private JsonNode createOk(JsonNode body) {
    Result result = create(customerUuid, apiToken, body);
    assertEquals(contentAsString(result), OK, result.status());
    return Json.parse(contentAsString(result));
  }

  @Test
  public void createsSelfSignedRootCA() throws Exception {
    JsonNode cert = createOk(spec("n2n-root", certPem, keyPem));

    UUID certUuid = UUID.fromString(cert.get("uuid").asText());
    assertEquals("n2n-root", cert.get("label").asText());
    assertEquals("SelfSigned", cert.get("cert_type").asText());
    assertEquals(
        caCert.getNotBefore().toInstant(), Instant.parse(cert.get("start_date").asText()));
    assertEquals(
        caCert.getNotAfter().toInstant(), Instant.parse(cert.get("expiry_date").asText()));
    assertFalse("the key isn't returned", cert.has("key_content"));

    CertificateInfo info = CertificateInfo.get(certUuid);
    assertEquals(customerUuid, info.getCustomerUUID());
    assertEquals(CertConfigType.SelfSigned, info.getCertType());
    assertTrue(Files.exists(Paths.get(info.getCertificate())));
    assertTrue(Files.exists(Paths.get(info.getPrivateKey())));

    List<Audit> audits =
        Audit.getAll(customerUuid).stream()
            .filter(a -> a.getTarget() == Audit.TargetType.Certificate)
            .toList();
    assertEquals(1, audits.size());
    assertEquals(Audit.ActionType.Create, audits.get(0).getAction());
    assertEquals(certUuid.toString(), audits.get(0).getTargetID());
    String payload = audits.get(0).getPayload().toString();
    assertFalse("the audit entry has no private key", payload.contains("PRIVATE KEY"));
    assertEquals("REDACTED", audits.get(0).getPayload().get("key_content").asText());
  }

  @Test
  public void contentWithoutTrailingNewlineIsAccepted() {
    JsonNode cert = createOk(spec("no-newline", certPem.stripTrailing(), keyPem));

    assertEquals("no-newline", cert.get("label").asText());
  }

  @Test
  public void duplicateLabelIsConflict() {
    createOk(spec("n2n-root", certPem, keyPem));

    Result result =
        assertPlatformException(() -> create(customerUuid, apiToken, spec("n2n-root", certPem, keyPem)));

    assertConflict(result, "Certificate with label n2n-root already exists");
    assertEquals(1, CertificateInfo.getAll(customerUuid).size());
  }

  @Test
  public void keyOfAnotherCertificateIsBadRequest() throws Exception {
    String otherKey = CertificateHelper.getAsPemString(CertificateHelper.getKeyPairObject().getPrivate());

    Result result =
        assertPlatformException(() -> create(customerUuid, apiToken, spec("mismatch", certPem, otherKey)));

    assertEquals(400, result.status());
    assertTrue(CertificateInfo.getAll(customerUuid).isEmpty());
  }

  @Test
  public void contentThatIsNotPemIsBadRequest() {
    Result result =
        assertPlatformException(
            () -> create(customerUuid, apiToken, spec("garbage", "not a certificate\n", keyPem)));

    assertBadRequest(result, "Unable to get cert Objects");
  }

  @Test
  public void missingKeyIsBadRequest() {
    Result result =
        assertPlatformException(() -> create(customerUuid, apiToken, spec("no-key", certPem, null)));

    assertBadRequest(result, "CertificateSpec.keyContent must not be null");
  }

  @Test
  public void noTokenIsUnauthorized() {
    Result result = create(customerUuid, null, spec("n2n-root", certPem, keyPem));

    assertEquals(contentAsString(result), UNAUTHORIZED, result.status());
  }

  @Test
  public void readOnlyUserIsUnauthorized() {
    Users readOnly =
        ModelFactory.testUser(
            Customer.getOrBadRequest(customerUuid), "ro@example.com", Users.Role.ReadOnly);

    Result result = create(customerUuid, readOnly.upsertApiToken(), spec("n2n-root", certPem, keyPem));

    // The authorization layer answers a missing permission with 401.
    assertEquals(contentAsString(result), UNAUTHORIZED, result.status());
    assertNotNull(CertificateInfo.getAll(customerUuid));
    assertTrue(CertificateInfo.getAll(customerUuid).isEmpty());
  }
}
