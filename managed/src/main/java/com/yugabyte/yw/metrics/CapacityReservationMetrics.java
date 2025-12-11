// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.utils.CapacityReservationUtil;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import io.prometheus.metrics.core.metrics.Gauge;
import io.prometheus.metrics.core.metrics.Histogram;
import io.prometheus.metrics.model.registry.PrometheusRegistry;
import java.util.UUID;
import java.util.function.Supplier;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public class CapacityReservationMetrics {

  public static final String RESERVATION = "ybp_capacity_reservation";
  public static final String RESERVATION_TIME = "ybp_capacity_reservation_time";

  private static final Gauge RESERVATION_GAUGE =
      initReservationGauge(PrometheusRegistry.defaultRegistry);

  private static final Histogram RESERVATION_TIME_HISTOGRAM =
      initReservationTimeHistogram(PrometheusRegistry.defaultRegistry);

  public static Gauge initReservationGauge(PrometheusRegistry registry) {
    return Gauge.builder()
        .name(RESERVATION)
        .help("Capacity reservation count")
        .labelNames(
            KnownAlertLabels.CLOUD_TYPE.labelName(),
            KnownAlertLabels.OPERATION_TYPE.labelName(),
            KnownAlertLabels.OPERATION_STATUS.labelName(),
            KnownAlertLabels.UNIVERSE_UUID.labelName(),
            KnownAlertLabels.REQUEST_UUID.labelName())
        .register(registry);
  }

  public static Histogram initReservationTimeHistogram(PrometheusRegistry registry) {
    return Histogram.builder()
        .name(RESERVATION_TIME)
        .help("Capacity reservation operation time")
        .labelNames(
            KnownAlertLabels.CLOUD_TYPE.labelName(),
            KnownAlertLabels.OPERATION_TYPE.labelName(),
            KnownAlertLabels.OPERATION_STATUS.labelName())
        .register(registry);
  }

  public Gauge getReservationGauge() {
    return RESERVATION_GAUGE;
  }

  public Histogram getReservationTimeHistogram() {
    return RESERVATION_TIME_HISTOGRAM;
  }

  public <T> T wrapWithMetrics(
      UUID universeUUID,
      int count,
      Common.CloudType cloudType,
      CapacityReservationUtil.ReservationAction reservationAction,
      Supplier<T> action) {
    if (action == null) {
      return null;
    }
    final long start = System.currentTimeMillis();
    String operationStatus = "success";
    UUID requestUUID = UUID.randomUUID();
    try {
      T result = action.get();
      try {
        getReservationGauge()
            .labelValues(
                cloudType.toString(),
                reservationAction.name(),
                operationStatus,
                universeUUID.toString(),
                requestUUID.toString())
            .set(count);
      } catch (Exception e) {
        log.error("Failed to update counter", e);
      }
      return result;
    } catch (RuntimeException e) {
      operationStatus = "failure";
      try {
        getReservationGauge()
            .labelValues(cloudType.toString(), reservationAction.name(), operationStatus)
            .set(count);
      } catch (Exception ex) {
        log.error("Failed to update counter", e);
      }
      throw e;
    } finally {
      try {
        getReservationTimeHistogram()
            .labelValues(cloudType.toString(), reservationAction.name(), operationStatus)
            .observe(System.currentTimeMillis() - start);
      } catch (Exception e) {
        log.error("Failed to update counter", e);
      }
    }
  }
}
