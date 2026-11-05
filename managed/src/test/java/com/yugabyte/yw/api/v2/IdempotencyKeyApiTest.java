// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registerOk;
import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registrationSpec;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.UNPROCESSABLE_ENTITY;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.idempotency.IdempotencyService;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.models.ApiIdempotencyKey;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import io.ebean.DB;
import java.security.KeyPair;
import java.security.cert.X509Certificate;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

/** Idempotency-Key handling, exercised through the v2 operations that opt into it. */
public class IdempotencyKeyApiTest extends FakeDBApplication {

  private static final String KEY = "6f1c4d0e-1d3b-4c55-9a4e-0f7f8f6f9b11";

  private UUID customerUuid;
  private String apiToken;
  private String certPem;
  private String keyPem;

  @Before
  public void setUp() throws Exception {
    JsonNode registration = registerOk(app, registrationSpec("admin@example.com"), null);
    customerUuid = UUID.fromString(registration.get("customer_uuid").asText());
    apiToken = registration.get("api_token").asText();

    CertificateHelper certificateHelper = app.injector().instanceOf(CertificateHelper.class);
    KeyPair keyPair = CertificateHelper.getKeyPairObject();
    X509Certificate caCert = certificateHelper.generateCACertificate("idem-ca", keyPair, 4);
    certPem = CertificateHelper.getAsPemString(caCert);
    keyPem = CertificateHelper.getAsPemString(keyPair.getPrivate());
  }

  private ObjectNode certSpec(String label) {
    return Json.newObject()
        .put("label", label)
        .put("cert_type", "SelfSigned")
        .put("cert_content", certPem)
        .put("key_content", keyPem);
  }

  private Result createCert(
      UUID cUUID, String tokenHeader, String token, JsonNode body, String key) {
    Http.RequestBuilder request =
        Helpers.fakeRequest("POST", "/api/v2/customers/" + cUUID + "/certificates").bodyJson(body);
    request.header(tokenHeader, token);
    if (key != null) {
      request.header(IdempotencyService.IDEMPOTENCY_KEY_HEADER, key);
    }
    return Helpers.route(app, request);
  }

  private Result createCert(JsonNode body, String key) {
    return createCert(customerUuid, TokenAuthenticator.API_TOKEN_HEADER, apiToken, body, key);
  }

  private List<Audit> certificateAudits() {
    return Audit.getAll(customerUuid).stream()
        .filter(a -> a.getTarget() == Audit.TargetType.Certificate)
        .toList();
  }

  @Test
  public void retryWithTheSameKeyReplaysTheResponse() {
    Result first = createCert(certSpec("root"), KEY);
    assertEquals(contentAsString(first), OK, first.status());
    assertFalse(first.headers().containsKey(IdempotencyService.REPLAYED_HEADER));

    Result retry = createCert(certSpec("root"), KEY);

    assertEquals(contentAsString(retry), OK, retry.status());
    assertEquals("true", retry.headers().get(IdempotencyService.REPLAYED_HEADER));
    assertEquals(Json.parse(contentAsString(first)), Json.parse(contentAsString(retry)));
    assertEquals("application/json", retry.contentType().orElse(null));
    assertEquals("the operation ran once", 1, CertificateInfo.getAll(customerUuid).size());
    assertEquals("the replay isn't audited again", 1, certificateAudits().size());
  }

  @Test
  public void sameJsonWithReorderedFieldsIsTheSameRequest() {
    createCert(certSpec("root"), KEY);
    ObjectNode reordered =
        Json.newObject()
            .put("key_content", keyPem)
            .put("cert_content", certPem)
            .put("cert_type", "SelfSigned")
            .put("label", "root");

    Result retry = createCert(reordered, KEY);

    assertEquals(contentAsString(retry), OK, retry.status());
    assertEquals("true", retry.headers().get(IdempotencyService.REPLAYED_HEADER));
  }

