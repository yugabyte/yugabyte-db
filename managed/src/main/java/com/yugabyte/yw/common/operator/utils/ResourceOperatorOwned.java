// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator.utils;

import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.Universe;
import java.util.Collections;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ResourceOperatorOwned {
  /** The 403 message for an API call blocked because the operator owns the resource. */
  public static final String CONTROLLED_BY_OPERATOR_MESSAGE =
      "resource is controlled by kubernetes operator";

  private final RuntimeConfGetter confGetter;

  @Inject
  ResourceOperatorOwned(RuntimeConfGetter confGetter) {
    this.confGetter = confGetter;
  }

  public Boolean isUniverseOwned(Universe universe) {
    return universe.getUniverseDetails().isKubernetesOperatorControlled;
  }

  /**
   * Whether the API block for operator-owned resources applies to the universe: the block is on and
   * the operator owns the universe. A UUID that is not a universe is not blocked. It reads only the
   * one field, since the task status poll calls it.
   *
   * <p>{@code BlockOperatorResourceHandler} applies the same rule to every annotated API call. It
   * reads the flag through {@code RuntimeConfigCache}, to avoid a runtime config read per request,
   * and fails on a missing universe, so it does not call this.
   */
  public boolean isUniverseBlocked(UUID universeUuid) {
    return confGetter.getGlobalConf(GlobalConfKeys.blockOperatorApiResources)
        && Universe.getUniverseDetailsField(
                String.class,
                universeUuid,
                UniverseDefinitionTaskParams.IS_KUBERNETES_OPERATOR_CONTROLLED_FIELD)
            .map(Boolean::parseBoolean)
            .orElse(false);
  }

  /** The universes for which {@link #isUniverseBlocked} holds, in one query. */
  public Set<UUID> getBlockedUniverseUUIDs(Long customerId, @Nullable UUID specificUniverseUuid) {
    if (!confGetter.getGlobalConf(GlobalConfKeys.blockOperatorApiResources)) {
      return Collections.emptySet();
    }
    return Universe.getUniverseDetailsFields(
            String.class,
            customerId,
            UniverseDefinitionTaskParams.IS_KUBERNETES_OPERATOR_CONTROLLED_FIELD,
            specificUniverseUuid)
        .entrySet()
        .stream()
        .filter(e -> Boolean.parseBoolean(e.getValue()))
        .map(Map.Entry::getKey)
        .collect(Collectors.toSet());
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
