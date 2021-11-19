// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.mockito.Mockito.mock;
import static play.inject.Bindings.bind;

import com.google.common.collect.Maps;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.commissioner.CallHome;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.SetUniverseKey;
import com.yugabyte.yw.common.TaskInfoManager;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.scheduler.Scheduler;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeoutException;
import kamon.instrumentation.play.GuiceModule;
import org.junit.Before;
import org.pac4j.play.CallbackController;
import org.pac4j.play.store.PlayCacheSessionStore;
import org.pac4j.play.store.PlaySessionStore;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.modules.swagger.SwaggerModule;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

public class FakeDBApplication extends PlatformGuiceApplicationBaseTest {
  public Commissioner mockCommissioner = mock(Commissioner.class);
  public CallHome mockCallHome = mock(CallHome.class);
  public ApiHelper mockApiHelper = mock(ApiHelper.class);
  public KubernetesManager mockKubernetesManager = mock(KubernetesManager.class);
  public EncryptionAtRestManager mockEARManager = mock(EncryptionAtRestManager.class);
  public SetUniverseKey mockSetUniverseKey = mock(SetUniverseKey.class);
  public CallbackController mockCallbackController = mock(CallbackController.class);
  public PlayCacheSessionStore mockSessionStore = mock(PlayCacheSessionStore.class);
  public AccessManager mockAccessManager = mock(AccessManager.class);
  public TemplateManager mockTemplateManager = mock(TemplateManager.class);
  public MetricQueryHelper mockMetricQueryHelper = mock(MetricQueryHelper.class);
  public CloudQueryHelper mockCloudQueryHelper = mock(CloudQueryHelper.class);
  public CloudAPI.Factory mockCloudAPIFactory = mock(CloudAPI.Factory.class);
  public ReleaseManager mockReleaseManager = mock(ReleaseManager.class);
  public YBClientService mockService = mock(YBClientService.class);
  public DnsManager mockDnsManager = mock(DnsManager.class);
  public NetworkManager mockNetworkManager = mock(NetworkManager.class);
  public YamlWrapper mockYamlWrapper = mock(YamlWrapper.class);
  public Executors mockExecutors = mock(Executors.class);
  public ShellProcessHandler mockShellProcessHandler = mock(ShellProcessHandler.class);
  public TableManager mockTableManager = mock(TableManager.class);
  public TaskInfoManager mockTaskManager = mock(TaskInfoManager.class);
  public GFlagsValidation mockGFlagsValidation = mock(GFlagsValidation.class);
  public NodeManager mockNodeManager = mock(NodeManager.class);

  public MetricService metricService;
  public AlertService alertService;
  public AlertDefinitionService alertDefinitionService;
  public AlertConfigurationService alertConfigurationService;

  @Override
  protected Application provideApplication() {
    Map<String, Object> additionalConfiguration = new HashMap<>();
    return provideApplication(additionalConfiguration);
  }

  public Application provideApplication(Map<String, Object> additionalConfiguration) {

    GuiceApplicationBuilder guiceApplicationBuilder =
        new GuiceApplicationBuilder().disable(GuiceModule.class);
    if (!isSwaggerEnabled()) {
      guiceApplicationBuilder = guiceApplicationBuilder.disable(SwaggerModule.class);
    }
    return configureApplication(
            guiceApplicationBuilder
                .configure(additionalConfiguration)
                .configure(Maps.newHashMap(Helpers.inMemoryDatabase()))
                .overrides(bind(ApiHelper.class).toInstance(mockApiHelper))
                .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
                .overrides(bind(CallHome.class).toInstance(mockCallHome))
                .overrides(bind(Executors.class).toInstance(mockExecutors))
                .overrides(bind(EncryptionAtRestManager.class).toInstance(mockEARManager))
                .overrides(bind(SetUniverseKey.class).toInstance(mockSetUniverseKey))
                .overrides(bind(KubernetesManager.class).toInstance(mockKubernetesManager))
                .overrides(bind(CallbackController.class).toInstance(mockCallbackController))
                .overrides(bind(PlaySessionStore.class).toInstance(mockSessionStore))
                .overrides(bind(AccessManager.class).toInstance(mockAccessManager))
                .overrides(bind(TemplateManager.class).toInstance(mockTemplateManager))
                .overrides(bind(MetricQueryHelper.class).toInstance(mockMetricQueryHelper))
                .overrides(bind(CloudQueryHelper.class).toInstance(mockCloudQueryHelper))
                .overrides(bind(ReleaseManager.class).toInstance(mockReleaseManager))
                .overrides(bind(YBClientService.class).toInstance(mockService))
                .overrides(bind(NetworkManager.class).toInstance(mockNetworkManager))
                .overrides(bind(DnsManager.class).toInstance(mockDnsManager))
                .overrides(bind(YamlWrapper.class).toInstance(mockYamlWrapper))
                .overrides(bind(CloudAPI.Factory.class).toInstance(mockCloudAPIFactory))
                .overrides(bind(Scheduler.class).toInstance(mock(Scheduler.class)))
                .overrides(bind(ShellProcessHandler.class).toInstance(mockShellProcessHandler))
                .overrides(bind(TableManager.class).toInstance(mockTableManager))
                .overrides(bind(TaskInfoManager.class).toInstance(mockTaskManager))
                .overrides(bind(GFlagsValidation.class).toInstance(mockGFlagsValidation))
                .overrides(bind(NodeManager.class).toInstance(mockNodeManager)))
        .build();
  }

  protected boolean isSwaggerEnabled() {
    return false;
  }

  public Application getApp() {
    return app;
  }

  @Before
  public void baseSetUp() {
    metricService = app.injector().instanceOf(MetricService.class);
    alertService = app.injector().instanceOf(AlertService.class);
    alertDefinitionService = app.injector().instanceOf(AlertDefinitionService.class);
    alertConfigurationService = app.injector().instanceOf(AlertConfigurationService.class);
  }

  /**
   * If you want to quickly fix existing test that returns YWError json when exception gets thrown
   * then use this function instead of Helpers.route(). Alternatively change the test to expect that
   * YWException get thrown
   */
  public Result routeWithYWErrHandler(Http.RequestBuilder requestBuilder)
      throws InterruptedException, ExecutionException, TimeoutException {
    return FakeApiHelper.routeWithYWErrHandler(requestBuilder, getApp());
  }
}
