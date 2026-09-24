// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.hasSize;
import static org.hamcrest.Matchers.notNullValue;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.pa.PerfAdvisorService;
import com.yugabyte.yw.common.pa.PerfAdvisorServiceTest;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.PerfAdvisorEndpoint;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import java.io.IOException;
import java.util.UUID;
import okhttp3.HttpUrl;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

public class PerfAdvisorEndpointApiControllerImpTest extends FakeDBApplication {

  private static final String VALID_REPORT =
      "{\"checks\":[{\"field\":\"collectionEndpoint\",\"ok\":true},"
          + "{\"field\":\"metricsEndpoint\",\"ok\":true}]}";

  private Customer customer;
  private Users user;
  private String authToken;
  private PerfAdvisorService perfAdvisorService;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    authToken = user.createAuthToken();
    perfAdvisorService = app.injector().instanceOf(PerfAdvisorService.class);
    setOnlineMode(true);
  }

  @Test
  public void testCreateAssignsTheUuidAndMasksThePassword() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      registerCollector(server);
      server.enqueue(new MockResponse().setBody(VALID_REPORT));

      JsonNode created = created(post(spec("byoc-prod")));

      UUID uuid = UUID.fromString(created.get("info").get("uuid").asText());
      assertThat(PerfAdvisorEndpoint.createQuery().idEq(uuid).findOne(), notNullValue());
      // Reads never carry the credential back in the clear.
      assertThat(
          created.get("spec").get("collection_auth").get("password").asText(), equalTo("********"));
      assertThat(created.get("info").get("universe_uuids").size(), equalTo(0));
    }
  }

  @Test
  public void testCreateIsRejectedWhenTheDestinationContradictsIt() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      registerCollector(server);
      server.enqueue(
          new MockResponse()
              .setBody(
                  "{\"checks\":[{\"field\":\"collectionEndpoint\",\"ok\":false,"
                      + "\"message\":\"Cannot reach the Collection API\"},"
                      + "{\"field\":\"metricsEndpoint\",\"ok\":true}]}"));

      Result result = assertPlatformException(() -> post(spec("byoc-prod")));

      assertThat(result.status(), equalTo(BAD_REQUEST));
      assertThat(contentAsString(result), containsString("Cannot reach the Collection API"));
      // Nothing stored: a destination that cannot be reached must not become a registration that
      // can only fail.
      assertThat(PerfAdvisorEndpoint.createQuery().findList(), hasSize(0));
    }
  }

  @Test
  public void testEditKeepsAPasswordEchoedBackMasked() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      registerCollector(server);
      server.enqueue(new MockResponse().setBody(VALID_REPORT));
      JsonNode created = created(post(spec("byoc-prod")));
      UUID uuid = UUID.fromString(created.get("info").get("uuid").asText());

      // The UI edits what it read back, mask included.
      ObjectNode edited = (ObjectNode) created.get("spec");
      edited.put("name", "byoc-renamed");
      server.enqueue(new MockResponse().setBody(VALID_REPORT));
      Result result = doRequestWithAuthTokenAndBody("PUT", endpointUrl(uuid), authToken, edited);

      assertThat(result.status(), equalTo(OK));
      PerfAdvisorEndpoint stored = PerfAdvisorEndpoint.createQuery().idEq(uuid).findOne();
      assertThat(stored.getName(), equalTo("byoc-renamed"));
      assertThat(stored.getCollectionAuth().getPassword(), equalTo("s3cret"));
    }
  }

  @Test
  public void testCreateRejectsADuplicateName() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      registerCollector(server);
      server.enqueue(new MockResponse().setBody(VALID_REPORT));
      post(spec("byoc-prod"));

      server.enqueue(new MockResponse().setBody(VALID_REPORT));
      Result result = assertPlatformException(() -> post(spec("byoc-prod")));

      assertThat(result.status(), equalTo(BAD_REQUEST));
      assertThat(contentAsString(result), containsString("already exists"));
      assertThat(PerfAdvisorEndpoint.createQuery().findList(), hasSize(1));
    }
  }

  @Test
  public void testPaOnlineTypeIsRejectedUntilItExists() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      registerCollector(server);
      ObjectNode spec = spec("hosted");
      spec.put("type", "PA_ONLINE");

      Result result = assertPlatformException(() -> post(spec));

      assertThat(result.status(), equalTo(BAD_REQUEST));
      assertThat(contentAsString(result), containsString("not supported yet"));
    }
  }

  @Test
  public void testDeleteIsRefusedWhileAUniverseUsesIt() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      PACollector collector = registerCollector(server);
      server.enqueue(new MockResponse().setBody(VALID_REPORT));
      UUID uuid =
          UUID.fromString(created(post(spec("byoc-prod"))).get("info").get("uuid").asText());

      Universe universe = ModelFactory.createUniverse(customer.getId());
      Universe.saveDetails(
          universe.getUniverseUUID(),
          u -> {
            u.getUniverseDetails().setPaCollectorUuid(collector.getUuid());
            u.getUniverseDetails().setPaEndpointUuid(uuid);
          });

      Result result =
          assertPlatformException(
              () -> doRequestWithAuthToken("DELETE", endpointUrl(uuid), authToken));

      assertThat(result.status(), equalTo(BAD_REQUEST));
      assertThat(contentAsString(result), containsString("still used by universes"));
      assertThat(PerfAdvisorEndpoint.createQuery().idEq(uuid).findOne(), notNullValue());
    }
  }

  @Test
  public void testEveryOperationIsRefusedWhileOnlineModeIsOff() throws IOException {
    setOnlineMode(false);

    Result list =
        assertPlatformException(
            () ->
                doRequestWithAuthToken(
                    "GET",
                    "/api/v2/customers/" + customer.getUuid() + "/perf-advisor-endpoints",
                    authToken));

    assertThat(list.status(), equalTo(BAD_REQUEST));
    assertThat(contentAsString(list), containsString("online mode is not enabled"));
  }

  @Test
  public void testValidateReportsPerFieldWithoutStoringAnything() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      registerCollector(server);
      server.enqueue(
          new MockResponse()
              .setBody(
                  "{\"checks\":[{\"field\":\"metricsEndpoint\",\"ok\":false,"
                      + "\"message\":\"rejected these credentials\"}]}"));

      Result result =
          doRequestWithAuthTokenAndBody(
              "POST",
              "/api/v2/customers/" + customer.getUuid() + "/perf-advisor-endpoints/validate",
              authToken,
              spec("candidate"));

      assertThat(result.status(), equalTo(OK));
      JsonNode body = Json.parse(contentAsString(result));
      assertThat(body.get("valid").asBoolean(), equalTo(false));
      assertThat(
          body.get("checks").get(0).get("message").asText(),
          containsString("rejected these credentials"));
      assertThat(PerfAdvisorEndpoint.createQuery().findList(), hasSize(0));
    }
  }

  @Test
  public void testValidateWithNoCollectorIsNotAFailure() {
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/v2/customers/" + customer.getUuid() + "/perf-advisor-endpoints/validate",
            authToken,
            spec("candidate"));

    assertThat(result.status(), equalTo(OK));
    JsonNode body = Json.parse(contentAsString(result));
    // Nothing contradicted the configuration, so it is not reported as invalid.
    assertThat(body.get("valid").asBoolean(), equalTo(true));
    assertThat(body.get("checks").size(), equalTo(0));
  }

  @Test
  public void testEndpointsListIsEmptyBeforeAnythingIsConfigured() {
    Result result =
        doRequestWithAuthToken(
            "GET",
            "/api/v2/customers/" + customer.getUuid() + "/perf-advisor-endpoints",
            authToken);

    assertThat(result.status(), equalTo(OK));
    assertThat(Json.parse(contentAsString(result)).size(), equalTo(0));
  }

  private void setOnlineMode(boolean enabled) {
    app.injector()
        .instanceOf(SettableRuntimeConfigFactory.class)
        .forCustomer(customer)
        .setValue(CustomerConfKeys.enablePaOnlineMode.getKey(), Boolean.toString(enabled));
  }

  private ObjectNode spec(String name) {
    ObjectNode spec = Json.newObject();
    spec.put("name", name);
    spec.put("type", "BYOC");
    spec.put("metrics_endpoint", "https://byoc.cloud.yugabyte.com/api/v1/otlp/metrics");
    spec.put("metrics_type", "otlphttp");
    spec.put("collection_endpoint", "https://byoc.cloud.yugabyte.com");
    ObjectNode auth = Json.newObject();
    auth.put("type", "BASIC");
    auth.put("username", "writer");
    auth.put("password", "s3cret");
    spec.set("collection_auth", auth);
    spec.put("ybm_account_id", "account-1");
    spec.put("ybm_project_id", "project-1");
    return spec;
  }

  private Result post(ObjectNode spec) {
    return doRequestWithAuthTokenAndBody(
        "POST",
        "/api/v2/customers/" + customer.getUuid() + "/perf-advisor-endpoints",
        authToken,
        spec);
  }

  private JsonNode created(Result result) {
    assertThat(result.status(), equalTo(OK));
    return Json.parse(contentAsString(result));
  }

  private String endpointUrl(UUID uuid) {
    return "/api/v2/customers/" + customer.getUuid() + "/perf-advisor-endpoints/" + uuid;
  }

  private PACollector registerCollector(MockWebServer server) {
    HttpUrl baseUrl = server.url("/api/customer/" + customer.getUuid() + "/metadata");
    PACollector collector =
        PerfAdvisorServiceTest.createTestPlatform(
            customer.getUuid(), baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
    server.enqueue(
        new MockResponse().setBody(PerfAdvisorServiceTest.convertToCustomerMetadata(collector)));
    return perfAdvisorService.save(collector, false);
  }
}
