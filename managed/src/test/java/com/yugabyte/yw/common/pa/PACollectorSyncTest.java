// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.pa;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.notNullValue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.pa.PerfAdvisorClient.ExportMetricsType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.PerfAdvisorEndpoint;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.paendpoint.PaEndpointAuth;
import com.yugabyte.yw.models.helpers.paendpoint.PaEndpointAuthType;
import com.yugabyte.yw.models.helpers.paendpoint.PerfAdvisorEndpointType;
import java.util.Date;
import java.util.UUID;
import okhttp3.HttpUrl;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import okhttp3.mockwebserver.RecordedRequest;
import org.junit.Before;
import org.junit.Test;

/**
 * Covers what {@link PACollectorSync} does with export configs, which is the part that decides how
 * much traffic the loop makes: a full pass on the first tick and then nothing until something
 * changes.
 */
public class PACollectorSyncTest extends FakeDBApplication {

  private Customer customer;
  private PACollectorSync sync;
  private PerfAdvisorService perfAdvisorService;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    sync = app.injector().instanceOf(PACollectorSync.class);
    perfAdvisorService = app.injector().instanceOf(PerfAdvisorService.class);
    app.injector()
        .instanceOf(SettableRuntimeConfigFactory.class)
        .forCustomer(customer)
        .setValue(CustomerConfKeys.enablePaOnlineMode.getKey(), "true");
  }

  @Test
  public void testFirstTickPushesEveryEndpointInUseAndThenGoesQuiet() throws Exception {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      PACollector collector = registerCollector(server);
      int baseline = server.getRequestCount();
      PerfAdvisorEndpoint endpoint = createEndpoint("byoc-prod");
      registerUniverse(collector, endpoint);

      // The push, then the reconciliation listing.
      server.enqueue(new MockResponse().setBody("{}"));
      server.enqueue(new MockResponse().setBody("[]"));
      sync.initialize(customer);

      RecordedRequest push = server.takeRequest();
      assertThat(push.getMethod(), equalTo("PUT"));
      assertThat(push.getPath(), containsString("/api/export/config/" + endpoint.getUuid()));
      // The endpoint's own uuid is the collector's export config id, so the two sides never
      // need reconciling by name.
      assertThat(push.getBody().readUtf8(), containsString(endpoint.getUuid().toString()));

      RecordedRequest reconcile = server.takeRequest();
      assertThat(reconcile.getMethod(), equalTo("GET"));
      assertThat(reconcile.getPath(), containsString("/api/export/config"));

      // Second tick with nothing changed: no export config traffic at all. Every push makes the
      // collector dial the destination, so a quiet tick has to stay quiet.
      sync.initialize(customer);
      assertThat(server.getRequestCount() - baseline, equalTo(2));
    }
  }

  @Test
  public void testAnEditedEndpointIsPushedAgain() throws Exception {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      PACollector collector = registerCollector(server);
      PerfAdvisorEndpoint endpoint = createEndpoint("byoc-prod");
      registerUniverse(collector, endpoint);

      server.enqueue(new MockResponse().setBody("{}"));
      server.enqueue(new MockResponse().setBody("[]"));
      sync.initialize(customer);
      server.takeRequest();
      server.takeRequest();

      // The collector returns passwords masked, so a field comparison cannot see this change -
      // updateTime is what makes it detectable.
      endpoint.setUpdateTime(new Date(endpoint.getUpdateTime().getTime() + 1000));
      endpoint.update();

      server.enqueue(new MockResponse().setBody("{}"));
      sync.initialize(customer);

      RecordedRequest rePush = server.takeRequest();
      assertThat(rePush.getMethod(), equalTo("PUT"));
      assertThat(rePush.getPath(), containsString("/api/export/config/" + endpoint.getUuid()));
    }
  }

  @Test
  public void testAFailedPushIsRetriedOnTheNextTick() throws Exception {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      PACollector collector = registerCollector(server);
      PerfAdvisorEndpoint endpoint = createEndpoint("byoc-prod");
      registerUniverse(collector, endpoint);

      // Push rejected, so it must not be recorded as synced.
      server.enqueue(new MockResponse().setResponseCode(502).setBody("{}"));
      server.enqueue(new MockResponse().setBody("[]"));
      sync.initialize(customer);
      server.takeRequest();
      server.takeRequest();

      server.enqueue(new MockResponse().setBody("{}"));
      sync.initialize(customer);

      RecordedRequest retry = server.takeRequest();
      assertThat(retry.getMethod(), equalTo("PUT"));
      assertThat(retry.getPath(), containsString("/api/export/config/" + endpoint.getUuid()));
    }
  }

  @Test
  public void testReconciliationLeavesAnUnknownExportConfigAlone() throws Exception {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      PACollector collector = registerCollector(server);
      int baseline = server.getRequestCount();
      PerfAdvisorEndpoint endpoint = createEndpoint("byoc-prod");
      registerUniverse(collector, endpoint);

      // The collector also holds a config a Perf Advisor operator added by hand. Sync must not
      // treat that as garbage: only ids matching a YBA endpoint are ever removed.
      UUID foreign = UUID.randomUUID();
      server.enqueue(new MockResponse().setBody("{}"));
      server.enqueue(
          new MockResponse().setBody("[{\"id\":\"" + foreign + "\",\"name\":\"hand-made\"}]"));
      sync.initialize(customer);

      server.takeRequest();
      server.takeRequest();
      // Nothing else: no DELETE for the foreign config, and the wanted one is in use.
      assertThat(server.getRequestCount() - baseline, equalTo(2));
    }
  }

  @Test
  public void testNothingIsPushedWhileOnlineModeIsOff() throws Exception {
    app.injector()
        .instanceOf(SettableRuntimeConfigFactory.class)
        .forCustomer(customer)
        .setValue(CustomerConfKeys.enablePaOnlineMode.getKey(), "false");

    try (MockWebServer server = new MockWebServer()) {
      server.start();
      PACollector collector = registerCollector(server);
      int baseline = server.getRequestCount();
      PerfAdvisorEndpoint endpoint = createEndpoint("byoc-prod");
      registerUniverse(collector, endpoint);

      sync.initialize(customer);

      // The loop should not be dialing destinations for a feature that is switched off.
      assertThat(server.getRequestCount() - baseline, equalTo(0));
    }
  }

  @Test
  public void testAnEndpointNoUniverseUsesIsNeverPushed() throws Exception {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      registerCollector(server);
      int baseline = server.getRequestCount();
      PerfAdvisorEndpoint endpoint = createEndpoint("unused");
      assertThat(endpoint.getUuid(), notNullValue());

      server.enqueue(new MockResponse().setBody("[]"));
      sync.initialize(customer);

      // Only the reconciliation listing: an endpoint reaches a collector when a universe on it
      // needs it, not because it exists.
      RecordedRequest reconcile = server.takeRequest();
      assertThat(reconcile.getMethod(), equalTo("GET"));
      assertThat(server.getRequestCount() - baseline, equalTo(1));
    }
  }

  private PACollector registerCollector(MockWebServer server) {
    HttpUrl baseUrl = server.url("/api/customer/" + customer.getUuid() + "/metadata");
    PACollector collector =
        PerfAdvisorServiceTest.createTestPlatform(
            customer.getUuid(), baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
    server.enqueue(
        new MockResponse().setBody(PerfAdvisorServiceTest.convertToCustomerMetadata(collector)));
    PACollector saved = perfAdvisorService.save(collector, false);
    try {
      // Drain the customer_metadata PUT the save above made, so the assertions below see only
      // what sync did.
      server.takeRequest();
    } catch (InterruptedException e) {
      Thread.currentThread().interrupt();
    }
    return saved;
  }

  /**
   * Stored directly rather than through the service, whose create path probes the destination -
   * that round trip belongs to the endpoint API's tests, not to these.
   */
  private PerfAdvisorEndpoint createEndpoint(String name) {
    PaEndpointAuth auth = new PaEndpointAuth();
    auth.setType(PaEndpointAuthType.BASIC);
    auth.setUsername("writer");
    auth.setPassword("s3cret");

    PerfAdvisorEndpoint endpoint = new PerfAdvisorEndpoint();
    endpoint.generateUUID();
    endpoint.setCustomerUUID(customer.getUuid());
    endpoint.setName(name);
    endpoint.setType(PerfAdvisorEndpointType.BYOC);
    endpoint.setCollectionEndpoint("https://byoc.example.com");
    endpoint.setMetricsEndpoint("https://byoc.example.com/api/v1/otlp/metrics");
    endpoint.setMetricsType(ExportMetricsType.otlphttp);
    endpoint.setCollectionAuth(auth);
    endpoint.setMetricsAuth(auth);
    Date now = new Date();
    endpoint.setCreateTime(now);
    endpoint.setUpdateTime(now);
    endpoint.save();
    return endpoint;
  }

  private Universe registerUniverse(PACollector collector, PerfAdvisorEndpoint endpoint) {
    Universe universe = ModelFactory.createUniverse(customer.getId());
    Universe updated =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              u.getUniverseDetails().setPaCollectorUuid(collector.getUuid());
              u.getUniverseDetails().setPaEndpointUuid(endpoint.getUuid());
            });
    assertThat(updated.getUniverseDetails().getPaEndpointUuid(), equalTo(endpoint.getUuid()));
    assertThat(updated.getUniverseDetails().getPaCollectorUuid(), notNullValue());
    return updated;
  }
}
