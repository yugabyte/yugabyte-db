// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers;

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
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.Users;
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
