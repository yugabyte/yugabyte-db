// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import com.google.api.services.compute.model.Reservation;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.cloud.gcp.GCPProjectApiClient;
import com.yugabyte.yw.cloud.gcp.GCPProjectApiClientFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Provider;
import java.io.IOException;
import java.time.Duration;
import java.util.List;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;

/**
 * Background garbage collector that periodically scans all GCP capacity reservations across all
 * zones in the GCP project and deletes reservations that are fully utilized (i.e., all reserved
 * instances have been consumed by running VMs).
 *
 * <p>A reservation is considered fully utilized when its {@code inUseCount} equals its {@code
 * count}. Such reservations are no longer needed because the instances they reserved have already
 * been provisioned, and the reservation only blocks capacity that could otherwise be released.
 *
 * <p>YBA uses a single GCP project across all providers, so this GC picks any available GCP
 * provider for credentials and scans the project once per cycle.
 *
 * <p>This runs as a singleton scheduled task using {@link PlatformScheduler}.
 */
@Singleton
@Slf4j
public class GcpCapacityReservationGC {

  private final PlatformScheduler platformScheduler;
  private final RuntimeConfGetter confGetter;
  private final GCPProjectApiClientFactory gcpClientFactory;

  @Inject
  public GcpCapacityReservationGC(
      PlatformScheduler platformScheduler,
      RuntimeConfGetter confGetter,
      GCPProjectApiClientFactory gcpClientFactory) {
    this.platformScheduler = platformScheduler;
    this.confGetter = confGetter;
    this.gcpClientFactory = gcpClientFactory;
  }

  /** Starts the periodic garbage collection schedule. */
  public void start() {
    Duration gcInterval = confGetter.getGlobalConf(GlobalConfKeys.gcpCapacityReservationGcInterval);
    platformScheduler.schedule(
        getClass().getSimpleName(), Duration.ZERO, gcInterval, this::scheduleRunner);
  }

  /**
   * Main GC loop. Finds any GCP provider for credentials, lists all reservations across zones in
   * the project, and deletes the fully utilized ones.
   */
  void scheduleRunner() {
    if (!confGetter.getGlobalConf(GlobalConfKeys.gcpCapacityReservationGcEnabled)) {
      log.debug("GCP capacity reservation GC is disabled, skipping run");
      return;
    }
    log.info("Running GCP Capacity Reservation Garbage Collector");
    try {
      Provider provider =
          Provider.getAll().stream()
              .filter(p -> p.getCloudCode() == Common.CloudType.gcp)
              .findFirst()
              .orElse(null);

      if (provider == null) {
        log.debug("No GCP provider found, skipping capacity reservation GC");
        return;
      }

      cleanupReservations(provider);
    } catch (Exception e) {
      log.error("Error running GCP capacity reservation garbage collector", e);
    }
  }

  /**
   * Lists all reservations in the GCP project and deletes any that are fully utilized.
   *
   * @param provider a GCP provider used for project credentials
   */
  private void cleanupReservations(Provider provider) {
    GCPProjectApiClient apiClient = gcpClientFactory.getClient(provider);

    Map<String, List<Reservation>> reservationsByZone;
    try {
      reservationsByZone = apiClient.listAllCapacityReservations();
    } catch (IOException e) {
      log.error("Failed to list GCP capacity reservations", e);
      return;
    }

    if (reservationsByZone.isEmpty()) {
      log.debug("No capacity reservations found in GCP project");
      return;
    }

    int totalReservations = 0;
    int deletedCount = 0;
    int failedCount = 0;

    for (Map.Entry<String, List<Reservation>> entry : reservationsByZone.entrySet()) {
      String zone = entry.getKey();
      List<Reservation> reservations = entry.getValue();

      for (Reservation reservation : reservations) {
        totalReservations++;
        String reservationName = reservation.getName();

        if (!GCPProjectApiClient.isReservationFullyUtilized(reservation)) {
          log.debug(
              "Reservation {} in zone {} is not fully utilized, skipping", reservationName, zone);
          continue;
        }

        log.info(
            "Reservation {} in zone {} is fully utilized (inUseCount == count), deleting",
            reservationName,
            zone);
        try {
          boolean deleted =
              apiClient.deleteCapacityReservation(
                  reservationName, zone, true /* deleteOnlyIfFullyUtilized */);
          if (deleted) {
            deletedCount++;
            log.info(
                "Successfully deleted fully utilized reservation {} in zone {}",
                reservationName,
                zone);
          } else {
            // deleteCapacityReservation returned false - reservation state may have changed
            // between list and delete; this is not an error.
            log.info(
                "Reservation {} in zone {} was not deleted (state may have changed)",
                reservationName,
                zone);
          }
        } catch (IOException e) {
          failedCount++;
          log.error("Failed to delete reservation {} in zone {}", reservationName, zone, e);
        }
      }
    }

    log.info(
        "GCP capacity reservation GC complete: total={}, deleted={}, failed={}",
        totalReservations,
        deletedCount,
        failedCount);
  }
}
