package com.yugabyte.yw.common.alerts;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.filters.MaintenanceWindowFilter;
import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.Counter;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Date;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class AlertsGarbageCollector {

  private static final int YB_ALERT_GC_INTERVAL_DAYS = 1;

  // Counter names
  private static final String ALERT_GC_COUNT = "ybp_alert_gc_count";
  private static final String MAINTENANCE_WINDOW_GC_COUNT = "ybp_maintenance_window_gc_count";
  private static final String ALERT_GC_RUN_COUNT = "ybp_alert_gc_run_count";

  // Counters
  private static final Counter ALERT_GC_COUNTER =
      Counter.build(ALERT_GC_COUNT, "Number of garbage collected alerts")
          .register(CollectorRegistry.defaultRegistry);
  private static final Counter MAINTENANCE_WINDOW_GC_COUNTER =
      Counter.build(MAINTENANCE_WINDOW_GC_COUNT, "Number of garbage collected maintenance windows")
          .register(CollectorRegistry.defaultRegistry);
  private static final Counter ALERT_GC_RUN_COUNTER =
      Counter.build(ALERT_GC_RUN_COUNT, "Number of alert GC runs")
          .register(CollectorRegistry.defaultRegistry);

  // Config names
  @VisibleForTesting
  static final String YB_ALERT_GC_RESOLVED_RETENTION_DURATION =
      "yb.alert.resolved_retention_duration";

  @VisibleForTesting
  static final String YB_MAINTENANCE_WINDOW_RETENTION_DURATION =
      "yb.maintenance.retention_duration";

  private final PlatformScheduler platformScheduler;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private final AlertService alertService;
  private final MaintenanceService maintenanceService;

  @Inject
  public AlertsGarbageCollector(
      PlatformScheduler platformScheduler,
      RuntimeConfigFactory runtimeConfigFactory,
      AlertService alertService,
      MaintenanceService maintenanceService) {
    this.platformScheduler = platformScheduler;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.alertService = alertService;
    this.maintenanceService = maintenanceService;
  }

  public void start() {
    platformScheduler.schedule(
        getClass().getSimpleName(),
        Duration.ZERO,
        Duration.of(YB_ALERT_GC_INTERVAL_DAYS, ChronoUnit.DAYS),
        this::scheduleRunner);
  }

  @VisibleForTesting
  void scheduleRunner() {
    try {
      Customer.getAll().forEach(this::checkCustomer);
    } catch (Exception e) {
      log.error("Error running alerts garbage collector", e);
    }
  }

  private void checkCustomer(Customer c) {
    ALERT_GC_RUN_COUNTER.inc();
    cleanAlerts(c);
    cleanMaintenanceWindows(c);
  }

  private void cleanAlerts(Customer c) {
    Date resolvedDateBefore = Date.from(Instant.now().minus(alertRetentionDuration(c)));
    AlertFilter filter =
        AlertFilter.builder()
            .customerUuid(c.getUuid())
            .resolvedDateBefore(resolvedDateBefore)
            .build();
    int deleted = alertService.delete(filter);
    ALERT_GC_COUNTER.inc(deleted);
  }

  private void cleanMaintenanceWindows(Customer c) {
    Date endTimeBefore = Date.from(Instant.now().minus(maintenanceWindowRetentionDuration(c)));
    MaintenanceWindowFilter filter =
        MaintenanceWindowFilter.builder()
            .customerUuid(c.getUuid())
            .endTimeBefore(endTimeBefore)
            .build();
    int deleted = maintenanceService.delete(filter);
    MAINTENANCE_WINDOW_GC_COUNTER.inc(deleted);
  }

  private Duration alertRetentionDuration(Customer customer) {
    return runtimeConfigFactory
        .forCustomer(customer)
        .getDuration(YB_ALERT_GC_RESOLVED_RETENTION_DURATION);
  }

  private Duration maintenanceWindowRetentionDuration(Customer customer) {
    return runtimeConfigFactory
        .forCustomer(customer)
        .getDuration(YB_MAINTENANCE_WINDOW_RETENTION_DURATION);
  }
}
