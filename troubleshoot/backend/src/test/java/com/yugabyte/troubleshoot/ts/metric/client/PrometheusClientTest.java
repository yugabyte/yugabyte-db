package com.yugabyte.troubleshoot.ts.metric.client;

import static org.assertj.core.api.Assertions.assertThat;
import static org.springframework.test.web.client.match.MockRestRequestMatchers.requestTo;
import static org.springframework.test.web.client.response.MockRestResponseCreators.withSuccess;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.troubleshoot.ts.TestUtils;
import com.yugabyte.troubleshoot.ts.metric.models.MetricQuery;
import com.yugabyte.troubleshoot.ts.metric.models.MetricRangeQuery;
import com.yugabyte.troubleshoot.ts.metric.models.MetricResponse;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneId;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.http.MediaType;
import org.springframework.test.context.ActiveProfiles;
import org.springframework.test.web.client.MockRestServiceServer;
import org.springframework.web.client.RestTemplate;

@SpringBootTest
@ActiveProfiles("test")
public class PrometheusClientTest {

  @Autowired private PrometheusClient client;

  @Autowired private RestTemplate prometheusClientTemplate;

  private MockRestServiceServer server;

  @Autowired private ObjectMapper objectMapper;

  @BeforeEach
  public void setUp() {
    server = MockRestServiceServer.createServer(prometheusClientTemplate);
  }

  @Test
  public void testVectorQuery() {
    String queryResponse = TestUtils.readResource("metrics/query_response.json");
    this.server
        .expect(
            requestTo("http://localhost:9000/api/v1/query?query=up&time=2015-07-01T20:10:51.781Z"))
        .andRespond(withSuccess(queryResponse, MediaType.APPLICATION_JSON));

    MetricQuery query = new MetricQuery();
    query.setQuery("up");
    query.setTime(Instant.parse("2015-07-01T20:10:51.781Z").atZone(ZoneId.systemDefault()));

    MetricResponse response = client.query("http://localhost:9000", query);
    assertThat(response.getStatus()).isEqualTo(MetricResponse.Status.SUCCESS);
    assertThat(response.getData().getResultType()).isEqualTo(MetricResponse.ResultType.VECTOR);
    assertThat(response.getData().getResult()).hasSize(2);

    MetricResponse.Result firstResult = response.getData().getResult().get(0);
    assertThat(firstResult.getMetric())
        .containsExactlyEntriesOf(
            ImmutableMap.of(
                "__name__", "up",
                "job", "prometheus",
                "instance", "localhost:9090"));
    assertThat(firstResult.getValue()).isEqualTo(new ImmutablePair<>(1435781451.781d, 1d));
  }

  @Test
  public void testRangeQuery() {
    String queryRangeResponse = TestUtils.readResource("metrics/query_range_response.json");
    this.server
        .expect(
            requestTo(
                "http://localhost:9000/api/v1/query_range?query=up&start=2015-07-01T20:10:30.781Z"
                    + "&end=2015-07-01T20:11:00.781Z&step=15s"))
        .andRespond(withSuccess(queryRangeResponse, MediaType.APPLICATION_JSON));

    MetricRangeQuery query = new MetricRangeQuery();
    query.setQuery("up");
    query.setStart(Instant.parse("2015-07-01T20:10:30.781Z").atZone(ZoneId.systemDefault()));
    query.setEnd(Instant.parse("2015-07-01T20:11:00.781Z").atZone(ZoneId.systemDefault()));
    query.setStep(Duration.ofSeconds(15));

    MetricResponse response = client.queryRange("http://localhost:9000", query);
    assertThat(response.getStatus()).isEqualTo(MetricResponse.Status.SUCCESS);
    assertThat(response.getData().getResultType()).isEqualTo(MetricResponse.ResultType.MATRIX);
    assertThat(response.getData().getResult()).hasSize(2);

    MetricResponse.Result firstResult = response.getData().getResult().get(0);
    assertThat(firstResult.getMetric())
        .containsExactlyEntriesOf(
            ImmutableMap.of(
                "__name__", "up",
                "job", "prometheus",
                "instance", "localhost:9090"));
    assertThat(firstResult.getValues())
        .containsExactly(
            new ImmutablePair<>(1435781430.781d, 1d),
            new ImmutablePair<>(1435781445.781d, 1d),
            new ImmutablePair<>(1435781460.781d, 1d));
  }
}
