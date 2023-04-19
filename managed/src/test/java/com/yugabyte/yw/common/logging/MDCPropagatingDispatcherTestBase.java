package com.yugabyte.yw.common.logging;

import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.typesafe.config.ConfigValueFactory;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.CustomWsClientFactoryProvider;
import com.yugabyte.yw.common.PlatformGuiceApplicationBaseTest;
import com.yugabyte.yw.common.alerts.AlertConfigurationWriter;
import com.yugabyte.yw.common.alerts.AlertsGarbageCollector;
import com.yugabyte.yw.common.alerts.QueryAlerts;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import kamon.instrumentation.play.GuiceModule;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;

public abstract class MDCPropagatingDispatcherTestBase extends PlatformGuiceApplicationBaseTest {

  private Config mockConfig;
  private HealthChecker mockHealthChecker;
  private QueryAlerts mockQueryAlerts;
  private AlertsGarbageCollector mockAlertsGarbageCollector;
  private AlertConfigurationWriter mockAlertConfigurationWriter;

  @Override
  protected Application provideApplication() {
    mockConfig = mock(Config.class);
    mockHealthChecker = mock(HealthChecker.class);
    mockQueryAlerts = mock(QueryAlerts.class);
    mockAlertConfigurationWriter = mock(AlertConfigurationWriter.class);
    mockAlertsGarbageCollector = mock(AlertsGarbageCollector.class);
    when(mockConfig.getString(anyString())).thenReturn("");

    Config config =
        ConfigFactory.parseMap(testDatabase())
            .withValue(
                "akka.actor.default-dispatcher.type",
                ConfigValueFactory.fromAnyRef(
                    "com.yugabyte.yw.common.logging.MDCPropagatingDispatcherConfigurator"));
    return new GuiceApplicationBuilder()
        .disable(GuiceModule.class)
        .configure(config)
        .overrides(
            bind(RuntimeConfigFactory.class)
                .toInstance(new DummyRuntimeConfigFactoryImpl(mockConfig)))
        .overrides(bind(HealthChecker.class).toInstance(mockHealthChecker))
        .overrides(bind(QueryAlerts.class).toInstance(mockQueryAlerts))
        .overrides(bind(AlertConfigurationWriter.class).toInstance(mockAlertConfigurationWriter))
        .overrides(bind(AlertsGarbageCollector.class).toInstance(mockAlertsGarbageCollector))
        .overrides(
            bind(CustomWsClientFactory.class).toProvider(CustomWsClientFactoryProvider.class))
        .build();
  }
}
