// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
@Slf4j
public class DoCapacityReservationTest {

  private static final UUID UNIVERSE_UUID = UUID.fromString("11111111-2222-3333-4444-555555555555");

  // ---- extractZoneNumber ----
  // Pattern is "-([0-9]+)$": end-anchored. The anchor matters because
  // without it, "us-east-1-2" would match the first segment ("1") instead
  // of the trailing zone id ("2"), silent off-by-one in zone routing.

  @Test
  public void testExtractZoneNumberAzureStyle() {
    assertEquals(Integer.valueOf(1), DoCapacityReservation.extractZoneNumber("eastus-1"));
    assertEquals(Integer.valueOf(12), DoCapacityReservation.extractZoneNumber("centralus-12"));
  }

  @Test
  public void testExtractZoneNumberNoTrailingNumber() {
    // AWS/GCP zone codes don't end in -N; must fall through to -1.
    assertEquals(Integer.valueOf(-1), DoCapacityReservation.extractZoneNumber("us-east-1a"));
    assertEquals(Integer.valueOf(-1), DoCapacityReservation.extractZoneNumber("us-central1-a"));
  }

  @Test
  public void testExtractZoneNumberAnchorsOnTrailingSegment() {
    assertEquals(Integer.valueOf(2), DoCapacityReservation.extractZoneNumber("us-east-1-2"));
  }

  // ---- Azure group naming ----
  // "<universeUUID>_<region>_<clusterType>_RnG", region-scoped (zones share
  // a group). ClusterType disambiguation matters because primary and RR
  // can share a region, collision would cause the second cluster to reuse
  // the first cluster's group.

  @Test
  public void testAzureGroupNameFormat() {
    String name =
        DoCapacityReservation.getCapacityReservationGroupName(
            UNIVERSE_UUID, UniverseDefinitionTaskParams.ClusterType.PRIMARY, "eastus");
    assertEquals(UNIVERSE_UUID + "_eastus_PRIMARY_RnG", name);
    assertTrue(name.endsWith(DoCapacityReservation.GROUP_SUFFIX));
  }

  @Test
  public void testAzureGroupNameClusterTypeDisambiguates() {
    String primary =
        DoCapacityReservation.getCapacityReservationGroupName(
            UNIVERSE_UUID, UniverseDefinitionTaskParams.ClusterType.PRIMARY, "eastus");
    String readReplica =
        DoCapacityReservation.getCapacityReservationGroupName(
            UNIVERSE_UUID, UniverseDefinitionTaskParams.ClusterType.ASYNC, "eastus");
    assertNotEquals(primary, readReplica);
  }

  // ---- AWS reservation naming ----
  // "<type>-<zone>-<universeUUID>-<clusterType>", deterministic.
  // Determinism is what doAwsReservation's processedReservations dedup
  // relies on for retry safety, same inputs MUST produce the same name.

  @Test
  public void testAwsReservationNameFormatAndDeterminism() {
    String first =
        DoCapacityReservation.getZoneInstanceCapacityReservationName(
            UNIVERSE_UUID,
            UniverseDefinitionTaskParams.ClusterType.PRIMARY,
            "us-east-1a",
            "m5.large");
    String second =
        DoCapacityReservation.getZoneInstanceCapacityReservationName(
            UNIVERSE_UUID,
            UniverseDefinitionTaskParams.ClusterType.PRIMARY,
            "us-east-1a",
            "m5.large");
    assertEquals("m5.large-us-east-1a-" + UNIVERSE_UUID + "-PRIMARY", first);
    assertEquals(first, second);
  }

  // ---- GCP reservation naming ----
  // Random "r-<uuid>" per call, generated lazily in doGcpReservation.
  // Must satisfy GCP's RFC 1035 / 63-char constraint or the API rejects it.

  @Test
  public void testGcpReservationNameIsRandomAndRfc1035Compliant() {
    // Loop guards against a future format change that produces compliant
    // output for some UUID values but not others.
    for (int i = 0; i < 50; i++) {
      String name = DoCapacityReservation.getGcpZoneInstanceCapacityReservationName();
      assertTrue("expected r- prefix, got: " + name, name.startsWith("r-"));
      assertTrue("exceeds 63 chars: " + name, name.length() <= 63);
      assertTrue("not RFC 1035 compliant: " + name, name.matches("^[a-z]([-a-z0-9]*[a-z0-9])?$"));
    }
  }

  @Test
  public void testGcpReservationNameUniquePerCall() {
    // Contrast with AWS determinism: GCP returns a fresh UUID each call,
    // which is what allows two zones with the same instance type to get
    // distinct reservations in doGcpReservation.
    String n1 = DoCapacityReservation.getGcpZoneInstanceCapacityReservationName();
    String n2 = DoCapacityReservation.getGcpZoneInstanceCapacityReservationName();
    assertNotEquals(n1, n2);
  }
}
