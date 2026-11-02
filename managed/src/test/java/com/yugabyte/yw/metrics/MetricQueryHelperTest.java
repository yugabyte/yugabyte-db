// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.metrics;

import static junit.framework.TestCase.assertTrue;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.core.AllOf.allOf;
import static org.hamcrest.core.Is.is;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.mockito.ArgumentMatchers.*;
import static org.mockito.Mockito.*;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.*;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.MetricQueryParams;
import com.yugabyte.yw.metrics.data.AlertData;
import com.yugabyte.yw.metrics.data.AlertState;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.MetricConfig;
import com.yugabyte.yw.models.MetricConfigDefinition;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.time.ZoneOffset;
import java.time.ZonedDateTime;
import java.util.*;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadPoolExecutor;
import org.hamcrest.CoreMatchers;
import org.hamcrest.Matchers;
import org.hamcrest.core.IsInstanceOf;
import org.joda.time.DateTime;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.yaml.snakeyaml.Yaml;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class MetricQueryHelperTest extends FakeDBApplication {

  private static final int DEFAULT_POINTS = 250;

  MetricQueryHelper metricQueryHelper;

  @Mock Config mockAppConfig;

  @Mock PlatformExecutorFactory mockPlatformExecutorFactory;

  @Mock RuntimeConfGetter runtimeConfGetter;

  @Mock WSClientRefresher wsClientRefresher;

  @Mock YBClientService ybService;

  MetricConfigDefinition validMetric;

  Customer customer;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    JsonNode configJson = Json.parse("{\"metric\": \"my_valid_metric\", \"function\": \"sum\"}");
    MetricConfig metricConfig = MetricConfig.create("valid_metric", configJson);
    metricConfig.save();
    validMetric = metricConfig.getConfig();
    ThreadPoolExecutor executor = (ThreadPoolExecutor) Executors.newFixedThreadPool(1);
    when(mockAppConfig.getString("yb.metrics.url")).thenReturn("foo://bar/api/v1");
    when(mockAppConfig.getString("yb.metrics.scrape_interval")).thenReturn("1s");
    when(mockPlatformExecutorFactory.createFixedExecutor(any(), anyInt(), any()))
        .thenReturn(executor);
    when(runtimeConfGetter.getStaticConf()).thenReturn(mockAppConfig);
    when(runtimeConfGetter.getGlobalConf(GlobalConfKeys.metricsAuth)).thenReturn(false);
    when(runtimeConfGetter.getGlobalConf(GlobalConfKeys.metricsLinkUseBrowserFqdn))
        .thenReturn(true);
    when(runtimeConfGetter.getConfForScope(customer, CustomerConfKeys.MetricsDefaultPoints))
        .thenReturn(DEFAULT_POINTS);

    MetricUrlProvider metricUrlProvider = new MetricUrlProvider(runtimeConfGetter);
    metricQueryHelper =
        new MetricQueryHelper(
            mockAppConfig,
            runtimeConfGetter,
            wsClientRefresher,
            metricUrlProvider,
            mockPlatformExecutorFactory,
            ybService) {
          @Override
          protected ApiHelper getApiHelper() {
            return mockApiHelper;
          }
        };
  }

  // Thin wrapper for the convenience `query(customer, metricKeys, params)` shape that used to
  // live on MetricQueryHelper itself. It was only ever called from these tests, so it has been
  // moved here to keep the production API focused on the (metricKeys, params, filterOverrides)
  // signature that real callers actually use.
  private JsonNode query(Customer customer, List<String> metricKeys, Map<String, String> params) {
    return metricQueryHelper.query(customer, metricKeys, params, Collections.emptyMap());
  }

  @Test
  public void testQueryWithInvalidParams() {
    try {
      query(customer, Collections.emptyList(), Collections.emptyMap());
    } catch (PlatformServiceException re) {
      AssertHelper.assertBadRequest(
          re.buildResult(fakeRequest), "Empty metricsWithSettings data provided.");
    }
  }

  @Test
  public void testQueryWithInvalidFilterParams() {
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1479281737");
    params.put("filters", "my-own-filter");

    try {
      query(customer, ImmutableList.of("valid_metric"), params);
    } catch (PlatformServiceException re) {
      AssertHelper.assertBadRequest(
          re.buildResult(fakeRequest), "Invalid filter params provided, it should be a hash.");
    }
  }

  @Test
  public void testQueryShortDifference() {
    HashMap<String, String> params = new HashMap<>();
    long startTimestamp = 1646925800;
    params.put("start", String.valueOf(startTimestamp));
    params.put("end", String.valueOf(startTimestamp - 1));

    try {
      query(customer, ImmutableList.of("valid_metric"), params);
    } catch (PlatformServiceException re) {
      AssertHelper.assertBadRequest(
          re.buildResult(fakeRequest), "Start time cannot be greater than the end time");
    }
  }

  @Test
  public void testQueryInvalidStep() {
    HashMap<String, String> params = new HashMap<>();
    long startTimestamp = 1646925800;
    params.put("start", String.valueOf(startTimestamp));
    params.put("end", String.valueOf(startTimestamp + DEFAULT_POINTS));
    params.put("step", "qwe");

    try {
      query(customer, ImmutableList.of("valid_metric"), params);
    } catch (PlatformServiceException re) {
      final Result result = re.buildResult(fakeRequest);
      AssertHelper.assertBadRequest(result, "Step should be a valid integer");
    }
  }

  @Test
  public void testQueryLowStep() {
    HashMap<String, String> params = new HashMap<>();
    long startTimestamp = 1646925800;
    params.put("start", String.valueOf(startTimestamp));
    params.put("end", String.valueOf(startTimestamp + DEFAULT_POINTS));
    params.put("step", "0");

    try {
      query(customer, ImmutableList.of("valid_metric"), params);
    } catch (PlatformServiceException re) {
      String expectedErr = "Step should not be less than 1 second";
      AssertHelper.assertBadRequest(re.buildResult(fakeRequest), expectedErr);
    }
  }

  @Test
  public void testQuerySingleMetricWithoutEndTime() {
    DateTime date = DateTime.now().minusSeconds(DEFAULT_POINTS + 100);
    Integer startTimestamp = Math.toIntExact(date.getMillis() / 1000);
    HashMap<String, String> params = new HashMap<>();
    params.put("start", startTimestamp.toString());

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]}]}}");

    ArgumentCaptor<String> queryUrl = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map> queryParam = ArgumentCaptor.forClass(Map.class);

    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap())).thenReturn(responseJson);
    query(customer, ImmutableList.of("valid_metric"), params);
    verify(mockApiHelper)
        .getRequest(queryUrl.capture(), anyMap(), (Map<String, String>) queryParam.capture());

    assertThat(queryUrl.getValue(), allOf(notNullValue(), equalTo("foo://bar/api/v1/query")));
    assertThat(
        queryParam.getValue(), allOf(notNullValue(), IsInstanceOf.instanceOf(HashMap.class)));

    Map<String, String> graphQueryParam = queryParam.getValue();
    assertThat(
        graphQueryParam.get("query"), allOf(notNullValue(), equalTo("sum(my_valid_metric)")));
    assertThat(
        Integer.parseInt(graphQueryParam.get("time")),
        allOf(notNullValue(), equalTo(startTimestamp)));
    assertThat(Integer.parseInt(graphQueryParam.get("step")), is(notNullValue()));
  }

  @Test
  public void testQuerySingleMetricWithEndTime() {
    DateTime date = DateTime.now();
    long startTimestamp = date.minusMinutes(100).getMillis() / 1000;
    long endTimestamp = date.getMillis() / 1000;
    HashMap<String, String> params = new HashMap<>();
    params.put("start", Long.toString(startTimestamp));
    params.put("end", Long.toString(endTimestamp));
    long timeDifference = endTimestamp - startTimestamp;
    int step = Math.round(timeDifference / 250);
    long adjustedStartTimestamp = startTimestamp - startTimestamp % step;
    long adjustedEndTimestamp = endTimestamp - startTimestamp % step;
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]}]}}");

    ArgumentCaptor<String> queryUrl = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map> queryParam = ArgumentCaptor.forClass(Map.class);

    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap())).thenReturn(responseJson);
    query(customer, ImmutableList.of("valid_metric"), params);
    verify(mockApiHelper)
        .getRequest(queryUrl.capture(), anyMap(), (Map<String, String>) queryParam.capture());

    assertThat(queryUrl.getValue(), allOf(notNullValue(), equalTo("foo://bar/api/v1/query_range")));
    assertThat(
        queryParam.getValue(), allOf(notNullValue(), IsInstanceOf.instanceOf(HashMap.class)));

    Map<String, String> graphQueryParam = queryParam.getValue();
    assertThat(
        graphQueryParam.get("query"), allOf(notNullValue(), equalTo("sum(my_valid_metric)")));
    assertThat(
        Long.parseLong(graphQueryParam.get("start")),
        allOf(notNullValue(), equalTo(adjustedStartTimestamp)));
    assertThat(
        Long.parseLong(graphQueryParam.get("end")),
        allOf(notNullValue(), equalTo(adjustedEndTimestamp)));
    assertThat(Integer.parseInt(graphQueryParam.get("step")), allOf(notNullValue(), equalTo(24)));
  }

  @Test
  public void testQuerySingleMetricWithStep() {
    DateTime date = DateTime.now();
    long startTimestamp = date.minusMinutes(1).getMillis() / 1000;
    long endTimestamp = date.getMillis() / 1000;
    HashMap<String, String> params = new HashMap<>();
    params.put("start", Long.toString(startTimestamp));
    params.put("end", Long.toString(endTimestamp));
    params.put("step", "30");
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]}]}}");

    int step = 30;
    long adjustedStartTimestamp = startTimestamp - startTimestamp % step;
    long adjustedEndTimestamp = endTimestamp - startTimestamp % step;

    ArgumentCaptor<String> queryUrl = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map> queryParam = ArgumentCaptor.forClass(Map.class);

    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap())).thenReturn(responseJson);
    query(customer, ImmutableList.of("valid_metric"), params);
    verify(mockApiHelper)
        .getRequest(queryUrl.capture(), anyMap(), (Map<String, String>) queryParam.capture());

    assertThat(queryUrl.getValue(), allOf(notNullValue(), equalTo("foo://bar/api/v1/query_range")));
    assertThat(
        queryParam.getValue(), allOf(notNullValue(), IsInstanceOf.instanceOf(HashMap.class)));

    Map<String, String> graphQueryParam = queryParam.getValue();
    assertThat(
        graphQueryParam.get("query"), allOf(notNullValue(), equalTo("sum(my_valid_metric)")));
    assertThat(
        Long.parseLong(graphQueryParam.get("start")),
        allOf(notNullValue(), equalTo(adjustedStartTimestamp)));
    assertThat(
        Long.parseLong(graphQueryParam.get("end")),
        allOf(notNullValue(), equalTo(adjustedEndTimestamp)));
    assertThat(Integer.parseInt(graphQueryParam.get("step")), allOf(notNullValue(), equalTo(step)));
  }

  @Test
  public void testDirectQuerySingleValue() {

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"__name__\":\"foobar\", \"node_prefix\":\"yb-test-1\"},\"value\":"
                + "[1479278137,\"0.027751899056199826\"]}]}}");

    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap())).thenReturn(responseJson);

    ArrayList<MetricQueryResponse.Entry> results = metricQueryHelper.queryDirect("foobar");
    assertEquals(results.size(), 1);
    assertEquals(results.get(0).labels.size(), 2);
    assertEquals(results.get(0).labels.get("node_prefix"), "yb-test-1");
    assertEquals(results.get(0).values.size(), 1);
    assertEquals(results.get(0).values.get(0).getLeft().intValue(), 1479278137);
    assertEquals(results.get(0).values.get(0).getRight().doubleValue(), 0.028, 0.005);
  }

  @Test
  public void testDirectQueryMultipleValues() {

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"__name__\":\"foobar\", \"node_prefix\":\"yb-test-1\"},\"values\":"
                + "[[1479278132,\"0.037751899056199826\"], [1479278137,\"0.027751899056199826\"]"
                + "]}]}}");

    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap())).thenReturn(responseJson);

    ArrayList<MetricQueryResponse.Entry> results = metricQueryHelper.queryDirect("foobar");
    assertEquals(results.size(), 1);
    assertEquals(results.get(0).labels.size(), 2);
    assertEquals(results.get(0).labels.get("node_prefix"), "yb-test-1");
    assertEquals(results.get(0).values.size(), 2);
    assertEquals(results.get(0).values.get(0).getLeft().intValue(), 1479278132);
    assertEquals(results.get(0).values.get(1).getLeft().intValue(), 1479278137);
    assertEquals(results.get(0).values.get(0).getRight().doubleValue(), 0.038, 0.005);
    assertEquals(results.get(0).values.get(1).getRight().doubleValue(), 0.028, 0.005);
  }

  @Test
  public void testQueryMultipleMetrics() {
    HashMap<String, String> params = new HashMap<>();
    params.put("start", "1481147528");
    params.put("end", "1481147648");

    JsonNode configJson = Json.parse("{\"metric\": \"my_valid_metric2\", \"function\": \"avg\"}");
    MetricConfig metricConfig = MetricConfig.create("valid_metric2", configJson);
    metricConfig.save();
    MetricConfigDefinition validMetric2 = metricConfig.getConfig();

    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]}]}}");

    ArgumentCaptor<String> queryUrl = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map> queryParam = ArgumentCaptor.forClass(Map.class);
    List<String> metricKeys = ImmutableList.of("valid_metric2", "valid_metric");

    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap())).thenReturn(responseJson);
    JsonNode result = query(customer, metricKeys, params);
    verify(mockApiHelper, times(2))
        .getRequest(queryUrl.capture(), anyMap(), (Map<String, String>) queryParam.capture());
    assertThat(queryUrl.getValue(), allOf(notNullValue(), equalTo("foo://bar/api/v1/query_range")));
    assertThat(
        queryParam.getValue(), allOf(notNullValue(), IsInstanceOf.instanceOf(HashMap.class)));

    List<String> expectedQueryStrings = new ArrayList<>();
    expectedQueryStrings.add(validMetric.getQuery(new HashMap<>(), 60 /* queryRangeSecs */));
    expectedQueryStrings.add(validMetric2.getQuery(new HashMap<>(), 60 /* queryRangeSecs */));

    for (Map<String, String> capturedQueryParam : queryParam.getAllValues()) {
      assertTrue(expectedQueryStrings.contains(capturedQueryParam.get("query")));
      assertTrue(metricKeys.contains(capturedQueryParam.get("queryKey")));
      assertThat(
          Integer.parseInt(capturedQueryParam.get("start").toString()),
          allOf(notNullValue(), equalTo(1481147526)));
      assertThat(
          Integer.parseInt(capturedQueryParam.get("step").toString()),
          allOf(notNullValue(), equalTo(3)));
      assertThat(
          Integer.parseInt(capturedQueryParam.get("end").toString()),
          allOf(notNullValue(), equalTo(1481147646)));
    }
  }

  @Test
  public void testQueryAlerts() throws IOException {
    JsonNode responseJson = Json.parse(TestUtils.readResource("alert/alerts_query.json"));

    ArgumentCaptor<String> queryUrl = ArgumentCaptor.forClass(String.class);

    when(mockApiHelper.getRequest(anyString(), any())).thenReturn(responseJson);
    List<AlertData> alerts = metricQueryHelper.queryAlerts();
    verify(mockApiHelper).getRequest(queryUrl.capture(), any());

    assertThat(queryUrl.getValue(), allOf(notNullValue(), equalTo("foo://bar/api/v1/alerts")));

    AlertData alertData =
        AlertData.builder()
            .activeAt(
                ZonedDateTime.parse("2018-07-04T20:27:12.60602144+02:00")
                    .withZoneSameInstant(ZoneOffset.UTC))
            .annotations(ImmutableMap.of("summary", "Clock Skew Alert for universe Test is firing"))
            .labels(
                ImmutableMap.of(
                    "customer_uuid", "199bccbc-6295-4676-950e-c0049b8adfa9",
                    "definition_uuid", "199bccbc-6295-4676-950e-c0049b8adfa8",
                    "definition_name", "Clock Skew Alert"))
            .state(AlertState.firing)
            .value(1)
            .build();
    assertThat(alerts, Matchers.contains(alertData));
  }

  @Test
  public void testQueryAlertsError() throws IOException {
    JsonNode responseJson = Json.parse(TestUtils.readResource("alert/alerts_query_error.json"));

    ArgumentCaptor<String> queryUrl = ArgumentCaptor.forClass(String.class);

    when(mockApiHelper.getRequest(anyString(), any())).thenReturn(responseJson);
    try {
      metricQueryHelper.queryAlerts();
    } catch (Exception e) {
      assertThat(e, CoreMatchers.instanceOf(RuntimeException.class));
    }
    verify(mockApiHelper).getRequest(queryUrl.capture(), any());

    assertThat(queryUrl.getValue(), allOf(notNullValue(), equalTo("foo://bar/api/v1/alerts")));
  }

  // Captures each key's query from one metrics request.
  @SuppressWarnings("unchecked")
  private Map<String, String> queryKeys(Universe universe, List<String> keys, boolean tserverOnly) {
    MetricQueryParams params = new MetricQueryParams();
    params.setNodePrefix(universe.getUniverseDetails().nodePrefix);
    params.setStart(1481147528L);
    params.setEnd(1481147648L);
    params.setMetrics(keys);
    if (tserverOnly) {
      params.setServerType(UniverseTaskBase.ServerType.TSERVER);
    }
    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap()))
        .thenReturn(Json.parse("{\"status\":\"success\",\"data\":{\"result\":[]}}"));
    metricQueryHelper.query(customer, params);
    ArgumentCaptor<Map> queryParam = ArgumentCaptor.forClass(Map.class);
    verify(mockApiHelper, times(keys.size()))
        .getRequest(anyString(), anyMap(), queryParam.capture());
    Map<String, String> queries = new HashMap<>();
    for (Map<String, String> captured : queryParam.getAllValues()) {
      queries.put(captured.get("queryKey"), captured.get("query"));
    }
    return queries;
  }

  private Universe kubernetesUniverse(String name, boolean newNamingStyle, String... azCodes) {
    MetricConfig.loadConfig(
        new Yaml().load(getClass().getClassLoader().getResourceAsStream("metric/metrics.yml")));
    Universe universe = ModelFactory.createUniverse(name, customer.getId(), CloudType.kubernetes);
    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    if (Region.getByCode(provider, "region-1") == null) {
      Region region = Region.create(provider, "region-1", "Region 1", "default-image");
      CloudInfoInterface.setCloudProviderInfoFromConfig(
          region, ImmutableMap.of("KUBECONFIG", "/tmp/kubeconfig"));
      region.save();
      int i = 1;
      for (String azCode : azCodes) {
        AvailabilityZone.createOrThrow(region, azCode, "AZ " + i, "subnet-" + i++);
      }
    }
    return Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> u.getUniverseDetails().useNewHelmNamingStyle = newNamingStyle);
  }

  // The tasks name releases and nodes by zone code, so a zone whose name differs from its code
  // still selects its pods and claims and names their lines as the YBA nodes.
  @Test
  public void testMultiAzReleasesUseTheZoneCode() {
    Universe universe = kubernetesUniverse("k8s-multi-az", true, "az-1", "az-2");
    Map<String, String> queries =
        queryKeys(
            universe, ImmutableList.of("container_memory_usage", "container_volume_stats"), false);
    K8sPodNodeNameResolver resolver = K8sPodNodeNameResolver.forUniverse(universe);

    for (String azCode : ImmutableList.of("az-1", "az-2")) {
      String release =
          KubernetesUtil.getHelmReleaseName(
              true,
              universe.getUniverseDetails().nodePrefix,
              universe.getName(),
              azCode,
              false,
              true);
      assertTrue(
          release, queries.get("container_memory_usage").contains(release + "-yb-tserver-(.*)"));
      assertTrue(
          release,
          queries.get("container_volume_stats").contains("(.*)-" + release + "-yb-tserver-(.*)"));
      assertEquals(
          "yb-tserver-0_" + azCode,
          resolver.resolveNodeName(release + "-yb-tserver-0", null, "us-west1-a"));
    }
  }

  // Universes in new naming style can share a namespace, and kubelet volume series carry no pod
  // name, so the volume keys select the universe's tserver claims by release.
  @Test
  public void testContainerVolumeKeysSelectTheUniverseClaims() {
    List<String> releases = new ArrayList<>();
    Universe universe = null;
    for (String name : ImmutableList.of("k8s-shared-a", "k8s-shared-b")) {
      universe = kubernetesUniverse(name, true, "az-1");
      releases.add(
          KubernetesUtil.getHelmReleaseName(
              false,
              universe.getUniverseDetails().nodePrefix,
              universe.getName(),
              "az-1",
              false,
              true));
    }
    Map<String, String> queries =
        queryKeys(
            universe,
            ImmutableList.of("container_volume_stats", "container_volume_usage_percent"),
            false);

    String claim = "persistentvolumeclaim=~\"(.*)-" + releases.get(1) + "-yb-tserver-(.*)\"";
    for (String key :
        ImmutableList.of("container_volume_stats", "container_volume_usage_percent")) {
      String query = queries.get(key);
      assertTrue(query, query.contains(claim));
      assertFalse(query, query.contains(releases.get(0)));
      assertFalse(query, query.contains("persistentvolumeclaim=~\"(.*)-yb-tserver-(.*)\""));
    }
    assertTrue(queries.get("container_volume_usage_percent").split("/", 2)[1].contains(claim));
  }

  @Test
  public void testContainerVolumeKeysSelectTheChosenPods() {
    Universe universe = kubernetesUniverse("k8s-scoped", true, "az-1");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              NodeDetails node = ApiUtils.getDummyNodeDetails(1, NodeDetails.NodeState.Live);
              node.isTserver = true;
              node.cloudInfo.kubernetesPodName = "yb-tserver-0";
              node.cloudInfo.kubernetesNamespace = "k8s-scoped";
              u.getUniverseDetails().nodeDetailsSet = new HashSet<>(Collections.singleton(node));
            });
    Map<String, String> queries =
        queryKeys(universe, ImmutableList.of("container_volume_stats"), true);

    assertTrue(
        queries
            .get("container_volume_stats")
            .contains("persistentvolumeclaim=~\"(.*)-yb-tserver-0\""));
  }

  // The tasks read KUBENAMESPACE from the region and zone config merged, so a namespace set on the
  // region applies to its zones; old-naming pods, matched by namespace, resolve in it.
  @Test
  public void testNamespaceSetAtRegionLevel() {
    Universe universe = kubernetesUniverse("k8s-region-ns", false, "az-1");
    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    Region region = Region.getByCode(provider, "region-1");
    CloudInfoInterface.setCloudProviderInfoFromConfig(
        region, ImmutableMap.of("KUBECONFIG", "/tmp/kubeconfig", "KUBENAMESPACE", "shared-ns"));
    region.save();
    Map<String, String> queries =
        queryKeys(universe, ImmutableList.of("container_memory_usage"), false);

    assertTrue(
        queries.get("container_memory_usage"),
        queries.get("container_memory_usage").contains("namespace=\"shared-ns\""));
    assertEquals(
        "yb-tserver-0",
        K8sPodNodeNameResolver.forUniverse(universe)
            .resolveNodeName("yb-tserver-0", "shared-ns", null));
  }

  // One request can mix keys over cAdvisor/kubelet series with keys over node_exporter and DB
  // series; each key must get the filters its own series carry.
  @Test
  @SuppressWarnings("unchecked")
  public void testKubernetesKeysGetPodFiltersInMixedRequest() {
    MetricConfig.loadConfig(
        new Yaml().load(getClass().getClassLoader().getResourceAsStream("metric/metrics.yml")));
    Universe universe =
        ModelFactory.createUniverse("k8s-metrics", customer.getId(), CloudType.kubernetes);
    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    Region region = Region.create(provider, "region-1", "Region 1", "default-image");
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    MetricQueryParams params = new MetricQueryParams();
    params.setNodePrefix(universe.getUniverseDetails().nodePrefix);
    params.setStart(1481147528L);
    params.setEnd(1481147648L);
    params.setMetrics(
        ImmutableList.of(
            "k8s_cpu_usage",
            "k8s_memory_usage",
            "k8s_volume_disk_used_size_total",
            "container_memory_usage",
            "disk_iops"));
    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap()))
        .thenReturn(Json.parse("{\"status\":\"success\",\"data\":{\"result\":[]}}"));

    metricQueryHelper.query(customer, params);

    ArgumentCaptor<Map> queryParam = ArgumentCaptor.forClass(Map.class);
    verify(mockApiHelper, times(5)).getRequest(anyString(), anyMap(), queryParam.capture());
    Map<String, String> queries = new HashMap<>();
    for (Map<String, String> captured : queryParam.getAllValues()) {
      queries.put(captured.get("queryKey"), captured.get("query"));
    }
    for (String key : ImmutableList.of("k8s_cpu_usage", "k8s_memory_usage")) {
      assertTrue(key, queries.get(key).contains("namespace="));
      assertFalse(key, queries.get(key).contains("node_prefix"));
    }
    assertTrue(queries.get("k8s_cpu_usage").contains("/ on (pod_name, namespace)"));
    String cpuRequests = queries.get("k8s_cpu_usage").split("/ on \\(pod_name, namespace\\)", 2)[1];
    assertTrue(cpuRequests, cpuRequests.contains("ybp_container_resource_requests_cpu_cores{"));
    assertTrue(cpuRequests, cpuRequests.contains("namespace="));
    assertTrue(queries.get("k8s_cpu_usage").contains("container_name=~\"yb-master|yb-tserver\""));
    assertTrue(
        queries
            .get("k8s_volume_disk_used_size_total")
            .contains("persistentvolumeclaim=~\"(.*)datadir[0-9]+-yb-(master|tserver)-(.*)\""));
    assertFalse(queries.get("container_memory_usage").contains("node_prefix"));
    assertFalse(queries.get("container_memory_usage").contains("master"));
    assertTrue(queries.get("disk_iops").contains("node_prefix="));
    assertFalse(queries.get("disk_iops").contains("namespace="));
  }

  // With a server type selected, a k8s_ key keeps the containers its definition names, while
  // the container_ keys get the selected pods' DB containers.
  @Test
  @SuppressWarnings("unchecked")
  public void testKubernetesKeysKeepTheirContainersInAScopedView() {
    MetricConfig.loadConfig(
        new Yaml().load(getClass().getClassLoader().getResourceAsStream("metric/metrics.yml")));
    JsonNode sidecarKey =
        Json.parse(
            "{\"metric\": \"container_memory_working_set_bytes\", \"function\": \"sum\","
                + " \"filters\": {\"container_name\": \"yb-node-exporter\"}}");
    MetricConfig.create("k8s_test_sidecar_memory", sidecarKey).save();
    Universe universe =
        ModelFactory.createUniverse("k8s-scoped", customer.getId(), CloudType.kubernetes);
    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    Region region = Region.create(provider, "region-1", "Region 1", "default-image");
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              NodeDetails node = ApiUtils.getDummyNodeDetails(1, NodeDetails.NodeState.Live);
              node.isTserver = true;
              node.cloudInfo.kubernetesPodName = "yb-tserver-0";
              node.cloudInfo.kubernetesNamespace = "k8s-scoped";
              u.getUniverseDetails().nodeDetailsSet = new HashSet<>(Collections.singleton(node));
            });
    MetricQueryParams params = new MetricQueryParams();
    params.setNodePrefix(universe.getUniverseDetails().nodePrefix);
    params.setStart(1481147528L);
    params.setEnd(1481147648L);
    params.setServerType(UniverseTaskBase.ServerType.TSERVER);
    params.setMetrics(
        ImmutableList.of(
            "k8s_test_sidecar_memory", "container_memory_usage", "k8s_volume_disk_volume_used"));
    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap()))
        .thenReturn(Json.parse("{\"status\":\"success\",\"data\":{\"result\":[]}}"));

    metricQueryHelper.query(customer, params);

    ArgumentCaptor<Map> queryParam = ArgumentCaptor.forClass(Map.class);
    verify(mockApiHelper, times(3)).getRequest(anyString(), anyMap(), queryParam.capture());
    Map<String, String> queries = new HashMap<>();
    for (Map<String, String> captured : queryParam.getAllValues()) {
      queries.put(captured.get("queryKey"), captured.get("query"));
    }
    assertTrue(
        queries.get("k8s_test_sidecar_memory").contains("container_name=\"yb-node-exporter\""));
    assertTrue(queries.get("container_memory_usage").contains("container_name=\"yb-tserver\""));
    assertTrue(
        queries.get("k8s_volume_disk_volume_used").contains("(.*)datadir[0-9]+-yb-tserver-0\""));
  }

  // In new naming style the unscoped view selects the universe's releases, masters included for
  // the k8s_ keys and tservers only for the container_ keys.
  @Test
  @SuppressWarnings("unchecked")
  public void testKubernetesKeysSelectReleasesInNewNamingStyle() {
    MetricConfig.loadConfig(
        new Yaml().load(getClass().getClassLoader().getResourceAsStream("metric/metrics.yml")));
    Universe universe =
        ModelFactory.createUniverse("k8s-new-naming", customer.getId(), CloudType.kubernetes);
    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    Region region = Region.create(provider, "region-1", "Region 1", "default-image");
    AvailabilityZone az = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(), u -> u.getUniverseDetails().useNewHelmNamingStyle = true);
    String release =
        KubernetesUtil.getHelmReleaseName(
            false,
            universe.getUniverseDetails().nodePrefix,
            universe.getName(),
            az.getCode(),
            false,
            true);
    MetricQueryParams params = new MetricQueryParams();
    params.setNodePrefix(universe.getUniverseDetails().nodePrefix);
    params.setStart(1481147528L);
    params.setEnd(1481147648L);
    params.setMetrics(
        ImmutableList.of("k8s_cpu_usage", "k8s_volume_disk_volume_used", "container_memory_usage"));
    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap()))
        .thenReturn(Json.parse("{\"status\":\"success\",\"data\":{\"result\":[]}}"));

    metricQueryHelper.query(customer, params);

    ArgumentCaptor<Map> queryParam = ArgumentCaptor.forClass(Map.class);
    verify(mockApiHelper, times(3)).getRequest(anyString(), anyMap(), queryParam.capture());
    Map<String, String> queries = new HashMap<>();
    for (Map<String, String> captured : queryParam.getAllValues()) {
      queries.put(captured.get("queryKey"), captured.get("query"));
    }
    assertTrue(
        queries
            .get("k8s_cpu_usage")
            .contains("pod_name=~\"" + release + "-yb-(master|tserver)-(.*)\""));
    String cpuRequests = queries.get("k8s_cpu_usage").split("/ on \\(pod_name, namespace\\)", 2)[1];
    assertTrue(
        cpuRequests,
        cpuRequests.contains("pod_name=~\"" + release + "-yb-(master|tserver)-(.*)\""));
    assertTrue(
        queries
            .get("k8s_volume_disk_volume_used")
            .contains("(.*)datadir[0-9]+-" + release + "-yb-(master|tserver)-(.*)\""));
    assertTrue(
        queries
            .get("container_memory_usage")
            .contains("pod_name=~\"" + release + "-yb-tserver-(.*)\""));
  }
}
