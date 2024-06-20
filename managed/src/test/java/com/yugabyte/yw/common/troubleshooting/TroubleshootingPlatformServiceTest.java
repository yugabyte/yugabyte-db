// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.troubleshooting;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.*;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.TroubleshootingPlatform;
import com.yugabyte.yw.models.filters.TroubleshootingPlatformFilter;
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
public class TroubleshootingPlatformServiceTest extends FakeDBApplication {

  private UUID defaultCustomerUuid;

  private TroubleshootingPlatformService troubleshootingPlatformService;

  @Before
  public void setUp() {
    defaultCustomerUuid = ModelFactory.testCustomer().getUuid();
    troubleshootingPlatformService =
        app.injector().instanceOf(TroubleshootingPlatformService.class);
  }

  @Test
  public void testCreateAndGet() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url("/api/customer_metadata/" + defaultCustomerUuid.toString());
      TroubleshootingPlatform platform =
          createTestPlatform(baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
      server.enqueue(new MockResponse().setBody(convertToCustomerMetadata(platform)));
      TroubleshootingPlatform updated = troubleshootingPlatformService.save(platform, false);

      assertThat(updated, equalTo(platform));

      TroubleshootingPlatform fromDb =
          troubleshootingPlatformService.get(platform.getCustomerUUID(), platform.getUuid());
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
              troubleshootingPlatformService.getOrBadRequest(defaultCustomerUuid, uuid);
            });
    assertThat(exception.getMessage(), equalTo("Troubleshooting Platform not found"));
  }

  @Test
  public void testListByCustomerUuid() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url("/api/customer_metadata/" + defaultCustomerUuid.toString());
      TroubleshootingPlatform platform =
          createTestPlatform(baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
      server.enqueue(new MockResponse().setBody(convertToCustomerMetadata(platform)));
      troubleshootingPlatformService.save(platform, false);

      TroubleshootingPlatform platform2 =
          createTestPlatform(baseUrl.scheme() + "://127.0.0.1:" + baseUrl.port());
      server.enqueue(new MockResponse().setBody(convertToCustomerMetadata(platform2)));
      troubleshootingPlatformService.save(platform2, false);

      UUID newCustomerUUID = ModelFactory.testCustomer().getUuid();
      TroubleshootingPlatform otherCustomerPlatform =
          createTestPlatform(
              newCustomerUUID, baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
      server.enqueue(new MockResponse().setBody(convertToCustomerMetadata(otherCustomerPlatform)));
      troubleshootingPlatformService.save(otherCustomerPlatform, false);

      TroubleshootingPlatformFilter filter =
          TroubleshootingPlatformFilter.builder().customerUuid(defaultCustomerUuid).build();
      List<TroubleshootingPlatform> platforms = troubleshootingPlatformService.list(filter);
      assertThat(platforms, containsInAnyOrder(platform, platform2));
    }
  }

  @Test
  public void testValidateDuplicateUrl() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url("/api/customer_metadata/" + defaultCustomerUuid.toString());
      TroubleshootingPlatform platform =
          createTestPlatform(baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
      server.enqueue(new MockResponse().setBody(convertToCustomerMetadata(platform)));
      troubleshootingPlatformService.save(platform, false);

      TroubleshootingPlatform duplicate =
          createTestPlatform(baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
      PlatformServiceException exception =
          assertThrows(
              PlatformServiceException.class,
              () -> {
                troubleshootingPlatformService.save(duplicate, false);
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
      TroubleshootingPlatform platform =
          createTestPlatform(baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
      server.enqueue(new MockResponse().setBody(convertToCustomerMetadata(platform)));
      troubleshootingPlatformService.save(platform, false);

      server.enqueue(new MockResponse());
      troubleshootingPlatformService.delete(platform.getCustomerUUID(), platform.getUuid(), true);

      TroubleshootingPlatform fromDb =
          troubleshootingPlatformService.get(platform.getCustomerUUID(), platform.getUuid());
      assertThat(fromDb, nullValue());
    }
  }

  private TroubleshootingPlatform createTestPlatform(String name) {
    return createTestPlatform(defaultCustomerUuid, name);
  }

  public static TroubleshootingPlatform createTestPlatform(UUID customerUUID, String tpUrl) {
    TroubleshootingPlatform platform = new TroubleshootingPlatform();
    platform.setCustomerUUID(customerUUID);
    platform.setTpUrl(tpUrl);
    platform.setYbaUrl("http://localhost:9000");
    platform.setMetricsUrl("http://localhost:9090");
    platform.setApiToken("token");
    platform.setMetricsScrapePeriodSecs(10L);
    return platform;
  }

  public static String convertToCustomerMetadata(TroubleshootingPlatform platform) {
    return Json.stringify(
        Json.toJson(
            new TroubleshootingPlatformClient.CustomerMetadata()
                .setId(platform.getCustomerUUID())
                .setApiToken(platform.getApiToken())
                .setPlatformUrl(platform.getYbaUrl())
                .setMetricsUrl(platform.getMetricsUrl())
                .setMetricsScrapePeriodSec(platform.getMetricsScrapePeriodSecs())));
  }
}
