// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.pa;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.*;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.verify;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.operator.utils.KubernetesEnvironmentVariables;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.PACollectorFilter;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import okhttp3.HttpUrl;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.MockedStatic;
import org.mockito.Mockito;
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
    assertThat(exception.getMessage(), equalTo("PA Collector not found"));
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
          equalTo("errorJson: {\"paUrl\":[\"collector with such url already exists.\"]}"));
    }
  }

  @Test
  public void testListByEmbeddedFlag() throws IOException {
    // The embedded flag is how EmbeddedCollectorInitializer identifies "its" collector
    // after an HA restore (paUrl still points at the old active's PA and can't be used).
    // Verify the filter picks up only the embedded row.
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url("/api/customer_metadata/" + defaultCustomerUuid.toString());
      PACollector embeddedPlatform =
          createTestPlatform(baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
      embeddedPlatform.setEmbedded(true);
      server.enqueue(new MockResponse().setBody(convertToCustomerMetadata(embeddedPlatform)));
      perfAdvisorService.save(embeddedPlatform, false);

      PACollector externalPlatform =
          createTestPlatform(baseUrl.scheme() + "://127.0.0.1:" + baseUrl.port());
      server.enqueue(new MockResponse().setBody(convertToCustomerMetadata(externalPlatform)));
      perfAdvisorService.save(externalPlatform, false);

      PACollectorFilter filter =
          PACollectorFilter.builder().customerUuid(defaultCustomerUuid).embedded(true).build();
      List<PACollector> platforms = perfAdvisorService.list(filter);
      assertThat(platforms, contains(embeddedPlatform));

      PACollectorFilter nonEmbeddedFilter =
          PACollectorFilter.builder().customerUuid(defaultCustomerUuid).embedded(false).build();
      List<PACollector> nonEmbeddedPlatforms = perfAdvisorService.list(nonEmbeddedFilter);
      assertThat(nonEmbeddedPlatforms, contains(externalPlatform));
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

  private Universe universeWithTservers(int count) {
    Universe universe = ModelFactory.createUniverse();
    return Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          u.getUniverseDetails().nodeDetailsSet.clear();
          for (int i = 0; i < count; i++) {
            NodeDetails node = new NodeDetails();
            node.nodeName = "n" + i;
            node.isTserver = true;
            u.getUniverseDetails().nodeDetailsSet.add(node);
          }
        });
  }

  /** Answers each Prometheus query with the byte value of the first matching container. */
  private void mockContainerMemory(Map<String, long[]> limitAndUsedByContainer) {
    doAnswer(
            invocation -> {
              String query = invocation.getArgument(0);
              for (Map.Entry<String, long[]> e : limitAndUsedByContainer.entrySet()) {
                if (query.contains("container_name=\"" + e.getKey() + "\"")) {
                  long bytes = query.contains("working_set") ? e.getValue()[1] : e.getValue()[0];
                  MetricQueryResponse.Entry entry = new MetricQueryResponse.Entry();
                  entry.values = new ArrayList<>(List.of(ImmutablePair.of(0.0, (double) bytes)));
                  return new ArrayList<>(List.of(entry));
                }
              }
              return new ArrayList<>();
            })
        .when(mockMetricQueryHelper)
        .queryDirect(anyString());
  }

  private MockedStatic<KubernetesEnvironmentVariables> mockYugawarePod() {
    MockedStatic<KubernetesEnvironmentVariables> env =
        Mockito.mockStatic(KubernetesEnvironmentVariables.class);
    env.when(KubernetesEnvironmentVariables::isYbaRunningInKubernetes).thenReturn(true);
    env.when(KubernetesEnvironmentVariables::getPodName).thenReturn("yb-demo-yugaware-0");
    env.when(KubernetesEnvironmentVariables::getPodNamespace).thenReturn("yb-platform");
    return env;
  }

  @Test
  public void testK8sCollectorBudgetCheckedOnPerfAdvisorContainer() {
    Universe universe = universeWithTservers(3);
    long mb = 1024 * 1024;
    mockContainerMemory(Map.of("perf-advisor", new long[] {512 * mb, 400 * mb}));
    try (MockedStatic<KubernetesEnvironmentVariables> env = mockYugawarePod()) {
      PlatformServiceException e =
          assertThrows(
              PlatformServiceException.class,
              () ->
                  perfAdvisorService.validatePerfAdvisorMemory(
                      universe,
                      PerfAdvisorService.PaMemoryMode.NONE,
                      PerfAdvisorService.PaMemoryMode.COLLECTOR_ONLY,
                      "Cannot register"));
      assertThat(e.getMessage(), containsString("in container perf-advisor"));
    }
    verify(mockMetricQueryHelper, atLeastOnce())
        .queryDirect(
            argThat(
                q ->
                    q.contains("pod_name=~\"yb-demo-main-.*\"")
                        && q.contains("container_name=\"perf-advisor\"")
                        && q.contains("namespace=\"yb-platform\"")));
  }

  @Test
  public void testK8sCollectorCheckSkippedWithoutMetrics() {
    Universe universe = universeWithTservers(3);
    mockContainerMemory(Map.of());
    try (MockedStatic<KubernetesEnvironmentVariables> env = mockYugawarePod()) {
      perfAdvisorService.validatePerfAdvisorMemory(
          universe,
          PerfAdvisorService.PaMemoryMode.NONE,
          PerfAdvisorService.PaMemoryMode.COLLECTOR_ONLY,
          "Cannot register");
    }
  }

  @Test
  public void testK8sPrometheusBudgetStillRequiresMetrics() {
    Universe universe = universeWithTservers(3);
    long mb = 1024 * 1024;
    mockContainerMemory(Map.of("perf-advisor", new long[] {4096 * mb, 100 * mb}));
    try (MockedStatic<KubernetesEnvironmentVariables> env = mockYugawarePod()) {
      PlatformServiceException e =
          assertThrows(
              PlatformServiceException.class,
              () ->
                  perfAdvisorService.validatePerfAdvisorMemory(
                      universe,
                      PerfAdvisorService.PaMemoryMode.NONE,
                      PerfAdvisorService.PaMemoryMode.ADVANCED,
                      "Cannot register"));
      assertThat(
          e.getMessage(), containsString("Could not determine available memory for container"));
      assertThat(e.getMessage(), containsString("prometheus"));
    }
  }
}
