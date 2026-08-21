// Copyright (c) YugabyteDB, Inc.
package api.v2.mappers;

import api.v2.models.Universe;
import java.time.ZoneOffset;

public abstract class UniverseRespDecorator implements UniverseRespMapper {
  private final UniverseRespMapper delegate;

  public UniverseRespDecorator(UniverseRespMapper delegate) {
    this.delegate = delegate;
  }

  @Override
  public Universe toV2Universe(
      com.yugabyte.yw.forms.UniverseResp v1UniverseResp, com.yugabyte.yw.models.Universe universe) {
    Universe v2Universe = delegate.toV2Universe(v1UniverseResp, universe);
    if (v1UniverseResp == null) {
      return v2Universe;
    }

    // Fill out the rest of the universe info field using top-level properties from v1 UniverseResp.
    v2Universe.getSpec().setName(v1UniverseResp.name);
    delegate.fillV2UniverseInfoFromV1UniverseResp(v1UniverseResp, v2Universe.getInfo());
    // Same source as v1 UniverseResp.platformVersion (injected since platformVersion is no longer
    // in universe details).
    v2Universe.getInfo().setPlatformVersion(v1UniverseResp.platformVersion);
    // Use the platformUrl already resolved on the v1 wrapper (which prefers the local HA instance
    // address) so ybaUrl matches the YBA serving the request after any switchover.
    if (v1UniverseResp.universeDetails != null) {
      v2Universe.getInfo().setYbaUrl(v1UniverseResp.universeDetails.getPlatformUrl());
    }
    // Creation date is not mapped by fillV2UniverseInfoFromV1UniverseResp as v1UniverseResp returns
    // the date as a string. To avoid parsing the date string and assuming some specific locale,
    // we just set the date directly from the Date object.
    if (universe != null) {
      v2Universe
          .getInfo()
          .setCreationDate(universe.getCreationDate().toInstant().atOffset(ZoneOffset.UTC));
    }
    return v2Universe;
  }
}
