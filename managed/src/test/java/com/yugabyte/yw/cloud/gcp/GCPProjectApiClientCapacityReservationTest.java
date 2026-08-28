// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.cloud.gcp;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.google.api.services.compute.model.AllocationSpecificSKUReservation;
import com.google.api.services.compute.model.Reservation;
import java.time.Duration;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import org.junit.Test;

public class GCPProjectApiClientCapacityReservationTest {

  @Test
  public void testIsReservationFullyUtilized() {
    assertFalse(GCPProjectApiClient.isReservationFullyUtilized(null));
    assertFalse(GCPProjectApiClient.isReservationFullyUtilized(new Reservation()));

    Reservation reservation = reservationWithCounts(4L, 4L);
    assertTrue(GCPProjectApiClient.isReservationFullyUtilized(reservation));

    reservation = reservationWithCounts(4L, 2L);
    assertFalse(GCPProjectApiClient.isReservationFullyUtilized(reservation));

    reservation = reservationWithCounts(4L, 0L);
    assertFalse(GCPProjectApiClient.isReservationFullyUtilized(reservation));
  }

  @Test
  public void testIsReservationEmpty() {
    assertFalse(GCPProjectApiClient.isReservationEmpty(null));
    assertFalse(GCPProjectApiClient.isReservationEmpty(new Reservation()));

    Reservation reservation = reservationWithCounts(4L, 0L);
    assertTrue(GCPProjectApiClient.isReservationEmpty(reservation));

    reservation = reservationWithCounts(4L, null);
    assertTrue(GCPProjectApiClient.isReservationEmpty(reservation));

    reservation = reservationWithCounts(4L, 1L);
    assertFalse(GCPProjectApiClient.isReservationEmpty(reservation));

    reservation = reservationWithCounts(4L, 4L);
    assertFalse(GCPProjectApiClient.isReservationEmpty(reservation));
  }

  @Test
  public void testIsReservationOlderThan() {
    assertFalse(GCPProjectApiClient.isReservationOlderThan(null, Duration.ofHours(1)));
    assertFalse(GCPProjectApiClient.isReservationOlderThan(new Reservation(), Duration.ofHours(1)));

    Reservation oldReservation = new Reservation();
    oldReservation.setCreationTimestamp(
        OffsetDateTime.now(ZoneOffset.UTC).minusHours(2).toString());
    assertTrue(GCPProjectApiClient.isReservationOlderThan(oldReservation, Duration.ofHours(1)));
    assertFalse(GCPProjectApiClient.isReservationOlderThan(oldReservation, Duration.ofHours(3)));

    Reservation recentReservation = new Reservation();
    recentReservation.setCreationTimestamp(
        OffsetDateTime.now(ZoneOffset.UTC).minusMinutes(10).toString());
    assertFalse(GCPProjectApiClient.isReservationOlderThan(recentReservation, Duration.ofHours(1)));

    Reservation badTimestamp = new Reservation();
    badTimestamp.setName("r-bad");
    badTimestamp.setCreationTimestamp("not-a-timestamp");
    assertFalse(GCPProjectApiClient.isReservationOlderThan(badTimestamp, Duration.ofHours(1)));
  }

  private static Reservation reservationWithCounts(Long count, Long inUseCount) {
    AllocationSpecificSKUReservation specific = new AllocationSpecificSKUReservation();
    specific.setCount(count);
    specific.setInUseCount(inUseCount);
    Reservation reservation = new Reservation();
    reservation.setSpecificReservation(specific);
    return reservation;
  }
}
