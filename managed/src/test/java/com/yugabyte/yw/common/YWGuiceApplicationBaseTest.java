// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.mockito.Mockito.mock;
import static play.inject.Bindings.bind;

import com.yugabyte.yw.commissioner.CleanExpiredMetrics;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.commissioner.QueryAlerts;
import com.yugabyte.yw.common.alerts.AlertConfigurationWriter;
import com.yugabyte.yw.common.alerts.AlertsGarbageCollector;
import play.inject.guice.GuiceApplicationBuilder;
import play.test.WithApplication;

public abstract class YWGuiceApplicationBaseTest extends WithApplication {
  protected HealthChecker mockHealthChecker;
  protected QueryAlerts mockQueryAlerts;
  protected CleanExpiredMetrics mockCleanExpiredMetrics;
  protected AlertsGarbageCollector mockAlertsGarbageCollector;
  protected AlertConfigurationWriter mockAlertConfigurationWriter;

  protected GuiceApplicationBuilder configureApplication(GuiceApplicationBuilder builder) {
    mockHealthChecker = mock(HealthChecker.class);
    mockQueryAlerts = mock(QueryAlerts.class);
    mockCleanExpiredMetrics = mock(CleanExpiredMetrics.class);
    mockAlertConfigurationWriter = mock(AlertConfigurationWriter.class);
    mockAlertsGarbageCollector = mock(AlertsGarbageCollector.class);

    return builder
        .overrides(bind(HealthChecker.class).toInstance(mockHealthChecker))
        .overrides(bind(QueryAlerts.class).toInstance(mockQueryAlerts))
        .overrides(bind(CleanExpiredMetrics.class).toInstance(mockCleanExpiredMetrics))
        .overrides(bind(AlertConfigurationWriter.class).toInstance(mockAlertConfigurationWriter))
        .overrides(bind(AlertsGarbageCollector.class).toInstance(mockAlertsGarbageCollector));
  }
}
