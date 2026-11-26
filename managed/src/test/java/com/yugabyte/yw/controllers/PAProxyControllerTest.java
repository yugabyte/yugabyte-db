// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.nullValue;
import static play.test.Helpers.contentAsString;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.pa.PerfAdvisorService;
import com.yugabyte.yw.common.pa.PerfAdvisorServiceTest;
import com.yugabyte.yw.common.rbac.Permission;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.ResourceGroup.ResourceDefinition;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import okhttp3.HttpUrl;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import okhttp3.mockwebserver.RecordedRequest;
import org.apache.pekko.stream.Materializer;
import org.junit.Before;
import org.junit.Test;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

/**
 * Covers which of the browser's headers reach Perf Advisor. The proxy exists so that embedded PA UI
 * calls are same-origin to YBA; anything that describes the browser's own context has to stop here.
 */
public class PAProxyControllerTest extends FakeDBApplication {

  private Customer customer;
  private String authToken;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    authToken = user.createAuthToken();
    app.injector()
        .instanceOf(SettableRuntimeConfigFactory.class)
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.paEmbeddedUiReverseProxyEnabled.getKey(), "true");
  }

  @Test
  public void testOriginIsNotForwardedToPerfAdvisor() throws Exception {
    try (MockWebServer paServer = new MockWebServer()) {
      paServer.start();
      PACollector collector = registerCollector(paServer);

      paServer.enqueue(new MockResponse().setBody("{}"));

      Http.RequestBuilder request =
          Helpers.fakeRequest(
                  "GET",
                  "/api/v1/customers/"
                      + customer.getUuid()
                      + "/pa_proxy/"
                      + collector.getUuid()
                      + "/api/anomaly")
              .header("X-AUTH-TOKEN", authToken)
              // What a browser sends on its own initiative. Forwarding Origin made Perf
              // Advisor's CORS filter reject the call with 403 before any handler ran.
              .header("Origin", "http://yba.example.com")
              .header("Cookie", "authToken=" + authToken)
              // A header the caller genuinely means for the upstream: must still get through.
              .header("Accept", "application/json");

      Result result = route(request);
      assertThat(result.status(), equalTo(200));
      // The proxy streams the upstream response, so the body is not a strict entity.
      assertThat(
          contentAsString(result, app.injector().instanceOf(Materializer.class)), equalTo("{}"));

      RecordedRequest forwarded = paServer.takeRequest();
      assertThat(forwarded.getHeader("Origin"), nullValue());
      assertThat(forwarded.getHeader("Cookie"), nullValue());
      assertThat(forwarded.getHeader("Accept"), equalTo("application/json"));
    }
  }

  @Test
  public void testUniverseCallIsForwarded() throws Exception {
    try (MockWebServer paServer = new MockWebServer()) {
      paServer.start();
      PACollector collector = registerCollector(paServer);
      Universe universe = ModelFactory.createUniverse(customer.getId());

      paServer.enqueue(new MockResponse().setBody("{}"));
      Result result =
          route(
              proxyRequest(
                  collector,
                  "universe/"
                      + universe.getUniverseUUID()
                      + "/queries?universe="
                      + universe.getUniverseUUID()));
      assertThat(result.status(), equalTo(200));
      assertThat(
          paServer.takeRequest().getPath(),
          equalTo(
              "/api/universe/"
                  + universe.getUniverseUUID()
                  + "/queries?universe="
                  + universe.getUniverseUUID()));
    }
  }

  @Test
  public void testUniverseCallNamingAnotherUniverseIsRejected() throws Exception {
    try (MockWebServer paServer = new MockWebServer()) {
      paServer.start();
      PACollector collector = registerCollector(paServer);
      Universe universe = ModelFactory.createUniverse(customer.getId());
      String prefix = "universe/" + universe.getUniverseUUID();
      String other = UUID.randomUUID().toString();
      String otherEncoded = String.format("%%%02X", (int) other.charAt(0)) + other.substring(1);

      for (String path :
          new String[] {
            prefix + "/queries?x=/universes/" + other,
            prefix + "/queries?universes=" + other,
            prefix + "/queries?universe=" + other,
            prefix + "/queries?x=/universes/" + otherEncoded,
            prefix + "/x/universe/" + other,
            prefix + "/x/universes/" + other + "/y/universes/" + universe.getUniverseUUID(),
            prefix + "/x/universes%5C" + other,
            prefix + "/x/Universes/" + other,
            prefix + "/x/universes;y/" + other
          }) {
        assertPlatformException(() -> route(proxyRequest(collector, path)));
      }
      assertThat(paServer.getRequestCount(), equalTo(1));
    }
  }

  @Test
  public void testCallOutsideItsRouteIsRejected() throws Exception {
    try (MockWebServer paServer = new MockWebServer()) {
      paServer.start();
      PACollector collector = registerCollector(paServer);
      Universe universe = ModelFactory.createUniverse(customer.getId());

      for (String path :
          new String[] {
            "x/%2e%2e/universe/" + universe.getUniverseUUID() + "/queries",
            "x/../universe/" + universe.getUniverseUUID() + "/queries",
            "universe/" + universe.getUniverseUUID() + "/%2e%2e/" + UUID.randomUUID(),
            "universe/" + universe.getUniverseUUID() + "/../" + UUID.randomUUID()
          }) {
        assertPlatformException(() -> route(proxyRequest(collector, path)));
      }
      assertThat(paServer.getRequestCount(), equalTo(1));
    }
  }

  @Test
  public void testUniverseCallAuthorizedForTheRouteUniverse() throws Exception {
    app.injector()
        .instanceOf(SettableRuntimeConfigFactory.class)
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.useNewRbacAuthz.getKey(), "true");
    try (MockWebServer paServer = new MockWebServer()) {
      paServer.start();
      PACollector collector = registerCollector(paServer);
      Universe readable = ModelFactory.createUniverse("readable", customer.getId());
      Universe other = ModelFactory.createUniverse("other", customer.getId());
      String readerToken = universeReaderToken(readable);

      paServer.enqueue(new MockResponse().setResponseCode(204));
      Result result =
          route(
              proxyRequest(
                  collector,
                  "POST",
                  "universe/" + readable.getUniverseUUID() + "/ui_events",
                  readerToken));
      assertThat(result.status(), equalTo(204));

      result =
          route(
              proxyRequest(
                  collector,
                  "POST",
                  "universe/" + other.getUniverseUUID() + "/ui_events",
                  readerToken));
      assertThat(result.status(), equalTo(401));
      assertThat(paServer.getRequestCount(), equalTo(2));
    }
  }

  // A user with universe READ on the given universe only.
  private String universeReaderToken(Universe universe) {
    Role universeReader =
        Role.create(
            customer.getUuid(),
            "UniverseReader",
            "Reads universes",
            RoleType.Custom,
            new HashSet<>(
                Set.of(
                    new Permission(ResourceType.UNIVERSE, Action.READ),
                    new Permission(ResourceType.OTHER, Action.READ))));
    Users reader = ModelFactory.testUser(customer, "reader@customer.com", Users.Role.ConnectOnly);
    ResourceDefinition universeOnly =
        ResourceDefinition.builder()
            .resourceType(ResourceType.UNIVERSE)
            .resourceUUIDSet(new HashSet<>(Set.of(universe.getUniverseUUID())))
            .build();
    RoleBinding.create(
        reader,
        RoleBindingType.Custom,
        universeReader,
        new ResourceGroup(new HashSet<>(Set.of(universeOnly))));
    return reader.createAuthToken();
  }

  private Http.RequestBuilder proxyRequest(PACollector collector, String path) {
    return proxyRequest(collector, "GET", path, authToken);
  }

  private Http.RequestBuilder proxyRequest(
      PACollector collector, String method, String path, String token) {
    return Helpers.fakeRequest(
            method,
            "/api/v1/customers/"
                + customer.getUuid()
                + "/pa_proxy/"
                + collector.getUuid()
                + "/api/"
                + path)
        .header("X-AUTH-TOKEN", token);
  }

  private PACollector registerCollector(MockWebServer server) throws Exception {
    HttpUrl baseUrl = server.url("/");
    PACollector collector =
        PerfAdvisorServiceTest.createTestPlatform(
            customer.getUuid(), baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
    server.enqueue(
        new MockResponse().setBody(PerfAdvisorServiceTest.convertToCustomerMetadata(collector)));
    PACollector saved = app.injector().instanceOf(PerfAdvisorService.class).save(collector, false);
    // Drain the customer_metadata PUT the save above made.
    server.takeRequest();
    return saved;
  }
}
