// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.FORBIDDEN;

import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ProviderEditRestrictionManager;
import com.yugabyte.yw.forms.AvailabilityZoneData;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.function.Consumer;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public class AvailabilityZoneHandler {
  @Inject private ProviderEditRestrictionManager providerEditRestrictionManager;

  public List<AvailabilityZone> createZones(Region region, List<AvailabilityZoneData> azDataList) {
    List<AvailabilityZone> result = new ArrayList<>();
    for (AvailabilityZoneData azData : azDataList) {
      AvailabilityZone az =
          AvailabilityZone.createOrThrow(
              region, azData.code, azData.name, azData.subnet, azData.secondarySubnet);
      result.add(az);
    }
    return result;
  }

  public AvailabilityZone editZone(
      UUID zoneUUID, UUID regionUUID, Consumer<AvailabilityZone> mutator) {
    AvailabilityZone az = AvailabilityZone.getByRegionOrBadRequest(zoneUUID, regionUUID);
    return providerEditRestrictionManager.tryEditProvider(
        az.getProvider().uuid,
        () -> {
          long nodeCount = az.getNodeCount();
          if (nodeCount > 0) {
            failDueToAZInUse(nodeCount, "modify");
          }
          mutator.accept(az);
          az.update();
          return az;
        });
  }

  public AvailabilityZone deleteZone(UUID zoneUUID, UUID regionUUID) {
    AvailabilityZone az = AvailabilityZone.getByRegionOrBadRequest(zoneUUID, regionUUID);
    return providerEditRestrictionManager.tryEditProvider(
        az.getProvider().uuid,
        () -> {
          long nodeCount = az.getNodeCount();
          if (nodeCount > 0) {
            failDueToAZInUse(nodeCount, "delete");
          }
          az.setActiveFlag(false);
          az.update();
          return az;
        });
  }

  private void failDueToAZInUse(long nodeCount, String action) {
    throw new PlatformServiceException(
        FORBIDDEN,
        String.format(
            "There %s %d node%s in this AZ, cannot %s",
            nodeCount > 1 ? "are" : "is", nodeCount, nodeCount > 1 ? "s" : "", action));
  }
}