  @Test
  public void withoutAKeyRetriesRunTheOperationAgain() {
    createCert(certSpec("root"), null);

    Result retry = assertPlatformException(() -> createCert(certSpec("root"), null));

    assertEquals(CONFLICT, retry.status());
    assertEquals(0, ApiIdempotencyKey.find.query().findCount());
  }

  @Test
  public void keyReusedForADifferentRequestIsRejected() {
    createCert(certSpec("root"), KEY);

    Result reused = assertPlatformException(() -> createCert(certSpec("other"), KEY));

    assertEquals(UNPROCESSABLE_ENTITY, reused.status());
    assertTrue(contentAsString(reused), contentAsString(reused).contains("different request"));
    assertEquals(1, CertificateInfo.getAll(customerUuid).size());
  }

  @Test
  public void keyReusedOnAnotherOperationIsRejected() {
    createCert(certSpec("root"), KEY);
    ObjectNode userSpec = Json.newObject();
    userSpec.set("spec", Json.newObject().put("email", "u@example.com").put("role", "Admin"));
    userSpec.put("password", "Passw0rd#123");

    Http.RequestBuilder request =
        Helpers.fakeRequest("POST", "/api/v2/customers/" + customerUuid + "/users")
            .bodyJson(userSpec)
            .header(TokenAuthenticator.API_TOKEN_HEADER, apiToken)
            .header(IdempotencyService.IDEMPOTENCY_KEY_HEADER, KEY);
    Result reused = assertPlatformException(() -> Helpers.route(app, request));

    assertEquals(UNPROCESSABLE_ENTITY, reused.status());
    assertTrue(contentAsString(reused), contentAsString(reused).contains("/certificates"));
  }

  @Test
  public void failedRequestReleasesTheKey() {
    ObjectNode invalid = certSpec("root").put("cert_content", "not a certificate\n");
    Result failed = assertPlatformException(() -> createCert(invalid, KEY));
    assertEquals(BAD_REQUEST, failed.status());
    assertEquals(0, ApiIdempotencyKey.find.query().findCount());

    // The client fixes the request and retries with the same key.
    Result retry = createCert(certSpec("root"), KEY);

    assertEquals(contentAsString(retry), OK, retry.status());
    assertFalse(retry.headers().containsKey(IdempotencyService.REPLAYED_HEADER));
    assertEquals(1, CertificateInfo.getAll(customerUuid).size());
  }

  @Test
  public void retryWhileTheFirstRequestRunsIsAConflict() {
    claimInProgress(KEY, new Date());

    Result retry = assertPlatformException(() -> createCert(certSpec("root"), KEY));

    assertEquals(CONFLICT, retry.status());
    assertTrue(contentAsString(retry), contentAsString(retry).contains("still in progress"));
    assertTrue(CertificateInfo.getAll(customerUuid).isEmpty());
  }

  @Test
  public void abandonedKeyIsTakenOver() {
    // A request that started two hours ago and never finished, e.g. YBA restarted.
    claimInProgress(KEY, new Date(System.currentTimeMillis() - 2 * 3600 * 1000L));

    Result retry = createCert(certSpec("root"), KEY);

    assertEquals(contentAsString(retry), OK, retry.status());
    assertEquals(1, CertificateInfo.getAll(customerUuid).size());
    ApiIdempotencyKey key = ApiIdempotencyKey.find(customerUuid, KEY).orElseThrow();
    assertEquals(ApiIdempotencyKey.State.COMPLETED, key.getState());
  }

  @Test
  public void expiredKeyCanBeReused() {
    createCert(certSpec("root"), KEY);
    expireAll();

    // The same key for a different request is accepted once the first one expired.
    Result second = createCert(certSpec("other"), KEY);

    assertEquals(contentAsString(second), OK, second.status());
    assertFalse(second.headers().containsKey(IdempotencyService.REPLAYED_HEADER));
    assertEquals(2, CertificateInfo.getAll(customerUuid).size());
    assertEquals(1, ApiIdempotencyKey.find.query().findCount());
  }

