// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.hamcrest.Matchers.equalTo;
import static org.junit.Assert.assertThrows;
import static org.mockito.Mockito.mock;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.pa.PerfAdvisorClient.ExportMetricsType;
import com.yugabyte.yw.common.pa.PerfAdvisorEndpointService;
import com.yugabyte.yw.common.pa.PerfAdvisorService;
import com.yugabyte.yw.common.pa.PerfAdvisorServiceTest;
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
 * The destination has to exist on the collector before a universe can name it, which is the whole
 * reason this runs as its own subtask ahead of the registration.
 */
public class PushPaExportConfigTest extends FakeDBApplication {

  private Customer customer;
  private PerfAdvisorService perfAdvisorService;
  private PerfAdvisorEndpointService endpointService;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    perfAdvisorService = app.injector().instanceOf(PerfAdvisorService.class);
    endpointService = app.injector().instanceOf(PerfAdvisorEndpointService.class);
  }

  @Test
  public void testPushesTheEndpointAtItsOwnUuid() throws Exception {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      PACollector collector = registerCollector(server);
      PerfAdvisorEndpoint endpoint = createEndpoint();
      Universe universe = ModelFactory.createUniverse(customer.getId());

      server.enqueue(new MockResponse().setBody("{}"));
      subtask(universe, collector.getUuid(), endpoint.getUuid()).run();

      RecordedRequest push = server.takeRequest();
      // A PUT rather than a POST: the collector's create handler assigns its own id, so only the
      // upsert keeps YBA's uuid meaningful on both sides.
      assertThat(push.getMethod(), equalTo("PUT"));
      assertThat(push.getPath(), containsString("/api/export/config/" + endpoint.getUuid()));

      String body = push.getBody().readUtf8();
      assertThat(body, containsString(endpoint.getUuid().toString()));
      assertThat(body, containsString("byoc-prod"));
      // The destination always receives Perf Advisor's own universe-less metrics too.
      assertThat(body, containsString("\"includeGlobalPaMetrics\" : true"));
      // The YBM identifiers a BYOC gateway routes on have to survive the mapping.
      assertThat(body, containsString("account-1"));
      assertThat(body, containsString("project-1"));
    }
  }

  @Test
  public void testARejectedPushFails() throws Exception {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      PACollector collector = registerCollector(server);
      PerfAdvisorEndpoint endpoint = createEndpoint();
      Universe universe = ModelFactory.createUniverse(customer.getId());

      // An unreachable destination, a name a Perf Advisor operator already used, a rejected
      // credential: the subtask must fail so the registration behind it never runs.
      server.enqueue(new MockResponse().setResponseCode(400).setBody("{\"errors\":[\"nope\"]}"));

      PushPaExportConfig task = subtask(universe, collector.getUuid(), endpoint.getUuid());
      RuntimeException thrown = assertThrows(RuntimeException.class, task::run);
      // The collector's own message reaches the task failure, which is what makes a task drawer
      // entry actionable.
      assertThat(thrown.getMessage(), containsString("failed with exception"));
      assertThat(thrown.getMessage(), containsString("nope"));
    }
  }

  @Test
  public void testAnUnknownEndpointFails() throws Exception {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      PACollector collector = registerCollector(server);
      Universe universe = ModelFactory.createUniverse(customer.getId());

      PushPaExportConfig task = subtask(universe, collector.getUuid(), UUID.randomUUID());
      int baseline = server.getRequestCount();
      assertThrows(RuntimeException.class, task::run);
      // Nothing was sent: the endpoint is resolved before the collector is called.
      assertThat(server.getRequestCount() - baseline, equalTo(0));
    }
  }

  private PushPaExportConfig subtask(Universe universe, UUID collectorUuid, UUID endpointUuid) {
    PushPaExportConfig task =
        new PushPaExportConfig(
            mock(BaseTaskDependencies.class), perfAdvisorService, endpointService);
    PushPaExportConfig.Params params = new PushPaExportConfig.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.paCollectorUuid = collectorUuid;
    params.paEndpointUuid = endpointUuid;
    task.initialize(params);
    return task;
  }

  private PACollector registerCollector(MockWebServer server) throws Exception {
    HttpUrl baseUrl = server.url("/api/customer/" + customer.getUuid() + "/metadata");
    PACollector collector =
        PerfAdvisorServiceTest.createTestPlatform(
            customer.getUuid(), baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
    server.enqueue(
        new MockResponse().setBody(PerfAdvisorServiceTest.convertToCustomerMetadata(collector)));
    PACollector saved = perfAdvisorService.save(collector, false);
    // Drain the customer_metadata PUT the save above made, so the assertions see only the push.
    server.takeRequest();
    return saved;
  }

  private PerfAdvisorEndpoint createEndpoint() {
    PaEndpointAuth auth = new PaEndpointAuth();
    auth.setType(PaEndpointAuthType.BASIC);
    auth.setUsername("writer");
    auth.setPassword("s3cret");

    PerfAdvisorEndpoint endpoint = new PerfAdvisorEndpoint();
    endpoint.generateUUID();
    endpoint.setCustomerUUID(customer.getUuid());
    endpoint.setName("byoc-prod");
    endpoint.setType(PerfAdvisorEndpointType.BYOC);
    endpoint.setCollectionEndpoint("https://byoc.example.com");
    endpoint.setMetricsEndpoint("https://byoc.example.com/api/v1/otlp/metrics");
    endpoint.setMetricsType(ExportMetricsType.otlphttp);
    endpoint.setCollectionAuth(auth);
    endpoint.setMetricsAuth(auth);
    endpoint.setYbmAccountId("account-1");
    endpoint.setYbmProjectId("project-1");
    Date now = new Date();
    endpoint.setCreateTime(now);
    endpoint.setUpdateTime(now);
    endpoint.save();
    return endpoint;
  }
}
