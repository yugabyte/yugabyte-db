// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.pa;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.*;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.filters.PACollectorFilter;
import java.io.IOException;
import java.util.List;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import okhttp3.HttpUrl;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class PerfAdvisorServiceTest extends FakeDBApplication {

  private UUID defaultCustomerUuid;

  private PerfAdvisorService perfAdvisorService;

  @Before
  public void setUp() {
    defaultCustomerUuid = ModelFactory.testCustomer().getUuid();
    perfAdvisorService = app.injector().instanceOf(PerfAdvisorService.class);
  }

  @Test
  public void testCreateAndGet() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url("/api/customer_metadata/" + defaultCustomerUuid.toString());
      PACollector platform =
          createTestPlatform(baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
      server.enqueue(new MockResponse().setBody(convertToCustomerMetadata(platform)));
      PACollector updated = perfAdvisorService.save(platform, false);

      assertThat(updated, equalTo(platform));

      PACollector fromDb = perfAdvisorService.get(platform.getCustomerUUID(), platform.getUuid());
      assertThat(fromDb, equalTo(platform));
    }
  }

  @Test
  public void testGetOrBadRequest() {
    // Should raise an exception for random UUID.
    final UUID uuid = UUID.randomUUID();
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              perfAdvisorService.getOrBadRequest(defaultCustomerUuid, uuid);
            });
    assertThat(exception.getMessage(), equalTo("Troubleshooting Platform not found"));
  }

  @Test
  public void testListByCustomerUuid() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url("/api/customer_metadata/" + defaultCustomerUuid.toString());
      PACollector platform =
          createTestPlatform(baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
      server.enqueue(new MockResponse().setBody(convertToCustomerMetadata(platform)));
      perfAdvisorService.save(platform, false);

      PACollector platform2 =
          createTestPlatform(baseUrl.scheme() + "://127.0.0.1:" + baseUrl.port());
      server.enqueue(new MockResponse().setBody(convertToCustomerMetadata(platform2)));
      perfAdvisorService.save(platform2, false);

      UUID newCustomerUUID = ModelFactory.testCustomer().getUuid();
      PACollector otherCustomerPlatform =
          createTestPlatform(
              newCustomerUUID, baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
      server.enqueue(new MockResponse().setBody(convertToCustomerMetadata(otherCustomerPlatform)));
      perfAdvisorService.save(otherCustomerPlatform, false);

      PACollectorFilter filter =
          PACollectorFilter.builder().customerUuid(defaultCustomerUuid).build();
      List<PACollector> platforms = perfAdvisorService.list(filter);
      assertThat(platforms, containsInAnyOrder(platform, platform2));
    }
  }

  @Test
  public void testValidateDuplicateUrl() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url("/api/customer_metadata/" + defaultCustomerUuid.toString());
      PACollector platform =
          createTestPlatform(baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
      server.enqueue(new MockResponse().setBody(convertToCustomerMetadata(platform)));
      perfAdvisorService.save(platform, false);

      PACollector duplicate =
          createTestPlatform(baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
      PlatformServiceException exception =
          assertThrows(
              PlatformServiceException.class,
              () -> {
                perfAdvisorService.save(duplicate, false);
              });
      assertThat(
          exception.getMessage(),
          equalTo("errorJson: {\"tpUrl\":[\"platform with such url already exists.\"]}"));
    }
  }

  @Test
  public void testDelete() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url("/api/customer_metadata/" + defaultCustomerUuid.toString());
      PACollector platform =
          createTestPlatform(baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
      server.enqueue(new MockResponse().setBody(convertToCustomerMetadata(platform)));
      perfAdvisorService.save(platform, false);

      server.enqueue(new MockResponse());
      perfAdvisorService.delete(platform.getCustomerUUID(), platform.getUuid(), true);

      PACollector fromDb = perfAdvisorService.get(platform.getCustomerUUID(), platform.getUuid());
      assertThat(fromDb, nullValue());
    }
  }

  private PACollector createTestPlatform(String name) {
    return createTestPlatform(defaultCustomerUuid, name);
  }

  public static PACollector createTestPlatform(UUID customerUUID, String tpUrl) {
    PACollector platform = new PACollector();
    platform.setCustomerUUID(customerUUID);
    platform.setPaUrl(tpUrl);
    platform.setYbaUrl("http://localhost:9000");
    platform.setMetricsUrl("http://localhost:9090");
    platform.setApiToken("token");
    platform.setMetricsScrapePeriodSecs(10L);
    return platform;
  }

  public static String convertToCustomerMetadata(PACollector platform) {
    return Json.stringify(
        Json.toJson(
            new PerfAdvisorClient.CustomerMetadata()
                .setId(platform.getCustomerUUID())
                .setApiToken(platform.getApiToken())
                .setPlatformUrl(platform.getYbaUrl())
                .setMetricsUrl(platform.getMetricsUrl())
                .setMetricsScrapePeriodSec(platform.getMetricsScrapePeriodSecs())));
  }
}