  @Test
  public void cleanupDeletesOnlyExpiredKeys() {
    createCert(certSpec("root"), KEY);
    expireAll();
    createCert(certSpec("other"), "another-key");

    app.injector().instanceOf(IdempotencyService.class).deleteExpiredKeys();

    List<ApiIdempotencyKey> left = ApiIdempotencyKey.find.all();
    assertEquals(1, left.size());
    assertEquals("another-key", left.get(0).getIdempotencyKey());
  }

  @Test
  public void keysAreScopedPerCustomer() {
    createCert(certSpec("root"), KEY);
    Customer other = ModelFactory.testCustomer("tc2", "other@customer.com");
    Users otherUser = ModelFactory.testUser(other, "other@customer.com");

    Result result =
        createCert(
            other.getUuid(),
            TokenAuthenticator.AUTH_TOKEN_HEADER,
            otherUser.createAuthToken(),
            certSpec("root"),
            KEY);

    assertEquals(contentAsString(result), OK, result.status());
    assertFalse(result.headers().containsKey(IdempotencyService.REPLAYED_HEADER));
    assertEquals(1, CertificateInfo.getAll(other.getUuid()).size());
  }

  @Test
  public void invalidKeyIsRejected() {
    for (String key : List.of("has space", "x".repeat(256), "tab\tkey")) {
      Result result = assertPlatformException(() -> createCert(certSpec("root"), key));
      assertEquals(key, BAD_REQUEST, result.status());
    }
    assertTrue(CertificateInfo.getAll(customerUuid).isEmpty());
  }

  @Test
  public void generatedSecretsAreReplayedButStoredEncrypted() {
    ObjectNode body = Json.newObject();
    body.set("spec", Json.newObject().put("email", "u@example.com").put("role", "Admin"));
    body.put("password", "Passw0rd#123").put("generate_api_token", true);
    Http.RequestBuilder request =
        Helpers.fakeRequest("POST", "/api/v2/customers/" + customerUuid + "/users")
            .bodyJson(body)
            .header(TokenAuthenticator.API_TOKEN_HEADER, apiToken)
            .header(IdempotencyService.IDEMPOTENCY_KEY_HEADER, KEY);

    Result first = Helpers.route(app, request);
    Result retry = Helpers.route(app, request);

    assertEquals(contentAsString(first), OK, first.status());
    String token = Json.parse(contentAsString(first)).get("api_token").asText();
    assertFalse(token.isEmpty());
    assertEquals(token, Json.parse(contentAsString(retry)).get("api_token").asText());
    byte[] stored =
        (byte[])
            DB.sqlQuery("SELECT response_body FROM api_idempotency_key")
                .findOne()
                .get("response_body");
    assertFalse("stored encrypted", new String(stored).contains(token));
  }

  private void claimInProgress(String key, Date updateTime) {
    ApiIdempotencyKey record = new ApiIdempotencyKey();
    record.setUuid(UUID.randomUUID());
    record.setCustomerUUID(customerUuid);
    record.setIdempotencyKey(key);
    record.setRequestMethod("POST");
    record.setRequestPath("/api/v2/customers/" + customerUuid + "/certificates");
    record.setRequestFingerprint(
        IdempotencyService.fingerprint(
            Helpers.fakeRequest("POST", "/api/v2/customers/" + customerUuid + "/certificates")
                .bodyJson(certSpec("root"))
                .build()));
    record.setState(ApiIdempotencyKey.State.IN_PROGRESS);
    record.setCreateTime(updateTime);
    record.setUpdateTime(updateTime);
    record.setExpireTime(new Date(System.currentTimeMillis() + 3600 * 1000L));
    record.insert();
  }

  private static void expireAll() {
    DB.sqlUpdate("UPDATE api_idempotency_key SET expire_time = :t")
        .setParameter("t", new Date(System.currentTimeMillis() - 1000))
        .execute();
  }
}
