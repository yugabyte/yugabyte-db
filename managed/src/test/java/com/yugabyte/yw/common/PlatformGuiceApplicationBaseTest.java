// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.mockito.Mockito.mock;
import static play.inject.Bindings.bind;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.cloud.aws.AWSCloudImpl;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.common.alerts.AlertConfigurationWriter;
import com.yugabyte.yw.common.alerts.AlertsGarbageCollector;
import com.yugabyte.yw.common.alerts.QueryAlerts;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeoutException;
import org.apache.pekko.stream.Materializer;
import org.apache.pekko.stream.javadsl.Source;
import org.apache.pekko.util.ByteString;
import org.junit.After;
import org.junit.BeforeClass;
import org.mockito.Mockito;
import play.inject.guice.GuiceApplicationBuilder;
import play.mvc.Http;
import play.mvc.Http.MultipartFormData.Part;
import play.mvc.Http.Request;
import play.mvc.Http.RequestBuilder;
import play.mvc.Result;
import play.test.Helpers;
import play.test.WithApplication;

public abstract class PlatformGuiceApplicationBaseTest extends WithApplication {
  protected HealthChecker mockHealthChecker;
  protected QueryAlerts mockQueryAlerts;
  protected AlertsGarbageCollector mockAlertsGarbageCollector;
  protected AlertConfigurationWriter mockAlertConfigurationWriter;
  protected AWSCloudImpl mockAWSCloudImpl;

  protected Request fakeRequest = Helpers.fakeRequest().build();

  protected GuiceApplicationBuilder configureApplication(GuiceApplicationBuilder builder) {
    mockHealthChecker = mock(HealthChecker.class);
    mockQueryAlerts = mock(QueryAlerts.class);
    mockAlertConfigurationWriter = mock(AlertConfigurationWriter.class);
    mockAlertsGarbageCollector = mock(AlertsGarbageCollector.class);
    mockAWSCloudImpl = mock(AWSCloudImpl.class);

    return builder
        .overrides(bind(HealthChecker.class).toInstance(mockHealthChecker))
        .overrides(bind(QueryAlerts.class).toInstance(mockQueryAlerts))
        .overrides(bind(AlertConfigurationWriter.class).toInstance(mockAlertConfigurationWriter))
        .overrides(bind(AWSCloudImpl.class).toInstance(mockAWSCloudImpl))
        .overrides(bind(AlertsGarbageCollector.class).toInstance(mockAlertsGarbageCollector));
  }

  @After
  public void baseTearDown() {
    TestHelper.shutdownDatabase();
  }

  @BeforeClass
  public static void clearMocks() {
    Mockito.framework().clearInlineMocks();
  }

  public Result doRequest(String method, String url) {
    return FakeApiHelper.doRequest(app, method, url);
  }

  public Result doGetRequestNoAuth(String url) {
    return FakeApiHelper.doGetRequestNoAuth(app, url);
  }

  public Result doRequestWithAuthToken(String method, String url, String authToken) {
    return FakeApiHelper.doRequestWithAuthToken(app, method, url, authToken);
  }

  public Result doRequestWithJWT(String method, String url, String authToken) {
    return FakeApiHelper.doRequestWithJWT(app, method, url, authToken);
  }

  public Result doRequestWithCustomHeaders(String method, String url, Map<String, String> headers) {
    return FakeApiHelper.doRequestWithCustomHeaders(app, method, url, headers);
  }

  public Result doRequestWithHAToken(String method, String url, String haToken) {
    return FakeApiHelper.doRequestWithHAToken(app, method, url, haToken);
  }

  public Result doRequestWithHATokenAndBody(
      String method, String url, String haToken, JsonNode body) {
    return FakeApiHelper.doRequestWithHATokenAndBody(app, method, url, haToken, body);
  }

  public Result doRequestWithBody(String method, String url, JsonNode body) {
    return FakeApiHelper.doRequestWithBody(app, method, url, body);
  }

  public Result doRequestWithAuthTokenAndBody(
      String method, String url, String authToken, JsonNode body) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(app, method, url, authToken, body);
  }

  public Result doRequestWithBodyAndWithoutAuthToken(String method, String url, JsonNode body) {
    return FakeApiHelper.doRequestWithBodyAndWithoutAuthToken(app, method, url, body);
  }

  public Result doRequestWithJWTAndBody(
      String method, String url, String authToken, JsonNode body) {
    return FakeApiHelper.doRequestWithJWTAndBody(app, method, url, authToken, body);
  }

  public Result doRequestWithMultipartData(
      String method, String url, List<Part<Source<ByteString, ?>>> data, Materializer mat) {
    return FakeApiHelper.doRequestWithMultipartData(app, method, url, data, mat);
  }

  public Result doRequestWithAuthTokenAndMultipartData(
      String method,
      String url,
      String authToken,
      List<Http.MultipartFormData.Part<Source<ByteString, ?>>> data,
      Materializer mat) {
    return FakeApiHelper.doRequestWithAuthTokenAndMultipartData(
        app, method, url, authToken, data, mat);
  }

  /**
   * If you want to quickly fix existing test that returns YWError json when exception gets thrown
   * then use this function instead of Helpers.route(). Alternatively change the test to expect that
   * YWException get thrown
   */
  public Result routeWithYWErrHandler(RequestBuilder requestBuilder)
      throws InterruptedException, ExecutionException, TimeoutException {
    return FakeApiHelper.routeWithYWErrHandler(app, requestBuilder);
  }

  public Result route(RequestBuilder requestBuilder) {
    return Helpers.route(app, requestBuilder);
  }
}
