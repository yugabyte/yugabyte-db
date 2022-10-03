/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.alerts;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.MaintenanceWindow;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.filters.MaintenanceWindowFilter;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.sql.Date;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.Silent.class)
public class AlertsGarbageCollectorTest extends FakeDBApplication {

  @Mock PlatformScheduler mockPlatformScheduler;

  @Mock Config mockAppConfig;

  @Mock RuntimeConfigFactory mockRuntimeConfigFactory;

  private AlertsGarbageCollector alertsGarbageCollector;

  private Customer customer;

  private MaintenanceService maintenanceService;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    when(mockRuntimeConfigFactory.forCustomer(customer)).thenReturn(mockAppConfig);
    when(mockAppConfig.getDuration(AlertsGarbageCollector.YB_ALERT_GC_RESOLVED_RETENTION_DURATION))
        .thenReturn(Duration.of(2, ChronoUnit.MINUTES));
    when(mockAppConfig.getDuration(AlertsGarbageCollector.YB_MAINTENANCE_WINDOW_RETENTION_DURATION))
        .thenReturn(Duration.of(120, ChronoUnit.DAYS));
    maintenanceService = app.injector().instanceOf(MaintenanceService.class);
    alertsGarbageCollector =
        new AlertsGarbageCollector(
            mockPlatformScheduler, mockRuntimeConfigFactory, alertService, maintenanceService);
  }

  @Test
  public void testAlertsGc() {
    Alert activeAlert = ModelFactory.createAlert(customer);
    Alert resolvedNotCollected = ModelFactory.createAlert(customer);
    resolvedNotCollected.setState(State.RESOLVED);
    resolvedNotCollected.setResolvedTime(Date.from(Instant.now().minus(1, ChronoUnit.MINUTES)));
    resolvedNotCollected.save();
    Alert resolvedCollected = ModelFactory.createAlert(customer);
    resolvedCollected.setState(State.RESOLVED);
    resolvedCollected.setResolvedTime(Date.from(Instant.now().minus(3, ChronoUnit.MINUTES)));
    resolvedCollected.save();

    alertsGarbageCollector.scheduleRunner();

    List<Alert> remainingAlerts = alertService.list(AlertFilter.builder().build());
    assertThat(remainingAlerts, containsInAnyOrder(activeAlert, resolvedNotCollected));
  }

  @Test
  public void testMaintenanceWindowGc() {
    MaintenanceWindow activeWindow = ModelFactory.createMaintenanceWindow(customer.getUuid());
    MaintenanceWindow endedNotCollected =
        ModelFactory.createMaintenanceWindow(
            customer.getUuid(),
            window -> window.setEndTime(CommonUtils.nowMinusWithoutMillis(119, ChronoUnit.DAYS)));
    MaintenanceWindow endedCollected =
        ModelFactory.createMaintenanceWindow(
            customer.getUuid(),
            window -> window.setEndTime(CommonUtils.nowMinusWithoutMillis(121, ChronoUnit.DAYS)));

    alertsGarbageCollector.scheduleRunner();

    List<MaintenanceWindow> remainingWindows =
        maintenanceService.list(MaintenanceWindowFilter.builder().build());
    assertThat(remainingWindows, containsInAnyOrder(activeWindow, endedNotCollected));
  }
}
