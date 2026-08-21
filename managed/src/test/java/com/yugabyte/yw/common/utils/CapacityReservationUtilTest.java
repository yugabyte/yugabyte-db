// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.utils;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Provider;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class CapacityReservationUtilTest {

  @Parameters({
    "Standard_DS2_v2, true",
    "Standard_D8as_v4, true",
    "Standard_D10as_v5, true",
    "Standard_D2, false",
    "Standard_D8ads_v4, false", // too low version.
    "Standard_NV8as_v4, false"
  })
  @Test
  public void testSupportedInstances(String instanceType, boolean availability) {
    assertEquals(
        availability, CapacityReservationUtil.azureCheckInstanceTypeIsSupported(instanceType));
  }

  @Test
  public void testInitReservationForProviderGcp() {
    UUID providerUuid = UUID.randomUUID();
    Provider provider = mock(Provider.class);
    when(provider.getCloudCode()).thenReturn(Common.CloudType.gcp);
    when(provider.getUuid()).thenReturn(providerUuid);

    UniverseDefinitionTaskParams.CapacityReservationState state =
        new UniverseDefinitionTaskParams.CapacityReservationState();

    CapacityReservationUtil.initReservationForProvider(state, provider);

    assertTrue(state.getGcpReservationInfos().containsKey(providerUuid));
    UniverseDefinitionTaskParams.GcpReservationInfo first =
        state.getGcpReservationInfos().get(providerUuid);
    assertNotNull(first);

    // putIfAbsent semantics: re-initializing must not replace the existing entry.
    CapacityReservationUtil.initReservationForProvider(state, provider);
    assertSame(first, state.getGcpReservationInfos().get(providerUuid));
  }

  @Test
  public void testGetReservationIfPresentGcp() {
    UUID providerUuid = UUID.randomUUID();
    Provider provider = mock(Provider.class);
    when(provider.getCloudCode()).thenReturn(Common.CloudType.gcp);
    when(provider.getUuid()).thenReturn(providerUuid);

    // Build a minimal GCP reservation tree:
    //   zone us-central1-a -> n2-standard-4 -> zonedReservation containing yb-node-1
    UniverseDefinitionTaskParams.ZonedReservation zonedReservation =
        new UniverseDefinitionTaskParams.ZonedReservation();
    zonedReservation.setReservationName("gcp-reservation-1");
    zonedReservation.getVmNames().add("yb-node-1");

    UniverseDefinitionTaskParams.PerInstanceTypeReservation perInstanceType =
        new UniverseDefinitionTaskParams.PerInstanceTypeReservation();
    perInstanceType.getZonedReservation().put("us-central1-a", zonedReservation);

    UniverseDefinitionTaskParams.GcpZoneReservation zoneReservation =
        new UniverseDefinitionTaskParams.GcpZoneReservation();
    zoneReservation.getReservationsByType().put("n2-standard-4", perInstanceType);

    UniverseDefinitionTaskParams.GcpReservationInfo gcpInfo =
        new UniverseDefinitionTaskParams.GcpReservationInfo();
    gcpInfo.getReservationsByZoneMap().put("us-central1-a", zoneReservation);

    UniverseDefinitionTaskParams.CapacityReservationState state =
        new UniverseDefinitionTaskParams.CapacityReservationState();
    state.getGcpReservationInfos().put(providerUuid, gcpInfo);

    TaskExecutor.TaskCache taskCache = mock(TaskExecutor.TaskCache.class);
    // Single-arg get(...) is the null-check guard at the top of the method.
    when(taskCache.get(CapacityReservationUtil.CAPACITY_RESERVATION_KEY))
        .thenReturn(Json.toJson(state));
    when(taskCache.get(
            CapacityReservationUtil.CAPACITY_RESERVATION_KEY,
            UniverseDefinitionTaskParams.CapacityReservationState.class))
        .thenReturn(state);

    // Hit: node belongs to a GCP zoned reservation -> returns its name.
    assertEquals(
        "gcp-reservation-1",
        CapacityReservationUtil.getReservationIfPresent(taskCache, provider, "yb-node-1"));

    // Miss: unknown node -> null.
    assertNull(
        CapacityReservationUtil.getReservationIfPresent(taskCache, provider, "yb-node-unknown"));
  }
}
