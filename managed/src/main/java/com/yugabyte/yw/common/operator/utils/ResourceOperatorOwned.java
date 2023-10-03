// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.operator.utils;

import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ResourceOperatorOwned {
  private final RuntimeConfGetter confGetter;

  @Inject
  ResourceOperatorOwned(RuntimeConfGetter confGetter) {
    this.confGetter = confGetter;
  }

  public Boolean isUniverseOwned(Universe universe) {
    return universe.getUniverseDetails().isKubernetesOperatorControlled;
  }

  public Boolean isSupportBundleOwned(UUID bundleUUID) {
    SupportBundle bundle = SupportBundle.getOrBadRequest(bundleUUID);
    try {
      Universe universe = Universe.getOrBadRequest(bundle.getScopeUUID());
      return isUniverseOwned(universe);
    } catch (PlatformServiceException ex) {
      log.debug("universe '%s' not found for bundle %s", bundle.getScopeUUID(), bundleUUID);
      // Return true as if we can't find the universe, as all bundles created by operator need
      // a universe
      return true;
    }
  }
}
