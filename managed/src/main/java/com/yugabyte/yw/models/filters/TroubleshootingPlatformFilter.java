/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.models.filters;

import java.util.Collection;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import lombok.Builder;
import lombok.NonNull;
import lombok.Value;

@Value
@Builder
public class TroubleshootingPlatformFilter {
  Set<UUID> uuids;
  UUID customerUuid;
  String tpUrl;

  // Can't use @Builder(toBuilder = true) as it sets null fields as well, which breaks non null
  // checks.
  public TroubleshootingPlatformFilterBuilder toBuilder() {
    TroubleshootingPlatformFilterBuilder result = TroubleshootingPlatformFilter.builder();
    if (uuids != null) {
      result.uuids(uuids);
    }
    if (customerUuid != null) {
      result.customerUuid(customerUuid);
    }
    if (tpUrl != null) {
      result.tpUrl(tpUrl);
    }
    return result;
  }

  public static class TroubleshootingPlatformFilterBuilder {
    Set<UUID> uuids = new HashSet<>();

    public TroubleshootingPlatformFilterBuilder uuid(@NonNull UUID uuid) {
      this.uuids.add(uuid);
      return this;
    }

    public TroubleshootingPlatformFilterBuilder uuids(@NonNull Collection<UUID> uuids) {
      this.uuids.addAll(uuids);
      return this;
    }

    public TroubleshootingPlatformFilterBuilder customerUuid(@NonNull UUID customerUuid) {
      this.customerUuid = customerUuid;
      return this;
    }

    public TroubleshootingPlatformFilterBuilder tpUrl(@NonNull String tpUrl) {
      this.tpUrl = tpUrl;
      return this;
    }
  }
}
